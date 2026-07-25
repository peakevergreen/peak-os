#include "sysmon.h"
#include "pmm.h"
#include "heap.h"
#include "timer.h"
#if defined(__x86_64__)
#include "e1000.h"
#endif
#include "vfs.h"
#include "sched.h"
#include "sync.h"
#include "util.h"

#define SAMPLE_INTERVAL 50 /* ticks @ 100Hz ≈ 0.5s */

static struct sysmon_sample hist[SYSMON_HISTORY];
static int hist_len;
static int hist_head;
static struct sysmon_sample latest;
static int inited;
static struct spinlock mon_lock;

static uint64_t idle_accum;
static uint64_t idle_mark;
static int idle_inside;
static uint64_t last_sample_ticks;
static uint64_t prev_rx, prev_tx;
static uint64_t window_idle;
static uint64_t window_start_ticks;
static uint64_t mem_peak_pages;
static uint64_t heap_peak;
static uint64_t frame_count;
static uint64_t frame_window_start;
static uint32_t last_fps;
static uint32_t last_compose_us;
static uint32_t last_present_us;
static uint32_t last_surf_pressure;
static uint32_t last_peakvec_us;
static uint32_t last_agent_audit_us;

#if defined(__x86_64__)
static uint64_t sysmon_cycles(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#elif defined(__aarch64__)
static uint64_t sysmon_cycles(void) {
    uint64_t t;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(t));
    return t;
}
#else
static uint64_t sysmon_cycles(void) {
    return timer_ticks() * 20000ull;
}
#endif

uint32_t sysmon_now_us(void) {
    /* Match desktop compose/present scale (~2k cycles per µs at typical QEMU). */
    return (uint32_t)(sysmon_cycles() / 2000ull);
}

void sysmon_init(void) {
    spin_init(&mon_lock, "sysmon");
    memset(hist, 0, sizeof(hist));
    memset(&latest, 0, sizeof(latest));
    hist_len = 0;
    hist_head = 0;
    idle_accum = 0;
    idle_inside = 0;
    last_sample_ticks = 0;
    prev_rx = prev_tx = 0;
    window_idle = 0;
    window_start_ticks = timer_ticks();
    mem_peak_pages = 0;
    heap_peak = 0;
    frame_count = 0;
    frame_window_start = timer_ticks();
    last_fps = 0;
    last_compose_us = 0;
    last_present_us = 0;
    last_surf_pressure = 0;
    last_peakvec_us = 0;
    last_agent_audit_us = 0;
    inited = 1;
}

void sysmon_note_frame(void) {
    if (!inited)
        sysmon_init();
    frame_count++;
    uint64_t now = timer_ticks();
    if (now - frame_window_start >= 100) {
        last_fps = (uint32_t)frame_count;
        frame_count = 0;
        frame_window_start = now;
    }
}

void sysmon_note_compose_us(uint32_t us) {
    last_compose_us = us;
}

void sysmon_note_present_us(uint32_t us) {
    last_present_us = us;
}

void sysmon_note_surf_pressure(uint32_t pct) {
    if (pct > 100)
        pct = 100;
    last_surf_pressure = pct;
}

void sysmon_note_peakvec_us(uint32_t us) {
    last_peakvec_us = us;
}

void sysmon_note_agent_audit_us(uint32_t us) {
    last_agent_audit_us = us;
}

void sysmon_idle_enter(void) {
    if (!inited)
        sysmon_init();
    if (idle_inside)
        return;
    idle_inside = 1;
    idle_mark = timer_ticks();
}

void sysmon_idle_leave(void) {
    if (!idle_inside)
        return;
    uint64_t now = timer_ticks();
    if (now > idle_mark) {
        uint64_t d = now - idle_mark;
        idle_accum += d;
        window_idle += d;
    }
    idle_inside = 0;
}

void sysmon_reset_history(void) {
    spin_lock(&mon_lock);
    hist_len = 0;
    hist_head = 0;
    memset(hist, 0, sizeof(hist));
    spin_unlock(&mon_lock);
}

static uint32_t pct(uint64_t num, uint64_t den) {
    if (!den)
        return 0;
    uint64_t p = (num * 100ull) / den;
    if (p > 100)
        p = 100;
    return (uint32_t)p;
}

static void push_sample(const struct sysmon_sample *s) {
    hist[hist_head] = *s;
    hist_head = (hist_head + 1) % SYSMON_HISTORY;
    if (hist_len < SYSMON_HISTORY)
        hist_len++;
}

void sysmon_poll(void) {
    if (!inited)
        sysmon_init();

    int was_idle = idle_inside;
    if (was_idle)
        sysmon_idle_leave();

    uint64_t now = timer_ticks();
    if (last_sample_ticks && now - last_sample_ticks < SAMPLE_INTERVAL) {
        if (was_idle)
            sysmon_idle_enter();
        return;
    }

    spin_lock(&mon_lock);

    uint64_t elapsed = now - window_start_ticks;
    if (!elapsed)
        elapsed = 1;
    uint64_t idle = window_idle;
    if (idle > elapsed)
        idle = elapsed;
    uint32_t idle_pct = pct(idle, elapsed);
    uint32_t load_pct = 100 - idle_pct;

    uint64_t free_p = pmm_free_pages();
    uint64_t tot_p = pmm_total_pages();
    uint64_t used_p = tot_p > free_p ? tot_p - free_p : 0;
    if (used_p > mem_peak_pages)
        mem_peak_pages = used_p;

    uint64_t hu = 0, hf = 0, hb = 0;
    heap_get_stats(&hu, &hf, &hb);
    if (hu > heap_peak)
        heap_peak = hu;

    uint64_t rx_bytes = 0, tx_bytes = 0, rx_packets = 0, tx_packets = 0;
#if defined(__x86_64__)
    {
        struct e1000_stats ns;
        memset(&ns, 0, sizeof(ns));
        if (e1000_ready())
            e1000_get_stats(&ns);
        rx_bytes = ns.rx_bytes;
        tx_bytes = ns.tx_bytes;
        rx_packets = ns.rx_packets;
        tx_packets = ns.tx_packets;
    }
#endif

    uint32_t rx_bps = 0, tx_bps = 0;
    if (last_sample_ticks) {
        uint64_t dt = now - last_sample_ticks;
        if (!dt)
            dt = 1;
        uint64_t drx = rx_bytes >= prev_rx ? rx_bytes - prev_rx : 0;
        uint64_t dtx = tx_bytes >= prev_tx ? tx_bytes - prev_tx : 0;
        rx_bps = (uint32_t)((drx * 100ull) / dt);
        tx_bps = (uint32_t)((dtx * 100ull) / dt);
    }
    prev_rx = rx_bytes;
    prev_tx = tx_bytes;

    struct sysmon_sample s;
    memset(&s, 0, sizeof(s));
    s.ticks = now;
    s.uptime_secs = timer_uptime_secs();
    s.mem_used_pages = used_p;
    s.mem_total_pages = tot_p;
    s.mem_peak_pages = mem_peak_pages;
    s.heap_used = hu;
    s.heap_free = hf;
    s.heap_blocks = hb;
    s.heap_peak = heap_peak;
    s.heap_frag_pct = heap_fragmentation_pct();
    {
        struct heap_freelist_stats fl;
        heap_get_freelist_stats(&fl);
        s.heap_free_blocks = fl.free_blocks;
    }
    s.heap_oom = heap_oom_count();
    s.vfs_nodes = (uint64_t)vfs_node_count();
    s.rx_bytes = rx_bytes;
    s.tx_bytes = tx_bytes;
    s.rx_packets = rx_packets;
    s.tx_packets = tx_packets;
    s.ctx_switches = sched_ctx_switches();
    s.irq_count = timer_irq_count();
    s.tasks = (uint32_t)sched_task_count();
    s.mem_pct = pct(used_p, tot_p);
    s.heap_pct = pct(hu, hu + hf);
    s.idle_pct = idle_pct;
    s.load_pct = load_pct;
    s.rx_bps = rx_bps;
    s.tx_bps = tx_bps;
    s.gui_fps = last_fps;
    s.compose_us = last_compose_us;
    s.present_us = last_present_us;
    s.surf_pressure = last_surf_pressure;
    s.peakvec_us = last_peakvec_us;
    s.agent_audit_us = last_agent_audit_us;

    latest = s;
    push_sample(&s);

    last_sample_ticks = now;
    window_idle = 0;
    window_start_ticks = now;

    spin_unlock(&mon_lock);

    if (was_idle)
        sysmon_idle_enter();
}

const struct sysmon_sample *sysmon_latest(void) {
    if (!inited)
        sysmon_init();
    return &latest;
}

int sysmon_history_len(void) {
    return hist_len;
}

int sysmon_history(struct sysmon_sample *out, int max) {
    if (!out || max <= 0)
        return 0;
    spin_lock(&mon_lock);
    int n = hist_len < max ? hist_len : max;
    int start = (hist_head - hist_len + SYSMON_HISTORY) % SYSMON_HISTORY;
    if (hist_len > max)
        start = (hist_head - max + SYSMON_HISTORY) % SYSMON_HISTORY;
    for (int i = 0; i < n; i++)
        out[i] = hist[(start + i) % SYSMON_HISTORY];
    spin_unlock(&mon_lock);
    return n;
}

void sysmon_format_bytes(uint64_t n, char *buf, size_t cap) {
    if (!buf || !cap)
        return;
    if (n < 1024) {
        snprintf(buf, cap, "%luB", (unsigned long)n);
    } else if (n < 1024ull * 1024) {
        snprintf(buf, cap, "%luK", (unsigned long)(n / 1024));
    } else {
        snprintf(buf, cap, "%luM", (unsigned long)(n / (1024ull * 1024)));
    }
}

void sysmon_format_rate(uint32_t bps, char *buf, size_t cap) {
    if (!buf || !cap)
        return;
    if (bps < 1024)
        snprintf(buf, cap, "%uB/s", (unsigned)bps);
    else if (bps < 1024u * 1024)
        snprintf(buf, cap, "%uK/s", (unsigned)(bps / 1024));
    else
        snprintf(buf, cap, "%uM/s", (unsigned)(bps / (1024u * 1024)));
}

void sysmon_format_us(uint32_t us, char *buf, size_t cap) {
    if (!buf || !cap)
        return;
    if (us >= 1000)
        snprintf(buf, cap, "%u.%01ums", us / 1000, (unsigned)((us % 1000) / 100));
    else
        snprintf(buf, cap, "%uus", (unsigned)us);
}

void sysmon_sparkline(const uint32_t *series, int n, char *out, int out_cols) {
    static const char levels[] = " .:-=+*#%@";
    if (!out || out_cols <= 0)
        return;
    if (!series || n <= 0) {
        for (int i = 0; i < out_cols - 1; i++)
            out[i] = ' ';
        out[out_cols - 1] = '\0';
        return;
    }
    uint32_t mx = 1;
    for (int i = 0; i < n; i++)
        if (series[i] > mx)
            mx = series[i];
    int cols = out_cols - 1;
    for (int c = 0; c < cols; c++) {
        int idx = (c * n) / cols;
        if (idx >= n)
            idx = n - 1;
        uint32_t v = series[idx];
        int li = (int)((v * 9ull) / mx);
        if (li > 9)
            li = 9;
        out[c] = levels[li];
    }
    out[cols] = '\0';
}

const char *sysmon_sparkline_legend(void) { return " .:-=+*#%@  (low -> high)"; }

void sysmon_sparkline_range(const uint32_t *series, int n, uint32_t *out_min, uint32_t *out_max) {
    uint32_t mn = 0, mx = 0;
    if (series && n > 0) { mn = mx = series[0];
        for (int i = 1; i < n; i++) { if (series[i] < mn) mn = series[i]; if (series[i] > mx) mx = series[i]; } }
    if (out_min) *out_min = mn; if (out_max) *out_max = mx;
}

void sysmon_format_bar(char *buf, size_t cap, uint32_t pct, int width) {
    if (!buf || cap < 4 || width < 1) return;
    if (width > (int)cap - 3) width = (int)cap - 3;
    buf[0] = '['; int filled = (int)((pct * (uint32_t)width) / 100);
    for (int i = 0; i < width; i++) buf[1 + i] = (i < filled) ? '#' : '-';
    buf[1 + width] = ']'; buf[2 + width] = '\0';
}

static const char *task_state_name(enum task_state st) {
    switch (st) { case TASK_RUNNING: return "run"; case TASK_READY: return "ready";
    case TASK_BLOCKED: return "block"; case TASK_ZOMBIE: return "zomb"; default: return "?"; }
}

int sysmon_snapshot(char *buf, size_t cap) {
    if (!buf || cap < 64) return -1;
    sysmon_poll(); const struct sysmon_sample *s = sysmon_latest(); size_t off = 0;
#define SNAP(...) do { int _n = snprintf(buf + off, cap > off ? cap - off : 0, __VA_ARGS__); if (_n < 0) return -1; if ((size_t)_n >= cap - off) return -1; off += (size_t)_n; } while (0)
    uint64_t mins = s->uptime_secs / 60, secs = s->uptime_secs % 60;
    SNAP("Peak sysmon — up %lum%02lus  load %u%% idle %u%%  fps %u\n", (unsigned long)mins, (unsigned long)secs, (unsigned)s->load_pct, (unsigned)s->idle_pct, (unsigned)s->gui_fps);
    char bar[20], a[24], b[24], p[24], f[24];
    sysmon_format_bar(bar, sizeof(bar), s->mem_pct, 12);
    sysmon_format_bytes(s->mem_used_pages * 4096ull, a, sizeof(a));
    sysmon_format_bytes((s->mem_total_pages - s->mem_used_pages) * 4096ull, f, sizeof(f));
    sysmon_format_bytes(s->mem_total_pages * 4096ull, b, sizeof(b));
    sysmon_format_bytes(s->mem_peak_pages * 4096ull, p, sizeof(p));
    SNAP("Mem  %s %3u%%  %s / %s  peak %s\n", bar, (unsigned)s->mem_pct, a, b, p);
    SNAP("     used %s  free %s  (%lu/%lu pages)\n", a, f, (unsigned long)s->mem_used_pages, (unsigned long)s->mem_total_pages);
    sysmon_format_bar(bar, sizeof(bar), s->heap_pct, 12);
    sysmon_format_bytes(s->heap_used, a, sizeof(a)); sysmon_format_bytes(s->heap_free, f, sizeof(f));
    sysmon_format_bytes(s->heap_used + s->heap_free, b, sizeof(b)); sysmon_format_bytes(s->heap_peak, p, sizeof(p));
    SNAP("Heap %s %3u%%  %s / %s  peak %s  (%lu blk)\n", bar, (unsigned)s->heap_pct, a, b, p, (unsigned long)s->heap_blocks);
    SNAP("     used %s  free %s  frag %u%%  free blk %u  oom %u\n", a, f,
         (unsigned)s->heap_frag_pct, (unsigned)s->heap_free_blocks, (unsigned)s->heap_oom);
    char rxr[16], txr[16], rxt[16], txt[16];
    sysmon_format_rate(s->rx_bps, rxr, sizeof(rxr)); sysmon_format_rate(s->tx_bps, txr, sizeof(txr));
    sysmon_format_bytes(s->rx_bytes, rxt, sizeof(rxt)); sysmon_format_bytes(s->tx_bytes, txt, sizeof(txt));
    SNAP("Net  RX %s  TX %s\n", rxr, txr);
    SNAP("     total RX %s TX %s  pkts %lu / %lu\n", rxt, txt, (unsigned long)s->rx_packets, (unsigned long)s->tx_packets);
    char cu[16], pu[16], pv[16], au[16];
    sysmon_format_us(s->compose_us, cu, sizeof(cu)); sysmon_format_us(s->present_us, pu, sizeof(pu));
    sysmon_format_us(s->peakvec_us, pv, sizeof(pv)); sysmon_format_us(s->agent_audit_us, au, sizeof(au));
    SNAP("Sched tasks %u  ctx %lu  irq %lu  vfs %lu\n", (unsigned)s->tasks, (unsigned long)s->ctx_switches, (unsigned long)s->irq_count, (unsigned long)s->vfs_nodes);
    SNAP("GFX  compose %s  present %s  surf %u%%\n", cu, pu, (unsigned)s->surf_pressure);
    SNAP("Agent peakvec %s  audit %s\n\n", pv, au);
    struct task list[MAX_TASKS]; int tn = sched_list_tasks(list, MAX_TASKS); sched_sort_tasks(list, tn); int cur = sched_current_pid();
    SNAP("PID  STATE   TICKS   AGE     SHARE  NAME\n");
    uint64_t total_ticks = 0;
    for (int i = 0; i < tn; i++)
        total_ticks += list[i].cpu_ticks;
    if (!total_ticks)
        total_ticks = 1;
    uint64_t now = timer_ticks();
    for (int i = 0; i < tn && i < 16; i++) {
        uint64_t age = now > list[i].spawned_at ? now - list[i].spawned_at : 0;
        uint32_t share = (uint32_t)((list[i].cpu_ticks * 100ull) / total_ticks);
        SNAP("%-4d %-7s %-7lu %-7lu %-5u %s%s\n", list[i].pid,
             task_state_name(list[i].state), (unsigned long)list[i].cpu_ticks,
             (unsigned long)age, (unsigned)share, list[i].name,
             list[i].pid == cur ? " *" : "");
    }
    SNAP("\n");
    struct sysmon_sample hist[SYSMON_HISTORY]; int hn = sysmon_history(hist, SYSMON_HISTORY);
    uint32_t load_s[SYSMON_HISTORY], mem_s[SYSMON_HISTORY], fps_s[SYSMON_HISTORY], net_s[SYSMON_HISTORY];
    for (int i = 0; i < hn; i++) { load_s[i]=hist[i].load_pct; mem_s[i]=hist[i].mem_pct; fps_s[i]=hist[i].gui_fps; net_s[i]=hist[i].rx_bps+hist[i].tx_bps; }
    char spark[SYSMON_SPARK_COLS + 1]; uint32_t smin, smax;
    SNAP("Spark %s\n", sysmon_sparkline_legend());
    sysmon_sparkline(load_s, hn, spark, SYSMON_SPARK_COLS); sysmon_sparkline_range(load_s, hn, &smin, &smax); SNAP("Load %s  (%u–%u%%)\n", spark, (unsigned)smin, (unsigned)smax);
    sysmon_sparkline(mem_s, hn, spark, SYSMON_SPARK_COLS); sysmon_sparkline_range(mem_s, hn, &smin, &smax); SNAP("Mem  %s  (%u–%u%%)\n", spark, (unsigned)smin, (unsigned)smax);
    sysmon_sparkline(fps_s, hn, spark, SYSMON_SPARK_COLS); sysmon_sparkline_range(fps_s, hn, &smin, &smax); SNAP("FPS  %s  (%u–%u)\n", spark, (unsigned)smin, (unsigned)smax);
    sysmon_sparkline(net_s, hn, spark, SYSMON_SPARK_COLS); sysmon_sparkline_range(net_s, hn, &smin, &smax); SNAP("Net  %s  (%u–%u B/s)\n", spark, (unsigned)smin, (unsigned)smax);
#undef SNAP
    return (int)off;
}

int sysmon_export(const char *path) {
    static char snap[4096]; int n = sysmon_snapshot(snap, sizeof(snap));
    if (n < 0) return -1;
    return vfs_write_file(path && path[0] ? path : SYSMON_EXPORT_PATH, snap, (size_t)n);
}
