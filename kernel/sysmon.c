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
