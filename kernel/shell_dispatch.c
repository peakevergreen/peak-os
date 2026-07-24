#include "shell.h"
#include "shell_split.h"
#include "console.h"
#include "elf.h"
#include "util.h"
#include "vfs.h"

static const char PIPE_PATH[] = "/tmp/.peak_pipe";

static int glob_match(const char *pat, const char *name) {
    while (*pat && *name) {
        if (*pat == '*') {
            pat++;
            if (!*pat)
                return 1;
            while (*name) {
                if (glob_match(pat, name))
                    return 1;
                name++;
            }
            return 0;
        }
        if (*pat == '?' || *pat == *name) {
            pat++;
            name++;
            continue;
        }
        return 0;
    }
    while (*pat == '*')
        pat++;
    return !*pat && !*name;
}

static int arg_has_glob(const char *s) {
    for (; s && *s; s++)
        if (*s == '*' || *s == '?')
            return 1;
    return 0;
}

/* Expand basename globs in argv; names stored in name_store. Returns new argc. */
static int expand_globs(char **argv, int argc, char **out, char name_store[][VFS_PATH_MAX],
                        int store_cap) {
    int o = 0;
    int store_i = 0;
    for (int i = 0; i < argc; i++) {
        if (!arg_has_glob(argv[i])) {
            if (o >= SHELL_ARGV_MAX - 1)
                return -1;
            out[o++] = argv[i];
            continue;
        }
        const char *pat = argv[i];
        char dir[VFS_PATH_MAX];
        const char *base = pat;
        const char *slash = 0;
        for (const char *p = pat; *p; p++)
            if (*p == '/')
                slash = p;
        if (slash) {
            size_t dlen = (size_t)(slash - pat);
            if (dlen == 0) {
                dir[0] = '/';
                dir[1] = '\0';
            } else if (dlen + 1 > sizeof(dir)) {
                out[o++] = argv[i];
                continue;
            } else {
                memcpy(dir, pat, dlen);
                dir[dlen] = '\0';
            }
            base = slash + 1;
        } else {
            const char *cwd = shell_getcwd();
            size_t j = 0;
            for (; cwd[j] && j + 1 < sizeof(dir); j++)
                dir[j] = cwd[j];
            dir[j] = '\0';
        }
        char absdir[VFS_PATH_MAX];
        if (shell_resolve_path(dir, absdir, sizeof(absdir)) != 0) {
            if (o >= SHELL_ARGV_MAX - 1)
                return -1;
            out[o++] = argv[i];
            continue;
        }
        struct vfs_dirent ents[64];
        int n = vfs_readdir(absdir, ents, 64);
        int matched = 0;
        if (n > 0) {
            for (int e = 0; e < n; e++) {
                if (!glob_match(base, ents[e].name))
                    continue;
                if (store_i >= store_cap || o >= SHELL_ARGV_MAX - 1)
                    return -1;
                if (slash) {
                    size_t dl = (size_t)(slash - pat + 1);
                    if (dl + strlen(ents[e].name) + 1 > VFS_PATH_MAX)
                        continue;
                    memcpy(name_store[store_i], pat, dl);
                    size_t k = 0;
                    for (; ents[e].name[k]; k++)
                        name_store[store_i][dl + k] = ents[e].name[k];
                    name_store[store_i][dl + k] = '\0';
                } else {
                    size_t k = 0;
                    for (; ents[e].name[k] && k + 1 < VFS_PATH_MAX; k++)
                        name_store[store_i][k] = ents[e].name[k];
                    name_store[store_i][k] = '\0';
                }
                out[o++] = name_store[store_i++];
                matched++;
            }
        }
        if (!matched) {
            if (o >= SHELL_ARGV_MAX - 1)
                return -1;
            out[o++] = argv[i];
        }
    }
    out[o] = 0;
    return o;
}

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
    char glob_store[SHELL_ARGV_MAX][VFS_PATH_MAX];
    int argc = expand_globs(st->argv, st->argc, argv_local, glob_store, SHELL_ARGV_MAX);
    if (argc < 0) {
        console_write("shell: glob expansion overflow\n");
        return 1;
    }

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
