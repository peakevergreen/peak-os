#ifndef PEAK_SCHED_H
#define PEAK_SCHED_H

#include "fpu.h"
#include "types.h"

#define TASK_NAME_MAX 32
#define MAX_TASKS 32
#define KSTACK_PAGES 2
#define KSTACK_SIZE  (KSTACK_PAGES * 4096)

enum task_state {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE,
};

#define PROC_FD_MAX 32

struct proc_fd {
    int used;
    char path[96];
    uint32_t rights; /* CAP_* subset */
    size_t offset;
};

struct task {
    int pid;
    enum task_state state;
    char name[TASK_NAME_MAX];
    uint64_t *kstack_base; /* kmalloc'd stack memory */
    uint64_t *rsp;         /* saved stack pointer for context_switch */
    uint64_t cr3;
    int is_user;
    void (*entry)(void);
    uint64_t cpu_ticks;    /* approximate timer ticks spent running */
    uint64_t spawned_at;
    uint64_t wake_tick;    /* for TASK_BLOCKED timer sleep; 0 = none */
    uint32_t caps;         /* capability bits for this task */
    struct proc_fd fds[PROC_FD_MAX];
    /* Complete architecture floating-point state, 16-byte aligned. */
    uint8_t fx[FPU_STATE_SIZE] __attribute__((aligned(16)));
    int fx_valid;
};

void sched_init(void);
/* Create a READY kernel thread (does not run until sched_yield). Returns pid. */
int  sched_spawn_kthread(const char *name, void (*entry)(void));
void sched_yield(void);
void sched_exit(void); /* current kthread finishes */
void sched_sleep_ticks(uint64_t ticks); /* wait-queue sleep until timer */
void sched_wake_sleepers(void);         /* called from timer path */
int  sched_zombie_stacks_freed(void);   /* diagnostic: stacks reaped */
void sched_on_timer(void);
void sched_maybe_preempt(void); /* cooperative check of need_resched */
void sched_start_background(void); /* net + agent worker kthreads */
struct task *sched_current(void);
int  sched_current_pid(void);
int  sched_task_count(void);
/* Fill out[0..max) with live tasks; returns count */
int  sched_list_tasks(struct task *out, int max);
uint64_t sched_ctx_switches(void);

#endif
