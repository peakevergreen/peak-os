#include "shell.h"
#include "shell_split.h"
#include "console.h"
#include "fb.h"
#include "gui.h"
#include "keyboard.h"
#include "util.h"
#include "vfs.h"
#include "elf.h"
#include "agent.h"
#include "theme.h"
#include "sched.h"

static enum os_mode mode = MODE_CLI;
static char line[256];
static uint32_t line_len;
static uint32_t caret;
static int sel_anchor; /* -1 = no selection; else selection is [min(anchor,caret), max) */
static char clipboard[256];
static char cwd[VFS_PATH_MAX] = "/home/dev/workspace";
static char prompt_buf[VFS_PATH_MAX + 16];
static uint32_t edit_paint_len; /* last MODE_CLI painted prompt+line width */

#define ENV_MAX 32
#define ENV_KEY 32
#define ENV_VAL 96

static struct {
    char key[ENV_KEY];
    char val[ENV_VAL];
    int used;
} env[ENV_MAX];

enum os_mode shell_mode(void) {
    return mode;
}

void shell_set_mode(enum os_mode m) {
    mode = m;
}

const char *shell_getcwd(void) {
    return cwd;
}

int shell_resolve_path(const char *in, char *out, size_t out_len) {
    if (!in || !out || out_len < 2)
        return -1;
    char tmp[VFS_PATH_MAX];
    if (in[0] == '/') {
        size_t n = 0;
        while (in[n] && n + 1 < sizeof(tmp)) {
            tmp[n] = in[n];
            n++;
        }
        tmp[n] = '\0';
    } else {
        /* cwd + / + in */
        size_t o = 0;
        for (; cwd[o] && o + 1 < sizeof(tmp); o++)
            tmp[o] = cwd[o];
        if (!(o == 1 && tmp[0] == '/')) {
            if (o + 1 >= sizeof(tmp))
                return -1;
            tmp[o++] = '/';
        }
        for (size_t i = 0; in[i] && o + 1 < sizeof(tmp); i++)
            tmp[o++] = in[i];
        tmp[o] = '\0';
    }
    return vfs_normalize(tmp, out, out_len);
}

int shell_chdir(const char *path) {
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)) != 0)
        return -1;
    if (!vfs_is_dir(abs))
        return -1;
    size_t i = 0;
    for (; abs[i] && i + 1 < sizeof(cwd); i++)
        cwd[i] = abs[i];
    cwd[i] = '\0';
    return 0;
}

int shell_env_set(const char *name, const char *val) {
    for (int i = 0; i < ENV_MAX; i++) {
        if (env[i].used && !strcmp(env[i].key, name)) {
            size_t j = 0;
            for (; val[j] && j + 1 < ENV_VAL; j++)
                env[i].val[j] = val[j];
            env[i].val[j] = '\0';
            return 0;
        }
    }
    for (int i = 0; i < ENV_MAX; i++) {
        if (!env[i].used) {
            size_t j = 0;
            for (; name[j] && j + 1 < ENV_KEY; j++)
                env[i].key[j] = name[j];
            env[i].key[j] = '\0';
            j = 0;
            for (; val[j] && j + 1 < ENV_VAL; j++)
                env[i].val[j] = val[j];
            env[i].val[j] = '\0';
            env[i].used = 1;
            return 0;
        }
    }
    return -1;
}

const char *shell_env_get(const char *name) {
    for (int i = 0; i < ENV_MAX; i++)
        if (env[i].used && !strcmp(env[i].key, name))
            return env[i].val;
    return NULL;
}

void shell_env_list(void) {
    for (int i = 0; i < ENV_MAX; i++) {
        if (!env[i].used)
            continue;
        console_write(env[i].key);
        console_putc('=');
        console_write(env[i].val);
        console_putc('\n');
    }
}

static void build_prompt(void) {
    snprintf(prompt_buf, sizeof(prompt_buf), "peak:%s> ", cwd);
}

static void print_prompt(void) {
    build_prompt();
    caret = 0;
    line_len = 0;
    sel_anchor = -1;
    line[0] = '\0';
    edit_paint_len = (uint32_t)strlen(prompt_buf);
    if (mode == MODE_GUI)
        gui_term_set_edit(prompt_buf, line, caret, -1, -1);
    else
        console_write(prompt_buf);
}

static void refresh_edit_display(void) {
    build_prompt();
    if (mode == MODE_GUI) {
        int a = -1, b = -1;
        if (sel_anchor >= 0) {
            a = (int)(sel_anchor < (int)caret ? (uint32_t)sel_anchor : caret);
            b = (int)(sel_anchor < (int)caret ? caret : (uint32_t)sel_anchor);
            if (b > a)
                b--; /* inclusive end for paint */
            else
                a = b = -1;
        }
        gui_term_set_edit(prompt_buf, line, caret, a, b);
        return;
    }
    /* CLI: rewrite line; pad spaces to clear leftovers when the line shrinks */
    line[line_len] = '\0';
    uint32_t cur = (uint32_t)strlen(prompt_buf) + line_len;
    console_putc('\r');
    console_write(prompt_buf);
    console_write(line);
    if (edit_paint_len > cur) {
        for (uint32_t i = 0; i < edit_paint_len - cur; i++)
            console_putc(' ');
        console_putc('\r');
        console_write(prompt_buf);
        console_write(line);
    }
    edit_paint_len = cur;
}

static int sel_lo(void) {
    if (sel_anchor < 0)
        return -1;
    return sel_anchor < (int)caret ? sel_anchor : (int)caret;
}

static int sel_hi(void) {
    if (sel_anchor < 0)
        return -1;
    return sel_anchor < (int)caret ? (int)caret : sel_anchor;
}

static void clear_sel(void) { sel_anchor = -1; }

static void delete_selection(void) {
    int a = sel_lo(), b = sel_hi();
    if (a < 0 || b <= a)
        return;
    uint32_t n = line_len - (uint32_t)b;
    memmove(line + a, line + b, n + 1);
    line_len -= (uint32_t)(b - a);
    caret = (uint32_t)a;
    clear_sel();
}

static void copy_selection(void) {
    int a = sel_lo(), b = sel_hi();
    if (a < 0 || b <= a) {
        /* nothing selected → copy whole line */
        size_t n = line_len;
        if (n >= sizeof(clipboard))
            n = sizeof(clipboard) - 1;
        memcpy(clipboard, line, n);
        clipboard[n] = '\0';
        return;
    }
    size_t n = (size_t)(b - a);
    if (n >= sizeof(clipboard))
        n = sizeof(clipboard) - 1;
    memcpy(clipboard, line + a, n);
    clipboard[n] = '\0';
}

static void paste_clipboard(void) {
    size_t n = strlen(clipboard);
    if (!n)
        return;
    delete_selection();
    if (line_len + n >= sizeof(line))
        n = sizeof(line) - 1 - line_len;
    memmove(line + caret + n, line + caret, line_len - caret + 1);
    memcpy(line + caret, clipboard, n);
    line_len += (uint32_t)n;
    caret += (uint32_t)n;
}

void shell_redraw_prompt(void) {
    print_prompt();
}

struct help_entry {
    const char *cmd;
    const char *cat;
    const char *blurb;
};

static const struct help_entry help_table[] = {
    { "pwd", "nav", "print working directory" },
    { "cd", "nav", "change directory" },
    { "ls", "nav", "list directory (-l long)" },
    { "tree", "nav", "print directory tree" },
    { "find", "nav", "find -name <name>" },
    { "mkdir", "file", "create directory" },
    { "touch", "file", "create empty file" },
    { "rm", "file", "remove file/dir (-rf)" },
    { "cp", "file", "copy (-r recursive)" },
    { "mv", "file", "rename/move" },
    { "ln", "file", "hard link" },
    { "stat", "file", "file metadata" },
    { "du", "file", "disk usage" },
    { "df", "file", "filesystem stats" },
    { "truncate", "file", "set file size" },
    { "cat", "text", "print file" },
    { "head", "text", "first N lines" },
    { "tail", "text", "last N lines" },
    { "wc", "text", "line/word/byte count" },
    { "grep", "text", "substring search" },
    { "hexdump", "text", "hex dump" },
    { "strings", "text", "printable runs" },
    { "echo", "text", "print arguments" },
    { "edit", "text", "line editor" },
    { "clear", "sys", "clear screen" },
    { "date", "sys", "uptime clock" },
    { "free", "sys", "memory pages" },
    { "top", "sys", "live system monitor" },
    { "sysmon", "sys", "alias for top" },
    { "ps", "sys", "list kernel tasks/threads" },
    { "env", "sys", "list/set env" },
    { "export", "sys", "set NAME=val" },
    { "which", "sys", "resolve /bin path" },
    { "seq", "sys", "print number sequence" },
    { "sleep", "sys", "sleep seconds" },
    { "theme", "sys", "list/set/next theme" },
    { "wallpaper", "sys", "set desktop wallpaper (PPM)" },
    { "scale", "sys", "UI scale 1..4" },
    { "uname", "sys", "system name" },
    { "true", "sys", "exit 0" },
    { "false", "sys", "exit 1" },
    { "sh", "sys", "nested shell loop" },
    { "reboot", "sys", "reboot guest" },
    { "help", "sys", "this help" },
    { "man", "sys", "command help" },
    { "peak", "meta", "Peak meta info" },
    { "ask", "meta", "peak-agent prompt (quotes ok)" },
    { "audit", "meta", "show audit log" },
    { "memory", "meta", "project memory" },
    { "policy", "meta", "agent policy" },
    { "privacy", "meta", "persist / net-allow / kill-switch" },
    { "gui", "meta", "enter desktop (Ctrl+Alt+Esc leaves)" },
    { "ctr", "net", "stage Dockerfile subset; serve static HTTP (not OCI)" },
    { "ctrd", "net", "ping Peak ctr staging helper" },
    { "ifconfig", "net", "show e1000 IPv4 config" },
    { "ping", "net", "DNS + TCP reachability probe" },
    { "wget", "net", "HTTP GET via in-guest TCP stack" },
    { "js", "sys", "Peak JS: js -e 'code' | js file.js" },
    { NULL, NULL, NULL },
};

void shell_help_topics(void) {
    console_write("Peak CLI — categories:\n");
    console_write("  nav   pwd cd ls tree find\n");
    console_write("  file  mkdir touch rm cp mv ln stat du df truncate\n");
    console_write("  text  cat head tail wc grep hexdump strings echo edit clear\n");
    console_write("  sys   date free top sysmon ps env which seq sleep theme wallpaper scale\n");
    console_write("        uname true false sh reboot help man js\n");
    console_write("  meta  peak ask audit memory policy privacy gui\n");
    console_write("  net   ctr ctrd ifconfig ping wget\n");
    console_write("Try: man <cmd>   theme list   gui   ask \"...\"   js -e '1+1'\n");
}

void shell_help_cmd(const char *cmd) {
    for (int i = 0; help_table[i].cmd; i++) {
        if (!strcmp(help_table[i].cmd, cmd)) {
            console_printf("%s (%s): %s\n", help_table[i].cmd, help_table[i].cat,
                           help_table[i].blurb);
            return;
        }
    }
    console_write("unknown command — try help\n");
}

void shell_execute(const char *cmd_in) {
    char cmdbuf[256];
    size_t n = 0;
    while (cmd_in[n] && n + 1 < sizeof(cmdbuf)) {
        cmdbuf[n] = cmd_in[n];
        n++;
    }
    cmdbuf[n] = '\0';
    char *cmd = cmdbuf;
    while (*cmd == ' ')
        cmd++;
    if (!*cmd)
        return;

    /* export NAME=val as shorthand */
    if (!strncmp(cmd, "export ", 7)) {
        char *rest = cmd + 7;
        while (*rest == ' ')
            rest++;
        char *argv[4] = { "export", rest, NULL };
        char path[64] = "/bin/export";
        proc_exec(path, 2, argv);
        return;
    }

    char *argv[16];
    int argc = shell_split_args(cmd, argv, 16);
    if (argc < 1)
        return;

    /* builtins that are shell-only (no /bin needed) handled via /bin dispatch */
    char path[64] = "/bin/";
    size_t i = 5;
    for (size_t j = 0; argv[0][j] && i + 1 < sizeof(path); j++)
        path[i++] = argv[0][j];
    path[i] = '\0';

    int rc = proc_exec(path, argc, argv);
    if (rc == -999) {
        console_write("Unknown command. Try 'help'.\n");
    }
}

void shell_init(void) {
    line_len = 0;
    caret = 0;
    sel_anchor = -1;
    clipboard[0] = '\0';
    memset(env, 0, sizeof(env));
    shell_env_set("HOME", "/home/dev");
    shell_env_set("PATH", "/bin");
    shell_env_set("USER", "peak");
    console_write("\n");
    console_write("  PeakOS 0.2 — arrows move  Ctrl+A select-all  Ctrl+C/X/V copy/cut/paste\n");
    console_write("  Workspace: /home/dev/workspace  |  ask \"...\"  |  gui  |  theme\n\n");
    print_prompt();
}

static void handle_key(int key) {
    if (!key)
        return;

    /* Ctrl chords */
    if (key == 1) { /* Ctrl+A — select all */
        if (line_len) {
            sel_anchor = 0;
            caret = line_len;
        }
        refresh_edit_display();
        return;
    }
    if (key == 3) { /* Ctrl+C — copy */
        copy_selection();
        return;
    }
    if (key == 24) { /* Ctrl+X — cut */
        copy_selection();
        delete_selection();
        refresh_edit_display();
        return;
    }
    if (key == 22) { /* Ctrl+V — paste */
        paste_clipboard();
        refresh_edit_display();
        return;
    }

    if (key == '\n' || key == '\r') {
        clear_sel();
        console_putc('\n');
        line[line_len] = '\0';
        shell_execute(line);
        line_len = 0;
        caret = 0;
        print_prompt();
        return;
    }

    if (key == KEY_LEFT) {
        if (keyboard_shift_down()) {
            if (sel_anchor < 0)
                sel_anchor = (int)caret;
        } else {
            clear_sel();
        }
        if (caret > 0)
            caret--;
        refresh_edit_display();
        return;
    }
    if (key == KEY_RIGHT) {
        if (keyboard_shift_down()) {
            if (sel_anchor < 0)
                sel_anchor = (int)caret;
        } else {
            clear_sel();
        }
        if (caret < line_len)
            caret++;
        refresh_edit_display();
        return;
    }
    if (key == KEY_HOME) {
        if (keyboard_shift_down()) {
            if (sel_anchor < 0)
                sel_anchor = (int)caret;
        } else {
            clear_sel();
        }
        caret = 0;
        refresh_edit_display();
        return;
    }
    if (key == KEY_END) {
        if (keyboard_shift_down()) {
            if (sel_anchor < 0)
                sel_anchor = (int)caret;
        } else {
            clear_sel();
        }
        caret = line_len;
        refresh_edit_display();
        return;
    }
    if (key == KEY_DELETE) {
        if (sel_anchor >= 0 && sel_hi() > sel_lo()) {
            delete_selection();
        } else if (caret < line_len) {
            memmove(line + caret, line + caret + 1, line_len - caret);
            line_len--;
        }
        refresh_edit_display();
        return;
    }

    if (key == '\b' || key == 127) {
        if (sel_anchor >= 0 && sel_hi() > sel_lo()) {
            delete_selection();
        } else if (caret > 0) {
            memmove(line + caret - 1, line + caret, line_len - caret + 1);
            caret--;
            line_len--;
            clear_sel();
        }
        refresh_edit_display();
        return;
    }

    if (key >= 32 && key < 127) {
        if (sel_anchor >= 0 && sel_hi() > sel_lo())
            delete_selection();
        if (line_len + 1 < sizeof(line)) {
            memmove(line + caret + 1, line + caret, line_len - caret + 1);
            line[caret] = (char)key;
            caret++;
            line_len++;
            clear_sel();
        }
        refresh_edit_display();
        return;
    }
}

void shell_feed_char(char c) {
    handle_key((unsigned char)c);
}

void shell_feed_key(int key) {
    handle_key(key);
}

void shell_run_once(void) {
    int k = keyboard_try_getkey();
    if (k)
        handle_key(k);
    sched_maybe_preempt();
}
