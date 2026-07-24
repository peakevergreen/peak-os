#include "sched.h"
#include "cap.h"
#include "fpu.h"
#include "heap.h"
#include "pmm.h"
#include "sync.h"
#include "timer.h"
#include "util.h"
#include "vmm.h"

extern void context_switch(uint64_t **old_sp, uint64_t *new_sp);

static void switch_to(struct task *prev, struct task *next) {
    if (prev && prev != next) {
        fpu_save(prev->fx);
        prev->fx_valid = 1;
    }
    if (next) {
        if (!next->fx_valid) {
            fpu_clear(next->fx);
            next->fx_valid = 1;
        }
        fpu_restore(next->fx);
    }
    context_switch(&prev->rsp, next->rsp);
}

static struct task tasks[MAX_TASKS];
static struct task *current;
static int next_pid = 1;
static volatile int need_resched;
static uint64_t ctx_switches;
static struct spinlock sched_lock;
static int zombie_stacks_freed;
static uint32_t ready_mask;
/* Earliest TASK_BLOCKED wake_tick, or 0 if no timed sleepers (UP cooperative). */
static uint64_t next_sleeper_wake;

static int task_slot(const struct task *t) {
    return (int)(t - tasks);
}

static void ready_mark(int i) {
    if (i >= 0 && i < MAX_TASKS)
        ready_mask |= (1u << i);
}

static void ready_unmark(int i) {
    if (i >= 0 && i < MAX_TASKS)
        ready_mask &= ~(1u << i);
}

static void reap_task_stack(struct task *t) {
    if (!t || !t->kstack_base)
        return;
    kfree(t->kstack_base);
    t->kstack_base = NULL;
    t->rsp = NULL;
    zombie_stacks_freed++;
}

static void kthread_trampoline(void) {
    void (*fn)(void) = current->entry;
    if (fn)
        fn();
    sched_exit();
}

void sched_init(void) {
    spin_init(&sched_lock, "sched");
    memset(tasks, 0, sizeof(tasks));
    current = &tasks[0];
    current->pid = next_pid++;
    current->state = TASK_RUNNING;
    memcpy(current->name, "idle", 5);
    current->is_user = 0;
    current->rsp = NULL; /* filled on first switch away */
    current->spawned_at = timer_ticks();
    need_resched = 0;
    ctx_switches = 0;
    ready_mask = 0;
    next_sleeper_wake = 0;
}

struct task *sched_current(void) {
    return current;
}

int sched_current_pid(void) {
    return current ? current->pid : 0;
}

uint64_t sched_ctx_switches(void) {
    return ctx_switches;
}

int sched_task_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_ZOMBIE)
            n++;
    return n;
}

int sched_list_tasks(struct task *out, int max) {
    if (!out || max <= 0)
        return 0;
    int n = 0;
    spin_lock(&sched_lock);
    for (int i = 0; i < MAX_TASKS && n < max; i++) {
        if (tasks[i].state == TASK_UNUSED)
            continue;
        out[n++] = tasks[i];
    }
    spin_unlock(&sched_lock);
    return n;
}

int sched_zombie_stacks_freed(void) {
    return zombie_stacks_freed;
}

int sched_spawn_kthread(const char *name, void (*entry)(void)) {
    if (!entry)
        return -1;
    spin_lock(&sched_lock);
    struct task *t = NULL;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_ZOMBIE) {
            t = &tasks[i];
            break;
        }
    }
    if (!t) {
        spin_unlock(&sched_lock);
        return -1;
    }

    /* Free prior stack before slot reuse. */
    if (t->state == TASK_ZOMBIE)
        reap_task_stack(t);

    memset(t, 0, sizeof(*t));
    t->pid = next_pid++;
    t->entry = entry;
    t->is_user = 0;
    t->caps = CAP_DEFAULT_SHELL;
    t->spawned_at = timer_ticks();
    size_t n = 0;
    if (name) {
        while (name[n] && n + 1 < TASK_NAME_MAX) {
            t->name[n] = name[n];
            n++;
        }
    }
    t->name[n] = '\0';

    /* Allocate kernel stack (must not hold lock across kmalloc if heap locks — unlock briefly) */
    spin_unlock(&sched_lock);
    void *stack = kzalloc(KSTACK_SIZE);
    spin_lock(&sched_lock);
    if (!stack) {
        t->state = TASK_UNUSED;
        spin_unlock(&sched_lock);
        return -1;
    }
    t->kstack_base = (uint64_t *)stack;

    /* Build initial frame matching context_switch restore order */
    uint8_t *top = (uint8_t *)stack + KSTACK_SIZE;
    uint64_t *sp = (uint64_t *)top;
#if defined(__aarch64__)
    /* AAPCS64 frame: x19-x30 (112 bytes), ret uses x30 */
    sp -= 14;
    for (int i = 0; i < 13; i++)
        sp[i] = 0;
    sp[13] = (uint64_t)kthread_trampoline; /* x30 / lr */
#else
    *(--sp) = (uint64_t)kthread_trampoline; /* rip via ret */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */
#endif
    t->rsp = sp;
    t->state = TASK_READY;
    ready_mark(task_slot(t));
    spin_unlock(&sched_lock);
    return t->pid;
}

static struct task *pick_next(void) {
    uint32_t mask = ready_mask;
    if (!mask) {
        if (current && current->state == TASK_RUNNING)
            return current;
        if (tasks[0].state == TASK_READY || tasks[0].state == TASK_RUNNING)
            return &tasks[0];
        return current;
    }

    int start = 0;
    if (current) {
        start = task_slot(current) + 1;
        if (start >= MAX_TASKS)
            start = 0;
    }

    uint32_t tail = start ? mask >> start : mask;
    if (tail) {
        int i = start + __builtin_ctz(tail);
        return &tasks[i];
    }
    int i = __builtin_ctz(mask);
    return &tasks[i];
}

void sched_yield(void) {
    spin_lock(&sched_lock);
    struct task *prev = current;
    if (prev && prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
        ready_mark(task_slot(prev));
    }
    /* BLOCKED / ZOMBIE stay as-is — never forced READY here. */

    struct task *next = pick_next();
    if (!next || next == prev) {
        if (prev && prev->state == TASK_READY)
            prev->state = TASK_RUNNING;
        ready_unmark(task_slot(prev));
        /* If prev is BLOCKED and nothing else is READY, fall through to idle. */
        if (prev && (prev->state == TASK_BLOCKED || prev->state == TASK_ZOMBIE) &&
            tasks[0].state != TASK_UNUSED) {
            next = &tasks[0];
            if (next != prev) {
                current = next;
                next->state = TASK_RUNNING;
                ready_unmark(task_slot(next));
                ctx_switches++;
                spin_unlock(&sched_lock);
                switch_to(prev, next);
                return;
            }
        }
        spin_unlock(&sched_lock);
        return;
    }

    current = next;
    next->state = TASK_RUNNING;
    ready_unmark(task_slot(next));
    ctx_switches++;
    spin_unlock(&sched_lock);

    /* Switch stacks — must not hold lock across switch */
    switch_to(prev, next);
}

void sched_exit(void) {
    spin_lock(&sched_lock);
    if (current) {
        ready_unmark(task_slot(current));
        current->state = TASK_ZOMBIE;
        current->entry = NULL;
        /* Stack freed when the slot is reused (or via explicit reap). */
    }
    spin_unlock(&sched_lock);
    for (;;)
        sched_yield();
}

void sched_wake_sleepers(void) {
    uint64_t now = timer_ticks();
    /*
     * Fast path: no timed sleepers, or none due yet. Avoid scanning all
     * task slots on every PIT tick while the system is idle.
     */
    if (!next_sleeper_wake || now < next_sleeper_wake)
        return;

    uint64_t next = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_BLOCKED || !tasks[i].wake_tick)
            continue;
        if (now >= tasks[i].wake_tick) {
            tasks[i].wake_tick = 0;
            tasks[i].state = TASK_READY;
            ready_mark(i);
        } else if (!next || tasks[i].wake_tick < next) {
            next = tasks[i].wake_tick;
        }
    }
    next_sleeper_wake = next;
}

void sched_sleep_ticks(uint64_t ticks) {
    if (ticks == 0)
        return;
    spin_lock(&sched_lock);
    if (current) {
        ready_unmark(task_slot(current));
        current->wake_tick = timer_ticks() + ticks;
        if (!next_sleeper_wake || current->wake_tick < next_sleeper_wake)
            next_sleeper_wake = current->wake_tick;
        current->state = TASK_BLOCKED;
    }
    spin_unlock(&sched_lock);
    sched_yield();
    /* If we woke early / only task, spin-yield until deadline. */
    while (current && current->wake_tick && timer_ticks() < current->wake_tick) {
        sched_yield();
        hlt_if_enabled();
    }
    if (current)
        current->wake_tick = 0;
}

void sched_on_timer(void) {
    if (current)
        current->cpu_ticks++;
    sched_wake_sleepers();
    /*
     * Cooperative UP: only raise the resched hint when another task is
     * READY (including any sleeper just woken above). Avoids shell/desktop
     * preempt checks thrashing need_resched on every idle tick.
     */
    uint32_t others = ready_mask;
    if (current)
        others &= ~(1u << (unsigned)task_slot(current));
    if (others)
        need_resched = 1;
}

void sched_maybe_preempt(void) {
    if (!need_resched)
        return;
    need_resched = 0;
    uint32_t others = ready_mask;
    if (current)
        others &= ~(1u << (unsigned)task_slot(current));
    if (others)
        sched_yield();
}

static void net_worker(void) {
    /*
     * Adaptive poll: wake often while traffic is seen, otherwise back off
     * so idle systems are not burning 100 Hz wakeups.
     */
    uint64_t idle_streak = 0;
    for (;;) {
        extern void net_poll(void);
        extern void ctr_poll(void);
        extern int netdev_rx_pending(void);
        int busy = 0;
        if (netdev_rx_pending())
            busy = 1;
        net_poll();
        ctr_poll();
        if (busy)
            idle_streak = 0;
        else if (idle_streak < 50)
            idle_streak++;
        uint64_t sleep = idle_streak < 5 ? 1 : (idle_streak < 20 ? 5 : 20);
        sched_sleep_ticks(sleep);
    }
}

static void agent_worker(void) {
    /* Local agent only — no host serial bridge. */
    for (;;)
        sched_sleep_ticks(50);
}

void sched_start_background(void) {
    sched_spawn_kthread("netd", net_worker);
    sched_spawn_kthread("agentd", agent_worker);
}
