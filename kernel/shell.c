#include "shell.h"
#include "console.h"
#include "fb.h"
#include "gui.h"
#include "keyboard.h"
#include "pmm.h"
#include "timer.h"
#include "util.h"

static enum os_mode mode = MODE_CLI;
static char line[256];
static uint32_t line_len;
static int gui_terminal_active;

enum os_mode shell_mode(void) {
    return mode;
}

void shell_set_mode(enum os_mode m) {
    mode = m;
}

static void print_prompt(void) {
    console_write("peak> ");
}

void shell_redraw_prompt(void) {
    print_prompt();
}

void shell_execute(const char *cmd) {
    while (*cmd == ' ')
        cmd++;
    if (!*cmd)
        return;

    if (!strcmp(cmd, "help")) {
        console_write("Commands:\n");
        console_write("  help     - this list\n");
        console_write("  clear    - clear screen\n");
        console_write("  echo     - print arguments\n");
        console_write("  uname    - system identity\n");
        console_write("  uptime   - seconds since boot\n");
        console_write("  mem      - physical memory stats\n");
        console_write("  gui      - enter desktop\n");
        console_write("  reboot   - restart machine\n");
    } else if (!strcmp(cmd, "clear")) {
        console_clear();
    } else if (!strncmp(cmd, "echo ", 5)) {
        console_write(cmd + 5);
        console_putc('\n');
    } else if (!strcmp(cmd, "echo")) {
        console_putc('\n');
    } else if (!strcmp(cmd, "uname")) {
        console_write("Peak OS 0.1.0 x86_64 (from-scratch MVP)\n");
    } else if (!strcmp(cmd, "uptime")) {
        console_printf("%lu seconds\n", timer_uptime_secs());
    } else if (!strcmp(cmd, "mem")) {
        uint64_t free_p = pmm_free_pages();
        uint64_t total_p = pmm_total_pages();
        console_printf("pages: %lu free / %lu total (%lu KiB free)\n",
                       free_p, total_p, free_p * 4);
    } else if (!strcmp(cmd, "gui")) {
        if (shell_mode() == MODE_GUI) {
            console_write("Already in desktop. Esc returns to CLI.\n");
            return;
        }
        console_write("Entering desktop... (Esc returns to CLI)\n");
        shell_set_mode(MODE_GUI);
        desktop_run();
        shell_set_mode(MODE_CLI);
        console_init();
        console_write("Back in Peak CLI. Type 'help'.\n");
    } else if (!strcmp(cmd, "reboot")) {
        console_write("Rebooting...\n");
        reboot();
    } else {
        console_write("Unknown command. Try 'help'.\n");
    }
}

void shell_init(void) {
    line_len = 0;
    gui_terminal_active = 0;
    console_write("\n");
    console_write("  Peak OS 0.1.0 — from-scratch kernel MVP\n");
    console_write("  Type 'help' for commands, 'gui' for desktop.\n\n");
    print_prompt();
}

static void handle_char(char c) {
    if (c == '\n' || c == '\r') {
        console_putc('\n');
        line[line_len] = '\0';
        shell_execute(line);
        line_len = 0;
        print_prompt();
        return;
    }
    if (c == '\b' || c == 127) {
        if (line_len > 0) {
            line_len--;
            console_backspace();
        }
        return;
    }
    if (c >= 32 && c < 127 && line_len + 1 < sizeof(line)) {
        line[line_len++] = c;
        console_putc(c);
    }
}

void shell_feed_char(char c) {
    handle_char(c);
}

void shell_run_once(void) {
    char c = keyboard_try_getchar();
    if (!c)
        return;
    handle_char(c);
}
