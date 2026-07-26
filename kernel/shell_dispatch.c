#include "shell.h"
#include "shell_split.h"
#include "console.h"
#include "elf.h"
#include "util.h"
#include "vfs.h"
#include "peak_errno.h"
#include "peak_io.h"

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
            shell_perror_path("shell: glob", dir);
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
    if (shell_resolve_path(path, abs, sizeof(abs)) != 0) {
        shell_perror_path("shell: redirect", path);
        return -1;
    }
    if (append) {
        char old[SHELL_CAPTURE_MAX];
        size_t old_n = 0;
        if (vfs_read_file(abs, old, sizeof(old) - 1, &old_n) != 0)
            old_n = 0;
        if (old_n + len >= sizeof(old)) {
            console_write("shell: redirect: append buffer full (8 KiB)\n");
            return -1;
        }
        memcpy(old + old_n, data, len);
        old_n += len;
        old[old_n] = '\0';
        int wrc = vfs_write_file(abs, old, old_n);
        if (wrc != 0) {
            console_write("shell: redirect: ");
            console_write(peak_strerror(wrc));
            console_write("\n");
            return -1;
        }
        return 0;
    }
    int wrc = vfs_write_file(abs, data, len);
    if (wrc != 0) {
        console_write("shell: redirect: ");
        console_write(peak_strerror(wrc));
        console_write("\n");
        return -1;
    }
    return 0;
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
            shell_perror_path("shell: input redirect", st->redir_in.path);
            return 1;
        }
        shell_set_stdin_path(abs);
        if (argc == 1 && argc < SHELL_ARGV_MAX - 1) {
            argv_local[argc++] = (char *)"-";
            argv_local[argc] = 0;
        }
    }

    int want_cap = capture_out || st->redir_out.kind != SHELL_REDIR_NONE ||
                   st->redir_err.kind != SHELL_REDIR_NONE;
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
            if (shell_last_path_errno() == PEAK_OK)
                console_write("shell: redirect write failed\n");
            return 1;
        }
    }
    /* 2>/2>> lite: same console capture as stdout (no separate stderr stream). */
    if (st->redir_err.kind != SHELL_REDIR_NONE && st->redir_err.path) {
        int ap = (st->redir_err.kind == SHELL_REDIR_ERR_APPEND);
        if (write_redir(st->redir_err.path, cap_buf, n, ap) != 0) {
            if (shell_last_path_errno() == PEAK_OK)
                console_write("shell: stderr redirect write failed\n");
            return 1;
        }
    }

    return rc;
}

/*
 * Mutates cmd in place. Supports !! and !n (1-based history index).
 */
static void expand_history(char *cmd, char *scratch, size_t scratch_cap) {
    if (!cmd || cmd[0] != '!')
        return;
    const char *repl = NULL;
    const char *suffix = NULL;
    if (cmd[1] == '!') {
        repl = shell_history_last();
        suffix = cmd + 2;
    } else if (cmd[1] >= '0' && cmd[1] <= '9') {
        int n = 0;
        const char *p = cmd + 1;
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        repl = shell_history_get(n);
        suffix = p;
    } else {
        return;
    }
    if (!repl || !repl[0]) {
        console_write("shell: history expansion failed\n");
        cmd[0] = '\0';
        return;
    }
    while (*suffix == ' ')
        suffix++;
    size_t ln = strlen(repl);
    size_t sn = strlen(suffix);
    if (ln + (sn ? sn + 1 : 0) + 1 > scratch_cap) {
        console_write("shell: history expansion too long\n");
        cmd[0] = '\0';
        return;
    }
    memcpy(scratch, repl, ln);
    if (sn) {
        scratch[ln++] = ' ';
        memcpy(scratch + ln, suffix, sn);
        ln += sn;
    }
    scratch[ln] = '\0';
    memcpy(cmd, scratch, ln + 1);
    console_write(cmd);
    console_putc('\n');
}

/* Expand leading alias: first token only. */
static void expand_alias(char *cmd, char *scratch, size_t scratch_cap) {
    if (!cmd || !*cmd)
        return;
    char name[32];
    size_t ni = 0;
    const char *p = cmd;
    while (*p && *p != ' ' && ni + 1 < sizeof(name))
        name[ni++] = *p++;
    name[ni] = '\0';
    if (!ni)
        return;
    const char *val = shell_alias_lookup(name);
    if (!val)
        return;
    while (*p == ' ')
        p++;
    size_t vn = strlen(val);
    size_t sn = strlen(p);
    if (vn + (sn ? sn + 1 : 0) + 1 > scratch_cap)
        return;
    memcpy(scratch, val, vn);
    if (sn) {
        scratch[vn++] = ' ';
        memcpy(scratch + vn, p, sn);
        vn += sn;
    }
    scratch[vn] = '\0';
    memcpy(cmd, scratch, vn + 1);
}

void shell_execute(char *cmd) {
    if (!cmd)
        return;
    while (*cmd == ' ')
        cmd++;
    if (!*cmd)
        return;

    char expand_buf[256];
    expand_history(cmd, expand_buf, sizeof(expand_buf));
    if (!*cmd) {
        shell_set_last_status(1);
        return;
    }
    expand_alias(cmd, expand_buf, sizeof(expand_buf));
    /* Record after !! / !n / alias so the ring stores the real command. */
    shell_history_add(cmd);

    /* export NAME=val as shorthand — rest is one argv, no re-split */
    if (!strncmp(cmd, "export ", 7)) {
        char *rest = cmd + 7;
        while (*rest == ' ')
            rest++;
        if (!*rest) {
            shell_set_last_status(0);
            return;
        }
        char *argv[3] = { "export", rest, NULL };
        char path[] = "/bin/export";
        shell_set_last_status(proc_exec(path, 2, argv));
        return;
    }

    /* unset NAME — remove shell env var */
    if (!strncmp(cmd, "unset ", 6)) {
        char *name = cmd + 6;
        while (*name == ' ')
            name++;
        if (!*name || strchr(name, ' ')) {
            console_write("unset: usage: unset NAME\n");
            shell_set_last_status(1);
            return;
        }
        shell_set_last_status(shell_env_unset(name) == 0 ? 0 : 1);
        return;
    }

    /* read NAME — read one line from stdin into env var */
    if (!strncmp(cmd, "read ", 5)) {
        char *name = cmd + 5;
        while (*name == ' ')
            name++;
        if (!*name || strchr(name, ' ')) {
            console_write("read: usage: read NAME\n");
            shell_set_last_status(1);
            return;
        }
        char buf[96];
        buf[0] = '\0';
        const char *sin = shell_stdin_path();
        if (sin) {
            size_t n = 0;
            if (vfs_read_file(sin, buf, sizeof(buf) - 1, &n) == 0) {
                buf[n] = '\0';
                /* trim trailing newline */
                while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
                    buf[--n] = '\0';
            }
        }
        shell_env_set(name, buf);
        shell_set_last_status(0);
        return;
    }

    (void)vfs_mkdir("/tmp");

    /* && / || list above pipelines (outside quotes). */
    char *seg = cmd;
    int final_rc = 0;
    int have_rc = 0;
    for (;;) {
        char *p = seg;
        int in_sq = 0, in_dq = 0;
        char *op_at = 0;
        int op_or = 0; /* 0=&&, 1=|| */
        while (*p) {
            if (!in_dq && *p == '\'') {
                in_sq = !in_sq;
                p++;
                continue;
            }
            if (!in_sq && *p == '"') {
                in_dq = !in_dq;
                p++;
                continue;
            }
            if (!in_sq && !in_dq) {
                if (p[0] == '&' && p[1] == '&') {
                    op_at = p;
                    op_or = 0;
                    break;
                }
                if (p[0] == '|' && p[1] == '|') {
                    op_at = p;
                    op_or = 1;
                    break;
                }
            }
            p++;
        }
        char *next = 0;
        if (op_at) {
            *op_at = '\0';
            next = op_at + 2;
            while (*next == ' ')
                next++;
        }

        while (*seg == ' ')
            seg++;
        if (*seg) {
            struct shell_pipeline pl;
            if (shell_parse_pipeline(seg, &pl) != 0) {
                console_write("shell: parse error (pipes/redirects; max ");
                console_printf("%d stages, %d args/stage)\n", SHELL_PIPE_MAX, SHELL_ARGV_MAX - 1);
                shell_set_last_status(1);
                return;
            }

            char cap[SHELL_CAPTURE_MAX];
            char pipe_data[SHELL_CAPTURE_MAX];
            size_t pipe_len = 0;
            int seg_rc = 0;

            for (int s = 0; s < pl.nstages; s++) {
                int is_last = (s + 1 == pl.nstages);
                int capture_for_pipe = !is_last;

                if (s > 0) {
                    int prc = vfs_write_file(PIPE_PATH, pipe_data, pipe_len);
                    if (prc != 0) {
                        console_write("shell: pipe buffer write failed: ");
                        console_write(peak_strerror(prc));
                        console_write("\n");
                        shell_set_stdin_path(0);
                        shell_set_last_status(1);
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
                if (rc == -999) {
                    shell_set_last_status(127);
                    return;
                }
                seg_rc = rc;

                if (capture_for_pipe) {
                    if (n >= sizeof(pipe_data) - 2)
                        console_printf("shell: pipe output truncated at %u bytes (%u KiB cap)\n",
                                       (unsigned)sizeof(pipe_data), PEAK_IO_CAP_KIB);
                    if (n >= sizeof(pipe_data))
                        n = sizeof(pipe_data) - 1;
                    memcpy(pipe_data, cap, n);
                    pipe_data[n] = '\0';
                    pipe_len = n;
                }
            }
            final_rc = seg_rc;
            have_rc = 1;
        }

        if (!op_at)
            break;
        /* Short-circuit: && skips rest on failure; || skips rest on success. */
        if ((!op_or && final_rc != 0) || (op_or && have_rc && final_rc == 0))
            break;
        seg = next;
    }
    shell_set_last_status(final_rc);
}
