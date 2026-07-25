#include "shell.h"
#include "console.h"
#include "util.h"
#include "vfs.h"
#include "peak_errno.h"

static char cwd[VFS_PATH_MAX] = "/home/dev/workspace";
static char stdin_path_buf[VFS_PATH_MAX];
static int stdin_path_set;
static int last_path_err = PEAK_OK;
static int last_status;

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

int shell_last_path_errno(void) {
    return last_path_err;
}

void shell_perror_path(const char *ctx, const char *path) {
    console_write(ctx ? ctx : "shell");
    console_write(": ");
    if (path && path[0]) {
        console_write(path);
        console_write(": ");
    }
    console_write(peak_strerror(last_path_err));
    console_putc('\n');
}

void shell_set_last_status(int rc) {
    last_status = rc;
}

int shell_last_status(void) {
    return last_status;
}

int shell_resolve_path(const char *in, char *out, size_t out_len) {
    last_path_err = PEAK_OK;
    if (!in || !out || out_len < 2) {
        last_path_err = PEAK_EINVAL;
        return last_path_err;
    }
    /* Absolute: normalize directly — skip the VFS_PATH_MAX temp copy. */
    if (in[0] == '/') {
        int rc = vfs_normalize(in, out, out_len);
        if (rc != 0)
            last_path_err = rc;
        return rc;
    }

    char tmp[VFS_PATH_MAX];
    size_t o = 0;
    for (; cwd[o] && o + 1 < sizeof(tmp); o++)
        tmp[o] = cwd[o];
    if (!(o == 1 && tmp[0] == '/')) {
        if (o + 1 >= sizeof(tmp)) {
            last_path_err = PEAK_ENOSPC;
            return last_path_err;
        }
        tmp[o++] = '/';
    }
    for (size_t i = 0; in[i] && o + 1 < sizeof(tmp); i++)
        tmp[o++] = in[i];
    tmp[o] = '\0';
    int rc = vfs_normalize(tmp, out, out_len);
    if (rc != 0)
        last_path_err = rc;
    return rc;
}

int shell_chdir(const char *path) {
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(path, abs, sizeof(abs)) != 0)
        return last_path_err;
    if (!vfs_is_dir(abs)) {
        last_path_err = vfs_exists(abs) ? PEAK_ENOTDIR : PEAK_ENOENT;
        return last_path_err;
    }
    shell_env_set("OLDPWD", cwd);
    size_t i = 0;
    for (; abs[i] && i + 1 < sizeof(cwd); i++)
        cwd[i] = abs[i];
    cwd[i] = '\0';
    shell_env_set("PWD", cwd);
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
    { "cd", "nav", "change directory (cd - = OLDPWD)" },
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
    { "diff", "text", "line diff (-/+)" },
    { "sort", "text", "sort lines" },
    { "uniq", "text", "drop adjacent dup lines" },
    { "cut", "text", "cut -f N [-d delim]" },
    { "tr", "text", "translate chars" },
    { "sed", "text", "sed-lite s/// d p -n" },
    { "cmp", "text", "byte compare files" },
    { "basename", "file", "strip directory" },
    { "dirname", "file", "strip basename" },
    { "realpath", "file", "normalize path" },
    { "hexdump", "text", "hex dump" },
    { "strings", "text", "printable runs" },
    { "echo", "text", "print arguments" },
    { "printf", "text", "format print (%s %d %u %x)" },
    { "tee", "text", "copy stdin to files (-a append)" },
    { "test", "sys", "file/string/int predicates (exit 0/1)" },
    { "[", "sys", "test alias (requires closing ])" },
    { "yes", "text", "print line repeatedly (bounded)" },
    { "fold", "text", "wrap lines (-w width)" },
    { "rev", "text", "reverse each line" },
    { "od", "text", "octal/hex dump (-tx1/-to1)" },
    { "split", "text", "split file by bytes (-b)" },
    { "paste", "text", "merge lines of two files" },
    { "nl", "text", "number non-empty lines" },
    { "tac", "text", "print lines in reverse order" },
    { "xargs", "text", "build argv from stdin tokens" },
    { "sha256sum", "text", "SHA-256 digest (64 KiB cap)" },
    { "md5sum", "text", "MD5 digest (64 KiB cap)" },
    { "base64", "text", "base64 encode/decode (-d)" },
    { "less", "text", "page file (space/q)" },
    { "more", "text", "page file (space/q)" },
    { "time", "sys", "time a built-in command" },
    { "edit", "text", "buffer editor (:w/:q/:p /search)" },
    { "clear", "sys", "clear screen" },
    { "date", "sys", "uptime clock" },
    { "free", "sys", "memory pages" },
    { "top", "sys", "live system monitor" },
    { "sysmon", "sys", "alias for top" },
    { "ps", "sys", "list kernel tasks/threads" },
    { "kill", "sys", "kill task by pid or name" },
    { "env", "sys", "list/set env" },
    { "export", "sys", "set NAME=val" },
    { "which", "sys", "resolve /bin path" },
    { "seq", "sys", "print number sequence" },
    { "sleep", "sys", "sleep seconds" },
    { "hostname", "sys", "get/set HOSTNAME" },
    { "uptime", "sys", "pretty uptime" },
    { "whoami", "sys", "print USER" },
    { "id", "sys", "uid/gid (single-user)" },
    { "cal", "sys", "month calendar" },
    { "gzip", "sys", "Peak RLE compress (.gz PEAKGZ1)" },
    { "gunzip", "sys", "decompress PEAKGZ1 .gz" },
    { "timeout", "sys", "run command (deadline note)" },
    { "watch", "sys", "repeat command (-n secs, bounded)" },
    { "theme", "sys", "list/set/next theme" },
    { "wallpaper", "sys", "set desktop wallpaper (PPM)" },
    { "scale", "sys", "UI scale 1..4" },
    { "uname", "sys", "system name" },
    { "true", "sys", "exit 0" },
    { "false", "sys", "exit 1" },
    { "sh", "sys", "nested shell loop" },
    { "reboot", "sys", "reboot guest" },
    { "help", "sys", "this help" },
    { "history", "sys", "command history" },
    { "alias", "sys", "list/set aliases (/var/peak/aliases)" },
    { "man", "sys", "command help" },
    { "peak", "meta", "Peak meta info" },
    { "ask", "meta", "peak-agent prompt (run ls …, audit, memory)" },
    { "audit", "meta", "show agent audit log tail" },
    { "memory", "meta", "project memory / recall file" },
    { "policy", "meta", "agent tool/path policy file" },
    { "privacy", "meta", "persist / net-allow / kill-switch" },
    { "disksave", "meta", "save workspace to block device" },
    { "gui", "meta", "enter desktop (Ctrl+Alt+Esc leaves)" },
    { "ctr", "net", "stage Dockerfile subset; serve static HTTP (not OCI)" },
    { "ctrd", "net", "ping Peak ctr staging helper" },
    { "ifconfig", "net", "show e1000 IPv4 config" },
    { "ping", "net", "DNS + TCP reachability probe" },
    { "wget", "net", "HTTP GET (-O path); TLS errors named" },
    { "curl", "net", "alias for wget (-o path)" },
    { "nslookup", "net", "DNS A lookup" },
    { "host", "net", "DNS A lookup (short)" },
    { "nc", "net", "TCP connect (+ optional send/recv)" },
    { "tlsinfo", "net", "TLS trust summary, last error, hostname -m test" },
    { "tar", "file", "ustar create/extract (-c/-x)" },
    { "js", "sys", "Peak JS: js -e 'code' | js file.js" },
    { NULL, NULL, NULL },
};

void shell_help_topics(void) {
    console_write("Peak CLI — categories:\n");
    console_write("  nav   pwd cd ls tree find\n");
    console_write("  file  mkdir touch rm cp mv ln stat du df truncate basename dirname realpath\n");
    console_write("  text  cat head tail wc grep diff sort uniq cut tr sed cmp hexdump strings echo printf tee yes\n");
    console_write("        fold rev od split paste nl tac xargs sha256sum md5sum base64 less more edit\n");
    console_write("  sys   date free top sysmon ps kill env which seq sleep theme wallpaper scale\n");
    console_write("        hostname uptime whoami id cal gzip gunzip timeout watch\n");
    console_write("        uname true false test [ yes time history sh reboot help man js\n");
    console_write("  meta  peak ask audit memory policy privacy disksave gui\n");
    console_write("  net   ctr ctrd ifconfig ping wget curl nslookup host nc tlsinfo\n");
    console_write("  file  … tar basename dirname realpath\n");
    console_write("Shell: quotes, globs (* ?), pipes |, redirects > >> <\n");
    console_write("Try: man <cmd>   ls *.c   echo hi | wc   tar -c a.tar f   gui\n");
}

void shell_help_cmd(const char *cmd) {
    for (int i = 0; help_table[i].cmd; i++) {
        if (!strcmp(help_table[i].cmd, cmd)) {
            console_printf("%s (%s): %s\n", help_table[i].cmd, help_table[i].cat,
                           help_table[i].blurb);
            return;
        }
    }
    console_write("unknown command — try help or man <cmd>\n");
}

void shell_builtins_init(void) {
    memset(env, 0, sizeof(env));
    shell_env_set("HOME", "/home/dev");
    shell_env_set("PATH", "/bin");
    shell_env_set("USER", "peak");
    shell_env_set("HOSTNAME", "peak");
    last_status = 0;
    last_path_err = PEAK_OK;
}
