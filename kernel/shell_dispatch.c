#include "shell.h"
#include "shell_split.h"
#include "console.h"
#include "elf.h"
#include "util.h"
#include "vfs.h"

static const char PIPE_PATH[] = "/tmp/.peak_pipe";

static int run_simple(char **argv, int argc) {
    if (argc < 1 || !argv || !argv[0])
        return -999;
    char path[64];
    path[0] = '/';
    path[1] = 'b';
    path[2] = 'i';
    path[3] = 'n';
    path[4] = '/';
    size_t i = 5;
    for (size_t j = 0; argv[0][j] && i + 1 < sizeof(path); j++)
        path[i++] = argv[0][j];
    path[i] = '\0';
    return proc_exec(path, argc, argv);
}

static int write_redir(const char *path, const char *data, size_t len, int append) {
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)) != 0)
        return -1;
    if (append) {
        char old[SHELL_CAPTURE_MAX];
        size_t old_n = 0;
        if (vfs_read_file(abs, old, sizeof(old) - 1, &old_n) != 0)
            old_n = 0;
        if (old_n + len >= sizeof(old))
            return -1;
        memcpy(old + old_n, data, len);
        old_n += len;
        old[old_n] = '\0';
        return vfs_write_file(abs, old, old_n);
    }
    return vfs_write_file(abs, data, len);
}

static int run_stage(struct shell_stage *st, int capture_out, char *cap_buf, size_t cap_sz,
                     size_t *cap_out) {
    char *argv_local[SHELL_ARGV_MAX];
    int argc = st->argc;
    for (int i = 0; i < argc; i++)
        argv_local[i] = st->argv[i];
    argv_local[argc] = 0;

    if (st->redir_in.kind == SHELL_REDIR_IN && st->redir_in.path) {
        char abs[VFS_PATH_MAX];
        if (shell_resolve_path(st->redir_in.path, abs, sizeof(abs)) != 0) {
            console_write("shell: cannot open input redirect\n");
            return 1;
        }
        shell_set_stdin_path(abs);
        if (argc == 1 && argc < SHELL_ARGV_MAX - 1) {
            argv_local[argc++] = (char *)"-";
            argv_local[argc] = 0;
        }
    }

    int want_cap = capture_out || st->redir_out.kind != SHELL_REDIR_NONE;
    if (want_cap)
        console_capture_begin(cap_buf, cap_sz);

    int rc = run_simple(argv_local, argc);

    size_t n = 0;
    if (want_cap)
        n = console_capture_end();
    if (cap_out)
        *cap_out = n;

    shell_set_stdin_path(0);

    if (rc == -999) {
        console_write("Unknown command. Try 'help'.\n");
        return rc;
    }

    if (st->redir_out.kind != SHELL_REDIR_NONE && st->redir_out.path) {
        int ap = (st->redir_out.kind == SHELL_REDIR_APPEND);
        if (write_redir(st->redir_out.path, cap_buf, n, ap) != 0) {
            console_write("shell: redirect write failed\n");
            return 1;
        }
    }

    return rc;
}

/*
 * Mutates cmd in place (quote-split writes NULs). Callers must pass a
 * writable buffer — avoids a per-command 256B stack copy.
 */
void shell_execute(char *cmd) {
    if (!cmd)
        return;
    while (*cmd == ' ')
        cmd++;
    if (!*cmd)
        return;

    /* export NAME=val as shorthand — rest is one argv, no re-split */
    if (!strncmp(cmd, "export ", 7)) {
        char *rest = cmd + 7;
        while (*rest == ' ')
            rest++;
        if (!*rest)
            return;
        char *argv[3] = { "export", rest, NULL };
        char path[] = "/bin/export";
        proc_exec(path, 2, argv);
        return;
    }

    struct shell_pipeline pl;
    if (shell_parse_pipeline(cmd, &pl) != 0) {
        console_write("shell: parse error (pipes/redirects)\n");
        return;
    }

    (void)vfs_mkdir("/tmp");

    char cap[SHELL_CAPTURE_MAX];
    char pipe_data[SHELL_CAPTURE_MAX];
    size_t pipe_len = 0;

    for (int s = 0; s < pl.nstages; s++) {
        int is_last = (s + 1 == pl.nstages);
        int capture_for_pipe = !is_last;

        if (s > 0) {
            if (vfs_write_file(PIPE_PATH, pipe_data, pipe_len) != 0) {
                console_write("shell: pipe buffer write failed\n");
                shell_set_stdin_path(0);
                return;
            }
            shell_set_stdin_path(PIPE_PATH);
            if (pl.stages[s].argc == 1 && pl.stages[s].argc < SHELL_ARGV_MAX - 1) {
                pl.stages[s].argv[pl.stages[s].argc++] = (char *)"-";
                pl.stages[s].argv[pl.stages[s].argc] = 0;
            }
        }

        size_t n = 0;
        int rc = run_stage(&pl.stages[s], capture_for_pipe, cap, sizeof(cap), &n);
        shell_set_stdin_path(0);
        if (rc == -999)
            return;

        if (capture_for_pipe) {
            if (n >= sizeof(pipe_data))
                n = sizeof(pipe_data) - 1;
            memcpy(pipe_data, cap, n);
            pipe_data[n] = '\0';
            pipe_len = n;
        }
    }
}
