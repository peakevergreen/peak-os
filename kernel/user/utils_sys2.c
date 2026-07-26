/* /bin sys pack: hostname, uptime, whoami, id, cal, gzip/gunzip, timeout, watch */
#include "libpeak.h"
#include "peak_io.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"
#include "timer.h"
#include "rtc.h"
#include "ubin.h"

#define COMP_MAX PEAK_IO_CAP
#define WATCH_MAX_ITERS 32

int uhostname_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("hostname", "[name]");
        return 0;
    }
    if (argc >= 2) {
        shell_env_set("HOSTNAME", argv[1]);
        console_printf("%s\n", argv[1]);
        return 0;
    }
    const char *h = shell_env_get("HOSTNAME");
    console_printf("%s\n", h && h[0] ? h : "peak");
    return 0;
}

int uuptime_main(int argc, char **argv) {
    int pretty = peak_has_flag(argc, argv, "-p");
    int since = peak_has_flag(argc, argv, "-s");
    uint64_t secs = timer_uptime_secs();
    if (since) {
        console_printf("%lu\n", (unsigned long)secs);
        return 0;
    }
    if (pretty) {
        uint64_t days = secs / 86400;
        uint64_t hrs = (secs % 86400) / 3600;
        uint64_t mins = (secs % 3600) / 60;
        if (days)
            console_printf("up %lu days, %lu hours, %lu minutes\n",
                           (unsigned long)days, (unsigned long)hrs, (unsigned long)mins);
        else if (hrs)
            console_printf("up %lu hours, %lu minutes\n",
                           (unsigned long)hrs, (unsigned long)mins);
        else
            console_printf("up %lu minutes\n", (unsigned long)mins);
        return 0;
    }
    uint64_t days = secs / 86400;
    uint64_t hrs = (secs % 86400) / 3600;
    uint64_t mins = (secs % 3600) / 60;
    uint64_t s = secs % 60;
    console_printf("up %lu days, %lu:",
                   (unsigned long)days, (unsigned long)hrs);
    if (mins < 10)
        console_putc('0');
    console_printf("%lu:", (unsigned long)mins);
    if (s < 10)
        console_putc('0');
    console_printf("%lu  (ticks=%lu)\n",
                   (unsigned long)s, (unsigned long)timer_ticks());
    return 0;
}

int uwhoami_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *u = shell_env_get("USER");
    console_printf("%s\n", u && u[0] ? u : "peak");
    return 0;
}

int uid_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *u = shell_env_get("USER");
    console_printf("uid=0(%s) gid=0(peak) groups=0(peak)\n",
                   u && u[0] ? u : "peak");
    return 0;
}

/* Zeller-ish day-of-week: 0=Sunday … for Gregorian */
static int dow(int y, int m, int d) {
    if (m < 3) {
        m += 12;
        y--;
    }
    int K = y % 100;
    int J = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    /* convert to 0=Sun */
    return (h + 6) % 7;
}

static int days_in_month(int y, int m) {
    static const int mdays[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2) {
        int leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    if (m < 1 || m > 12)
        return 30;
    return mdays[m];
}

int ucal_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv)) {
        peak_usage("cal", "[month] [year]");
        return 0;
    }
    struct rtc_time t;
    int month = 0, year = 0;
    if (rtc_read(&t) == 0) {
        month = t.month;
        year = t.year;
    } else {
        month = 1;
        year = 2026;
    }
    if (argc >= 2)
        month = peak_atoi(argv[1]);
    if (argc >= 3)
        year = peak_atoi(argv[2]);
    if (month < 1 || month > 12)
        month = 1;
    if (year < 1)
        year = 2026;

    static const char *moname[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    console_printf("    %s %d\n", moname[month], year);
    console_write("Su Mo Tu We Th Fr Sa\n");
    int start = dow(year, month, 1);
    int dim = days_in_month(year, month);
    for (int i = 0; i < start; i++)
        console_write("   ");
    for (int d = 1; d <= dim; d++) {
        if (d < 10)
            console_putc(' ');
        console_printf("%d", d);
        console_putc(((start + d) % 7 == 0 || d == dim) ? '\n' : ' ');
    }
    return 0;
}

/* Peak-native gzip: magic PEAKGZ1 + u32 len + RLE (byte,count) pairs; count 0 = raw run len next. */
static int gzip_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t cap, size_t *out_len) {
    if (cap < 12)
        return -1;
    memcpy(out, "PEAKGZ1", 7);
    out[7] = 0;
    out[8] = (uint8_t)(in_len & 0xFF);
    out[9] = (uint8_t)((in_len >> 8) & 0xFF);
    out[10] = (uint8_t)((in_len >> 16) & 0xFF);
    out[11] = (uint8_t)((in_len >> 24) & 0xFF);
    size_t o = 12;
    size_t i = 0;
    while (i < in_len) {
        uint8_t b = in[i];
        size_t run = 1;
        while (i + run < in_len && in[i + run] == b && run < 255)
            run++;
        if (o + 2 > cap)
            return -1;
        out[o++] = b;
        out[o++] = (uint8_t)run;
        i += run;
    }
    *out_len = o;
    return 0;
}

static int gzip_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t cap, size_t *out_len) {
    if (in_len < 12 || memcmp(in, "PEAKGZ1", 7) != 0)
        return -1;
    size_t expect = (size_t)in[8] | ((size_t)in[9] << 8) | ((size_t)in[10] << 16) | ((size_t)in[11] << 24);
    if (expect > cap)
        return -1;
    size_t o = 0;
    size_t i = 12;
    while (i + 1 < in_len && o < expect) {
        uint8_t b = in[i++];
        uint8_t run = in[i++];
        for (uint8_t r = 0; r < run && o < expect; r++)
            out[o++] = b;
    }
    if (o != expect)
        return -1;
    *out_len = o;
    return 0;
}

int unproc_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    /* Lite: report 1 processor (no SMP in guest). */
    console_write("1\n");
    return 0;
}

int ugzip_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("gzip", "<path>");
        console_write("  Peak RLE PEAKGZ1, 32 KiB input cap\n");
        return argc < 2 ? 1 : 0;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)) != 0)
        return 1;
    uint8_t in[COMP_MAX], out[COMP_MAX + 64];
    size_t n = 0;
    if (vfs_read_file(abs, (char *)in, sizeof(in), &n) != 0) {
        peak_perror("gzip", "cannot read");
        return 1;
    }
    size_t olen = 0;
    if (gzip_encode(in, n, out, sizeof(out), &olen) != 0) {
        peak_perror("gzip", "encode failed");
        return 1;
    }
    char dest[VFS_PATH_MAX];
    size_t al = strlen(abs);
    if (al + 4 >= sizeof(dest)) {
        peak_perror("gzip", "path too long");
        return 1;
    }
    memcpy(dest, abs, al);
    dest[al] = '.';
    dest[al + 1] = 'g';
    dest[al + 2] = 'z';
    dest[al + 3] = '\0';
    if (vfs_write_file(dest, (char *)out, olen) != 0) {
        peak_perror("gzip", "write failed");
        return 1;
    }
    console_printf("gzip: %s -> %s (%u -> %u)\n", abs, dest, (unsigned)n, (unsigned)olen);
    return 0;
}

int ugunzip_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("gunzip", "<path.gz>");
        return argc < 2 ? 1 : 0;
    }
    char abs[VFS_PATH_MAX];
    if (shell_resolve_path(argv[1], abs, sizeof(abs)) != 0)
        return 1;
    uint8_t in[COMP_MAX + 64], out[COMP_MAX];
    size_t n = 0;
    if (vfs_read_file(abs, (char *)in, sizeof(in), &n) != 0) {
        peak_perror("gunzip", "cannot read");
        return 1;
    }
    size_t olen = 0;
    if (gzip_decode(in, n, out, sizeof(out), &olen) != 0) {
        peak_perror("gunzip", "bad PEAKGZ1 stream");
        return 1;
    }
    char dest[VFS_PATH_MAX];
    size_t al = strlen(abs);
    if (al > 3 && abs[al - 3] == '.' && abs[al - 2] == 'g' && abs[al - 1] == 'z') {
        memcpy(dest, abs, al - 3);
        dest[al - 3] = '\0';
    } else {
        memcpy(dest, abs, al);
        dest[al] = '\0';
        if (al + 4 < sizeof(dest)) {
            dest[al] = '.';
            dest[al + 1] = 'o';
            dest[al + 2] = 'u';
            dest[al + 3] = 't';
            dest[al + 4] = '\0';
        }
    }
    if (vfs_write_file(dest, (char *)out, olen) != 0) {
        peak_perror("gunzip", "write failed");
        return 1;
    }
    console_printf("gunzip: %s -> %s (%u bytes)\n", abs, dest, (unsigned)olen);
    return 0;
}

int utimeout_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 3) {
        peak_usage("timeout", "<seconds> <command> [args...]");
        return argc < 3 ? 1 : 0;
    }
    int sec = peak_atoi(argv[1]);
    if (sec < 0)
        sec = 0;
    if (sec > 60)
        sec = 60;
    char path[64];
    size_t i = 0;
    path[i++] = '/';
    path[i++] = 'b';
    path[i++] = 'i';
    path[i++] = 'n';
    path[i++] = '/';
    for (const char *s = argv[2]; *s && i + 1 < sizeof(path); s++)
        path[i++] = *s;
    path[i] = '\0';

    if (sec == 0) {
        console_write("timeout: zero limit — skipping command (no preemption)\n");
        return 124;
    }
    uint64_t deadline = timer_ticks() + (uint64_t)sec * 100;
    /* Cooperative yield before run (no in-guest preemption). */
    while (timer_ticks() + 1 < deadline)
        hlt();
    int rc = ubin_run(path, argc - 2, argv + 2);
    if (rc == -999) {
        peak_perror("timeout", "unknown command");
        return 127;
    }
    if (timer_ticks() > deadline) {
        console_printf("timeout: limit %ds exceeded (cooperative — checked after command)\n", sec);
        return 124;
    }
    return rc;
}

int uwatch_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("watch", "[-n secs] [-c] <command> [args...]");
        return argc < 2 ? 1 : 0;
    }
    int interval = 2;
    int clear_screen = 0;
    int cmdi = 1;
    for (; cmdi < argc; cmdi++) {
        if (!strcmp(argv[cmdi], "-n") && cmdi + 1 < argc) {
            interval = peak_atoi(argv[++cmdi]);
            if (interval < 1)
                interval = 1;
            if (interval > 30)
                interval = 30;
            continue;
        }
        if (!strcmp(argv[cmdi], "-c")) {
            clear_screen = 1;
            continue;
        }
        if (argv[cmdi][0] == '-')
            continue;
        break;
    }
    if (cmdi >= argc) {
        peak_usage("watch", "[-n secs] <command> [args...]");
        return 1;
    }
    char path[64];
    size_t i = 0;
    path[i++] = '/';
    path[i++] = 'b';
    path[i++] = 'i';
    path[i++] = 'n';
    path[i++] = '/';
    for (const char *s = argv[cmdi]; *s && i + 1 < sizeof(path); s++)
        path[i++] = *s;
    path[i] = '\0';

    int last = 0;
    for (int iter = 0; iter < WATCH_MAX_ITERS; iter++) {
        if (clear_screen && iter > 0)
            console_clear();
        console_printf("--- watch iter %d/%d ---\n", iter + 1, WATCH_MAX_ITERS);
        last = ubin_run(path, argc - cmdi, argv + cmdi);
        if (last == -999) {
            peak_perror("watch", "unknown command");
            return 127;
        }
        if (iter + 1 >= WATCH_MAX_ITERS)
            break;
        uint64_t target = timer_ticks() + (uint64_t)interval * 100;
        while (timer_ticks() < target)
            hlt();
    }
    console_printf("watch: max iterations (%d) reached\n", WATCH_MAX_ITERS);
    return last;
}
