#include "console.h"
#include "vfs.h"
#include "elf.h"
#include "util.h"
#include "keyboard.h"
#include "shell.h"

static void uputs(const char *s) {
    console_write(s);
}

/*
 * Multi-line buffer editor:
 *  - Loads existing file when present
 *  - Line entry until '.' alone (append mode) OR command mode lines starting with ':'
 *  Commands (as a whole line):
 *    :w   save    :q  quit without save    :wq save+quit
 *    :p   print buffer    /pat  search (print matching lines)
 *  Ctrl+C discarded; backspace works on current line.
 */
int uedit_main(int argc, char **argv) {
    if (argc < 2) {
        uputs("usage: edit <path>\n");
        return 1;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)) != 0)
        return 1;

    char content[8192];
    size_t len = 0;
    size_t existing = 0;
    if (vfs_read_file(abs, content, sizeof(content) - 1, &existing) == 0) {
        len = existing;
        content[len] = '\0';
        console_printf("edit: loaded %s (%lu bytes)\n", abs, (unsigned long)len);
    } else {
        content[0] = '\0';
        console_printf("edit: new file %s\n", abs);
    }
    uputs("lines until '.'  |  :w :q :wq :p  |  /pat search\n");

    int dirty = 0;
    char line[256];
    uint32_t li = 0;

    for (;;) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') {
            console_putc('\n');
            line[li] = '\0';

            if (li == 1 && line[0] == '.') {
                if (dirty) {
                    if (vfs_write_file(abs, content, len) != 0) {
                        uputs("edit: write failed\n");
                        return 1;
                    }
                    dirty = 0;
                    uputs("saved.\n");
                }
                return 0;
            }

            if (line[0] == ':') {
                if (!strcmp(line, ":w") || !strcmp(line, ":wq")) {
                    if (vfs_write_file(abs, content, len) != 0) {
                        uputs("edit: write failed\n");
                        return 1;
                    }
                    dirty = 0;
                    uputs("saved.\n");
                    if (!strcmp(line, ":wq"))
                        return 0;
                } else if (!strcmp(line, ":q")) {
                    if (dirty)
                        uputs("edit: unsaved changes — :wq to save or '.' after edit\n");
                    else
                        return 0;
                } else if (!strcmp(line, ":p")) {
                    size_t start = 0;
                    int lineno = 1;
                    for (size_t i = 0; i <= len; i++) {
                        if (i == len || content[i] == '\n') {
                            console_printf("%4d ", lineno++);
                            for (size_t j = start; j < i; j++)
                                console_putc(content[j]);
                            console_putc('\n');
                            start = i + 1;
                        }
                    }
                } else {
                    uputs("edit: unknown :command\n");
                }
                li = 0;
                continue;
            }

            if (line[0] == '/' && li > 1) {
                const char *pat = line + 1;
                size_t plen = strlen(pat);
                size_t start = 0;
                int lineno = 1;
                int hits = 0;
                for (size_t i = 0; i <= len; i++) {
                    if (i == len || content[i] == '\n') {
                        size_t l = i - start;
                        int match = 0;
                        if (plen == 0)
                            match = 1;
                        else if (l >= plen) {
                            for (size_t j = 0; j + plen <= l; j++) {
                                if (!memcmp(content + start + j, pat, plen)) {
                                    match = 1;
                                    break;
                                }
                            }
                        }
                        if (match) {
                            console_printf("%4d ", lineno);
                            for (size_t j = start; j < i; j++)
                                console_putc(content[j]);
                            console_putc('\n');
                            hits++;
                        }
                        lineno++;
                        start = i + 1;
                    }
                }
                console_printf("edit: %d match(es)\n", hits);
                li = 0;
                continue;
            }

            if (len + li + 1 < sizeof(content)) {
                memcpy(content + len, line, li);
                len += li;
                content[len++] = '\n';
                content[len] = '\0';
                dirty = 1;
            } else {
                uputs("edit: buffer full (8 KiB)\n");
            }
            li = 0;
            continue;
        }
        if ((c == '\b' || c == 127) && li > 0) {
            li--;
            console_backspace();
            continue;
        }
        if (c >= 32 && c < 127 && li + 1 < sizeof(line)) {
            line[li++] = c;
            console_putc(c);
        }
    }
}

int ush_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    uputs("Peak userspace sh — type commands (exit to leave)\n");
    char line[256];
    for (;;) {
        uputs("ush> ");
        uint32_t n = 0;
        for (;;) {
            char c = keyboard_getchar();
            if (c == '\n' || c == '\r') {
                console_putc('\n');
                line[n] = '\0';
                break;
            }
            if ((c == '\b' || c == 127) && n > 0) {
                n--;
                console_backspace();
                continue;
            }
            if (c >= 32 && c < 127 && n + 1 < sizeof(line)) {
                line[n++] = c;
                console_putc(c);
            }
        }
        if (!line[0])
            continue;
        if (!strcmp(line, "exit"))
            return 0;
        shell_execute(line);
    }
}
