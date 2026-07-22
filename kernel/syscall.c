#include "syscall.h"
#include "console.h"
#include "vfs.h"
#include "sched.h"
#include "agent.h"
#include "peakvec.h"
#include "guiproto.h"
#include "elf.h"
#include "vmm.h"
#include "util.h"
#include "idt.h"
#include "cap.h"

/* Set by elf.c when user process exits */
extern void proc_notify_exit(int code);
extern void proc_finish_exit(int code);

/* Fallback global table when no task context (early boot). */
static char open_paths[8][VFS_PATH_MAX];
static int open_used[8];

void syscall_init(void) {
    memset(open_paths, 0, sizeof(open_paths));
    memset(open_used, 0, sizeof(open_used));
}

static struct proc_fd *fd_table(int *is_proc) {
    struct task *t = sched_current();
    if (t && t->pid > 0) {
        *is_proc = 1;
        return t->fds;
    }
    *is_proc = 0;
    return NULL;
}

static int64_t sys_write(int fd, const void *user_buf, size_t len) {
    (void)fd;
    if (len > 4096)
        len = 4096;
    char kbuf[4096];
    if (copy_from_user(kbuf, user_buf, len) != 0)
        return -1;
    for (size_t i = 0; i < len; i++)
        console_putc(kbuf[i]);
    return (int64_t)len;
}

static int64_t sys_open(const char *user_path) {
    if (!cap_check(CAP_FS_READ))
        return -1;
    char path[VFS_PATH_MAX];
    if (copyinstr_from_user(path, user_path, VFS_PATH_MAX) != 0)
        return -1;
    if (!vfs_lookup(path))
        return -1;
    int is_proc = 0;
    struct proc_fd *fds = fd_table(&is_proc);
    if (is_proc && fds) {
        for (int i = 0; i < PROC_FD_MAX; i++) {
            if (!fds[i].used) {
                fds[i].used = 1;
                size_t n = strlen(path);
                if (n >= sizeof(fds[i].path))
                    n = sizeof(fds[i].path) - 1;
                memcpy(fds[i].path, path, n);
                fds[i].path[n] = '\0';
                fds[i].rights = CAP_FS_READ;
                fds[i].offset = 0;
                return i;
            }
        }
        return -1;
    }
    for (int i = 0; i < 8; i++) {
        if (!open_used[i]) {
            open_used[i] = 1;
            size_t n = strlen(path);
            if (n >= VFS_PATH_MAX)
                n = VFS_PATH_MAX - 1;
            memcpy(open_paths[i], path, n + 1);
            return i;
        }
    }
    return -1;
}

static int64_t sys_read(int fd, void *user_buf, size_t len) {
    const char *path = NULL;
    int is_proc = 0;
    struct proc_fd *fds = fd_table(&is_proc);
    if (is_proc && fds) {
        if (fd < 0 || fd >= PROC_FD_MAX || !fds[fd].used)
            return -1;
        path = fds[fd].path;
    } else {
        if (fd < 0 || fd >= 8 || !open_used[fd])
            return -1;
        path = open_paths[fd];
    }
    if (len > 4096)
        len = 4096;
    char kbuf[4096];
    size_t out = 0;
    if (vfs_read_file(path, kbuf, len, &out) != 0)
        return -1;
    if (copy_to_user(user_buf, kbuf, out) != 0)
        return -1;
    return (int64_t)out;
}

static int64_t sys_close(int fd) {
    int is_proc = 0;
    struct proc_fd *fds = fd_table(&is_proc);
    if (is_proc && fds) {
        if (fd < 0 || fd >= PROC_FD_MAX)
            return -1;
        fds[fd].used = 0;
        fds[fd].path[0] = '\0';
        return 0;
    }
    if (fd < 0 || fd >= 8)
        return -1;
    open_used[fd] = 0;
    return 0;
}

void syscall_handler(struct interrupt_frame *frame) {
#if defined(__aarch64__)
    uint64_t num = frame->x[8];
    uint64_t a0 = frame->x[0];
    uint64_t a1 = frame->x[1];
    uint64_t a2 = frame->x[2];
    uint64_t a3 = frame->x[3];
#else
    uint64_t num = frame->rax;
    uint64_t a0 = frame->rdi;
    uint64_t a1 = frame->rsi;
    uint64_t a2 = frame->rdx;
    uint64_t a3 = frame->rcx;
#endif
    int64_t ret = -1;
    (void)a3;

    switch (num) {
    case SYS_exit:
        proc_notify_exit((int)a0);
        console_printf("\n[pid %d exited %d]\n", sched_current_pid(), (int)a0);
        proc_finish_exit((int)a0);
        break;
    case SYS_write:
        ret = sys_write((int)a0, (const void *)a1, (size_t)a2);
        break;
    case SYS_read:
        ret = sys_read((int)a0, (void *)a1, (size_t)a2);
        break;
    case SYS_open:
        ret = sys_open((const char *)a0);
        break;
    case SYS_close:
        ret = sys_close((int)a0);
        break;
    case SYS_getpid:
        ret = sched_current_pid();
        break;
    case SYS_listdir: {
        if (!cap_check(CAP_FS_READ)) {
            ret = -1;
            break;
        }
        char path[VFS_PATH_MAX];
        char listbuf[1024];
        if (copyinstr_from_user(path, (const void *)a0, VFS_PATH_MAX) != 0) {
            ret = -1;
            break;
        }
        if (vfs_list(path, listbuf, sizeof(listbuf)) != 0)
            ret = -1;
        else {
            size_t n = strlen(listbuf);
            if (n > a2)
                n = a2;
            if (copy_to_user((void *)a1, listbuf, n) != 0)
                ret = -1;
            else
                ret = (int64_t)n;
        }
        break;
    }
    case SYS_agent: {
        if (!cap_check(CAP_AGENT)) {
            ret = -1;
            break;
        }
        if (a0 == 1) {
            char goal[256];
            if (copyinstr_from_user(goal, (const void *)a1, sizeof(goal)) != 0) {
                ret = -1;
                break;
            }
            ret = agent_syscall(1, (uint64_t)(uintptr_t)goal, 0, 0);
        } else if (a0 == 2) {
            char tools[128];
            int64_t n = agent_syscall(2, (uint64_t)(uintptr_t)tools, sizeof(tools), 0);
            if (n < 0) {
                ret = -1;
                break;
            }
            if ((size_t)n > a2)
                n = (int64_t)a2;
            if (copy_to_user((void *)a1, tools, (size_t)n) != 0)
                ret = -1;
            else
                ret = n;
        } else {
            ret = -1;
        }
        break;
    }
    case SYS_exec: {
        char path[VFS_PATH_MAX];
        if (copyinstr_from_user(path, (const void *)a0, VFS_PATH_MAX) != 0) {
            ret = -1;
            break;
        }
        ret = proc_exec(path, 0, NULL);
        break;
    }
    case SYS_peakvec: {
        /* a0=op: 1=upsert_text 2=query_text 3=count 4=delete
         * Requires CAP_VEC (or CAP_AGENT for agent namespace ops). */
        if (!cap_check(CAP_VEC) && !cap_check(CAP_AGENT)) {
            ret = -1;
            break;
        }
        if (a0 == 3) {
            ret = peakvec_count("agent");
            break;
        }
        if (a0 == 1 || a0 == 4) {
            char key[PEAKVEC_KEY_MAX];
            char text[256];
            if (copyinstr_from_user(key, (const void *)a1, sizeof(key)) != 0) {
                ret = -1;
                break;
            }
            if (a0 == 4) {
                ret = peakvec_delete("agent", key) == 0 ? 0 : -1;
                break;
            }
            if (copyinstr_from_user(text, (const void *)a2, sizeof(text)) != 0) {
                ret = -1;
                break;
            }
            int16_t vec[PEAKVEC_DIM];
            peakvec_embed_text(text, vec);
            ret = peakvec_upsert("agent", key, vec, text) == 0 ? 0 : -1;
            break;
        }
        if (a0 == 2) {
            char text[256];
            if (copyinstr_from_user(text, (const void *)a1, sizeof(text)) != 0) {
                ret = -1;
                break;
            }
            struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
            int16_t vec[PEAKVEC_DIM];
            peakvec_embed_text(text, vec);
            int topk = (int)a3;
            if (topk <= 0 || topk > PEAKVEC_TOPK_MAX)
                topk = 3;
            int n = peakvec_query("agent", vec, topk, hits);
            if (n < 0) {
                ret = -1;
                break;
            }
            size_t bytes = (size_t)n * sizeof(hits[0]);
            if (copy_to_user((void *)a2, hits, bytes) != 0)
                ret = -1;
            else
                ret = n;
            break;
        }
        ret = -1;
        break;
    }
    case SYS_peakgui: {
        /* a0 = pointer to struct gui_msg in user memory */
        struct gui_msg msg;
        if (copy_from_user(&msg, (const void *)a0, sizeof(msg)) != 0) {
            ret = -1;
            break;
        }
        ret = guiproto_dispatch(&msg);
        break;
    }
    default:
        ret = -1;
        break;
    }
#if defined(__aarch64__)
    frame->x[0] = (uint64_t)ret;
#else
    frame->rax = (uint64_t)ret;
#endif
}
