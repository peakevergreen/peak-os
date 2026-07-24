#include "shell.h"
#include "console.h"
#include "util.h"
#include "vfs.h"

static char cwd[VFS_PATH_MAX] = "/home/dev/workspace";
static char stdin_path_buf[VFS_PATH_MAX];
static int stdin_path_set;

#define ENV_MAX 32
#define ENV_KEY 32
#define ENV_VAL 96

static struct {
    char key[ENV_KEY];
    char val[ENV_VAL];
    int used;
} env[ENV_MAX];

const char *shell_getcwd(void) {
    return cwd;
}

void shell_set_stdin_path(const char *path) {
    if (!path || !path[0]) {
        stdin_path_set = 0;
        stdin_path_buf[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(stdin_path_buf); i++)
        stdin_path_buf[i] = path[i];
    stdin_path_buf[i] = '\0';
    stdin_path_set = 1;
}

const char *shell_stdin_path(void) {
    return stdin_path_set ? stdin_path_buf : 0;
}

int shell_resolve_path(const char *in, char *out, size_t out_len) {
    if (!in || !out || out_len < 2)
        return -1;
    /* Absolute: normalize directly — skip the VFS_PATH_MAX temp copy. */
    if (in[0] == '/')
        return vfs_normalize(in, out, out_len);

    char tmp[VFS_PATH_MAX];
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
    { "disksave", "meta", "save workspace to block device" },
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
    console_write("  meta  peak ask audit memory policy privacy disksave gui\n");
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

void shell_builtins_init(void) {
    memset(env, 0, sizeof(env));
    shell_env_set("HOME", "/home/dev");
    shell_env_set("PATH", "/bin");
    shell_env_set("USER", "peak");
}
