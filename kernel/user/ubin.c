#include "console.h"
#include "vfs.h"
#include "elf.h"
#include "util.h"
#include "keyboard.h"
#include "shell.h"

static void uputs(const char *s) {
    console_write(s);
}

int uedit_main(int argc, char **argv) {
    if (argc < 2) {
        uputs("usage: edit <path>\n");
        return 1;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)) != 0)
        return 1;
    uputs("edit: enter lines, single '.' on a line to save\n");
    char content[8192];
    size_t len = 0;
    char line[256];
    uint32_t li = 0;
    for (;;) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') {
            console_putc('\n');
            line[li] = '\0';
            if (li == 1 && line[0] == '.')
                break;
            if (len + li + 1 < sizeof(content)) {
                memcpy(content + len, line, li);
                len += li;
                content[len++] = '\n';
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
    content[len] = '\0';
    if (vfs_write_file(abs, content, len) != 0) {
        uputs("edit: write failed\n");
        return 1;
    }
    uputs("saved.\n");
    return 0;
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

