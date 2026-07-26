#include "monitor.h"
#include "sysmon.h"
#include "sched.h"
#include "heap.h"
#include "timer.h"
#include "net.h"
#include "browser.h"
#include "fb.h"
#include "theme.h"
#include "notify.h"
#include "util.h"
#include "rtc.h"
#include "vfs.h"

/*
 * System Monitor desktop pane — overview, task list, and network tabs.
 * Polls sysmon on tick; large history buffers live in static storage.
 */

enum monitor_page {
    MON_PAGE_OVERVIEW = 0,
    MON_PAGE_TASKS    = 1,
    MON_PAGE_NET      = 2,
    MON_PAGE_COUNT    = 3,
};

static int needs_redraw = 1;
static int page; /* MON_PAGE_* */
static int paused;
static uint32_t mon_export_x, mon_export_y, mon_export_w, mon_export_h;
static char mon_last_export[VFS_PATH_MAX];

static void monitor_build_export_path(char *path, size_t cap) {
    struct rtc_time rt;
    if (rtc_read(&rt) == 0)
        snprintf(path, cap, "/tmp/sysmon-%04u%02u%02u-%02u%02u%02u.txt",
                 (unsigned)rt.year, (unsigned)rt.month, (unsigned)rt.day,
                 (unsigned)rt.hour, (unsigned)rt.min, (unsigned)rt.sec);
    else
        snprintf(path, cap, "/tmp/sysmon-%lu.txt", (unsigned long)timer_uptime_secs());
}

static int monitor_do_export(void) {
    char path[VFS_PATH_MAX];
    monitor_build_export_path(path, sizeof(path));
    if (sysmon_export(path) != 0)
        return -1;
    snprintf(mon_last_export, sizeof(mon_last_export), "%s", path);
    return 0;
}

/* History / task scratch — keep off the 8KB kernel stack. */
static struct sysmon_sample g_hist[SYSMON_HISTORY];
static uint32_t g_series_a[SYSMON_HISTORY];
static uint32_t g_series_b[SYSMON_HISTORY];
static uint32_t g_series_c[SYSMON_HISTORY];
static struct task g_tasks[MAX_TASKS];

void monitor_reset(void) {
    needs_redraw = 1;
    page = MON_PAGE_OVERVIEW;
    paused = 0;
}

void monitor_clear_redraw(void) {
    needs_redraw = 0;
}

void monitor_toggle_pause(void) {
    paused = !paused;
    needs_redraw = 1;
}

int monitor_is_paused(void) {
    return paused;
}

int monitor_wants_redraw(void) {
    return needs_redraw;
}

void monitor_input(char c) {
    if (c == 'p' || c == 'P') {
        paused = !paused;
        needs_redraw = 1;
    } else if (c == 'r' || c == 'R') {
        sysmon_reset_history();
        needs_redraw = 1;
    } else if (c == '1') {
        page = MON_PAGE_OVERVIEW;
        needs_redraw = 1;
    } else if (c == '2') {
        page = MON_PAGE_TASKS;
        needs_redraw = 1;
    } else if (c == '3') {
        page = MON_PAGE_NET;
        needs_redraw = 1;
    } else if (c == '[' || c == 'h' || c == 'H') {
        if (page > MON_PAGE_OVERVIEW)
            page--;
        needs_redraw = 1;
    } else if (c == ']' || c == 'l' || c == 'L') {
        if (page < MON_PAGE_NET)
            page++;
        needs_redraw = 1;
    } else if (c == 'e' || c == 'E') {
        if (monitor_do_export() == 0) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Saved export");
            notify_push(msg);
        } else
            notify_push("Export failed");
        needs_redraw = 1;
    }
}

void monitor_tick(void) {
    if (paused)
        return;
    sysmon_poll();
    needs_redraw = 1;
}

static uint32_t U(uint32_t v) { return v * fb_ui_scale(); }

static void draw_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint32_t pct, uint32_t fill, uint32_t track) {
    if (w < 4 || h < 2)
        return;
    fb_fill_rect(x, y, w, h, track);
    uint32_t fw = (w * pct) / 100;
    if (fw > w)
        fw = w;
    if (fw)
        fb_fill_rect(x, y, fw, h, fill);
}


static void monitor_export_btn(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t fg, uint32_t bg, uint32_t acc) {
    mon_export_x = x; mon_export_y = y; mon_export_w = w; mon_export_h = h;
    fb_fill_rect(x, y, w, h, bg);
    fb_fill_rect(x, y, w, U(2), acc);
    fb_draw_string_fit(x + U(4), y + U(2), w - U(8), "Export", fg, bg);
}
static void draw_spark(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       const uint32_t *vals, int n, uint32_t color, uint32_t bg) {
    fb_fill_rect(x, y, w, h, bg);
    for (uint32_t gy = y + h / 4; gy < y + h; gy += h / 4)
        fb_fill_rect(x, gy, w, U(1), color);
    if (!vals || n <= 0 || w < 2 || h < 4)
        return;
    uint32_t mx = 1;
    for (int i = 0; i < n; i++)
        if (vals[i] > mx)
            mx = vals[i];
    uint32_t bar_w = w / (uint32_t)n;
    if (bar_w < 1)
        bar_w = 1;
    for (int i = 0; i < n; i++) {
        uint32_t bh = (uint32_t)(((uint64_t)vals[i] * (h - 2)) / mx);
        if (bh < 1 && vals[i] > 0)
            bh = 1;
        uint32_t bx = x + (uint32_t)i * bar_w;
        if (bx + bar_w > x + w)
            break;
        fb_fill_rect(bx, y + h - bh - 1, bar_w > 1 ? bar_w - 1 : 1, bh, color);
    }
}

static const char *state_name(enum task_state st) {
    switch (st) {
    case TASK_READY: return "ready";
    case TASK_RUNNING: return "run";
    case TASK_BLOCKED: return "block";
    case TASK_ZOMBIE: return "zomb";
    default: return "?";
    }
}

static void draw_meter(uint32_t x, uint32_t *row, uint32_t inner_w,
                       const char *label, uint32_t pct,
                       uint32_t fill, uint32_t track, uint32_t fg, uint32_t dim,
                       uint32_t bg, const char *detail) {
    uint32_t ch = fb_cell_h();
    fb_draw_string_fit(x, *row, inner_w, label, dim, bg);
    *row += ch + U(2);
    draw_bar(x, *row, inner_w, U(10), pct, fill, track);
    *row += U(12);
    fb_draw_string_fit(x, *row, inner_w, detail, fg, bg);
    *row += ch + U(6);
}

void monitor_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const struct peak_theme *t = theme_get();
    uint32_t bg = t->bg;
    uint32_t fg = t->fg;
    uint32_t dim = t->dim;
    uint32_t accent = t->accent;
    uint32_t surface = t->surface;
    uint32_t ch = fb_cell_h();
    uint32_t pad = U(8);
    uint32_t inner_w = w > pad * 2 ? w - pad * 2 : w;

    fb_fill_rect(x, y, w, h, bg);
    sysmon_poll();
    const struct sysmon_sample *s = sysmon_latest();

    char line[112];
    uint64_t mins = s->uptime_secs / 60;
    uint64_t secs = s->uptime_secs % 60;

    static const char *tabs[] = {"1:Over", "2:Tasks", "3:Net"};
    uint32_t tab_w = inner_w / 3;
    if (tab_w < U(40))
        tab_w = U(40);
    for (int i = 0; i < 3; i++) {
        uint32_t tx = x + pad + (uint32_t)i * tab_w;
        uint32_t tw = tab_w > U(4) ? tab_w - U(4) : tab_w;
        uint32_t tbg = (i == page) ? accent : surface;
        uint32_t tfg = (i == page) ? bg : fg;
        fb_fill_rect(tx, y + pad, tw, ch + U(4), tbg);
        fb_draw_string_fit(tx + U(4), y + pad + U(2), tw > U(8) ? tw - U(8) : tw,
                           tabs[i], tfg, tbg);
    }

    uint32_t row = y + pad + ch + U(14);
    uint32_t text_x = x + pad;

    snprintf(line, sizeof(line), "up %lum:%02lus  load %u%%  idle %u%%",
             (unsigned long)mins, (unsigned long)secs,
             (unsigned)s->load_pct, (unsigned)s->idle_pct);
    fb_draw_string_fit(text_x, row, inner_w, line, fg, bg);
    row += ch + U(2);
    {
        char cu[16], pu[16];
        sysmon_format_us(s->compose_us, cu, sizeof(cu));
        sysmon_format_us(s->present_us, pu, sizeof(pu));
        snprintf(line, sizeof(line), "fps %u  compose %s  present %s",
                 (unsigned)s->gui_fps, cu, pu);
    }
    fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
    row += ch + U(2);
    snprintf(line, sizeof(line), "peakvec %uus (last query)  audit %uus (agent tail)",
             (unsigned)s->peakvec_us, (unsigned)s->agent_audit_us);
    fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
    row += ch + U(2);
    snprintf(line, sizeof(line), "surface pressure %u%%  tasks %u",
             (unsigned)s->surf_pressure, (unsigned)s->tasks);
    fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
    row += ch + U(2);
    snprintf(line, sizeof(line), "mem pages %u/%u  heap blocks %lu",
             (unsigned)s->mem_used_pages, (unsigned)s->mem_total_pages,
             (unsigned long)s->heap_blocks);
    fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
    row += ch + U(6);

    if (page == MON_PAGE_OVERVIEW) {
        char mb[24], tb[24], pk[24], ht[24];

        sysmon_format_bytes(s->mem_used_pages * 4096ull, mb, sizeof(mb));
        sysmon_format_bytes(s->mem_total_pages * 4096ull, tb, sizeof(tb));
        sysmon_format_bytes(s->mem_peak_pages * 4096ull, pk, sizeof(pk));
        snprintf(line, sizeof(line), "%s / %s   peak %s", mb, tb, pk);
        draw_meter(text_x, &row, inner_w, "Memory", s->mem_pct, accent, surface,
                   fg, dim, bg, line);
        sysmon_format_bytes((s->mem_total_pages - s->mem_used_pages) * 4096ull,
                            tb, sizeof(tb));
        snprintf(line, sizeof(line), "used %s  free %s  peak %s  (%lu/%lu pg)",
                 mb, tb, pk,
                 (unsigned long)s->mem_used_pages,
                 (unsigned long)s->mem_total_pages);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(4);

        sysmon_format_bytes(s->heap_used, mb, sizeof(mb));
        sysmon_format_bytes(s->heap_free, tb, sizeof(tb));
        sysmon_format_bytes(s->heap_peak, pk, sizeof(pk));
        sysmon_format_bytes(s->heap_used + s->heap_free, ht, sizeof(ht));
        snprintf(line, sizeof(line), "%s / %s   peak %s", mb, ht, pk);
        draw_meter(text_x, &row, inner_w, "Heap", s->heap_pct, t->danger, surface,
                   fg, dim, bg, line);
        snprintf(line, sizeof(line), "used %s  free %s  peak %s  (%lu blk)",
                 mb, tb, pk, (unsigned long)s->heap_blocks);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        {
            struct heap_freelist_stats fl;
            heap_get_freelist_stats(&fl);
            char lf[24];
            sysmon_format_bytes(fl.largest_free, lf, sizeof(lf));
            snprintf(line, sizeof(line), "frag %u%%  free blk %u  oom %u  largest %s",
                     (unsigned)s->heap_frag_pct, (unsigned)s->heap_free_blocks,
                     (unsigned)s->heap_oom, lf);
            fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        }
        row += ch + U(2);

        snprintf(line, sizeof(line), "ctx %lu  irq %lu",
                 (unsigned long)s->ctx_switches, (unsigned long)s->irq_count);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        snprintf(line, sizeof(line), "vfs %lu  heap blocks %lu",
                 (unsigned long)s->vfs_nodes, (unsigned long)s->heap_blocks);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        snprintf(line, sizeof(line), "heap frag %u%%  tcp %d  stacks freed %d",
                 (unsigned)heap_fragmentation_pct(),
                 net_tcp_active_count(),
                 sched_zombie_stacks_freed());
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        {
            uint32_t jt = 0, jo = 0, jtm = 0, jg = 0;
            browser_js_metrics(&jt, &jo, &jtm, &jg);
            snprintf(line, sizeof(line), "js tabs %u  objs %u  timers %u  gc %u",
                     (unsigned)jt, (unsigned)jo, (unsigned)jtm, (unsigned)jg);
        }
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(8);

        int hn = sysmon_history(g_hist, SYSMON_HISTORY);
        for (int i = 0; i < hn; i++) {
            g_series_a[i] = g_hist[i].load_pct;
            g_series_b[i] = g_hist[i].mem_pct;
            g_series_c[i] = g_hist[i].gui_fps;
        }
        uint32_t graph_h = U(36);
        uint32_t graph_w = inner_w;
        uint32_t foot = y + h - ch - pad;
        uint32_t smin, smax;

        snprintf(line, sizeof(line), "Load %%  %s", sysmon_sparkline_legend());
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        sysmon_sparkline_range(g_series_a, hn, &smin, &smax);
        snprintf(line, sizeof(line), "range %u–%u%%", (unsigned)smin, (unsigned)smax);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot) {
            draw_spark(text_x, row, graph_w, graph_h, g_series_a, hn, accent, surface);
            row += graph_h + U(4);
        }
        sysmon_sparkline_range(g_series_b, hn, &smin, &smax);
        snprintf(line, sizeof(line), "Memory %%  range %u–%u%%",
                 (unsigned)smin, (unsigned)smax);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot) {
            draw_spark(text_x, row, graph_w, graph_h, g_series_b, hn, t->danger, surface);
            row += graph_h + U(4);
        }
        sysmon_sparkline_range(g_series_c, hn, &smin, &smax);
        snprintf(line, sizeof(line), "GUI FPS  range %u–%u",
                 (unsigned)smin, (unsigned)smax);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot)
            draw_spark(text_x, row, graph_w, graph_h, g_series_c, hn, t->title, surface);

    } else if (page == MON_PAGE_TASKS) {
        fb_draw_string_fit(text_x, row, inner_w,
                           "PID  STATE   TICKS   AGE     SHARE  NAME", dim, bg);
        row += ch + U(4);
        int n = sched_list_tasks(g_tasks, MAX_TASKS);
        sched_sort_tasks(g_tasks, n);
        int cur = sched_current_pid();
        uint64_t total_ticks = 0;
        for (int i = 0; i < n; i++)
            total_ticks += g_tasks[i].cpu_ticks;
        if (!total_ticks)
            total_ticks = 1;
        uint64_t now = timer_ticks();
        for (int i = 0; i < n && row + ch < y + h - ch; i++) {
            uint64_t age = now > g_tasks[i].spawned_at ?
                           now - g_tasks[i].spawned_at : 0;
            uint32_t share = (uint32_t)((g_tasks[i].cpu_ticks * 100ull) / total_ticks);
            snprintf(line, sizeof(line), "%-4d %-7s %-7lu %-7lu %-5u %s%s",
                     g_tasks[i].pid, state_name(g_tasks[i].state),
                     (unsigned long)g_tasks[i].cpu_ticks, (unsigned long)age,
                     (unsigned)share, g_tasks[i].name,
                     g_tasks[i].pid == cur ? " *" : "");
            fb_draw_string_fit(text_x, row, inner_w, line, fg, bg);
            row += ch + U(2);
        }
        if (n == 0) {
            fb_draw_string_fit(text_x, row, inner_w, "(no tasks)", dim, bg);
            row += ch;
        }
        row += ch;
        fb_draw_string_fit(text_x, row, inner_w,
                           "Sorted by CPU ticks (desc).", dim, bg);

    } else {
        char rxr[16], txr[16], rxt[16], txt[16];
        sysmon_format_rate(s->rx_bps, rxr, sizeof(rxr));
        sysmon_format_rate(s->tx_bps, txr, sizeof(txr));
        sysmon_format_bytes(s->rx_bytes, rxt, sizeof(rxt));
        sysmon_format_bytes(s->tx_bytes, txt, sizeof(txt));
        snprintf(line, sizeof(line), "RX %s   TX %s", rxr, txr);
        fb_draw_string_fit(text_x, row, inner_w, line, accent, bg);
        row += ch + U(4);
        snprintf(line, sizeof(line), "Total RX %s", rxt);
        fb_draw_string_fit(text_x, row, inner_w, line, fg, bg);
        row += ch + U(2);
        snprintf(line, sizeof(line), "Total TX %s", txt);
        fb_draw_string_fit(text_x, row, inner_w, line, fg, bg);
        row += ch + U(2);
        snprintf(line, sizeof(line), "Packets RX %lu",
                 (unsigned long)s->rx_packets);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        snprintf(line, sizeof(line), "Packets TX %lu",
                 (unsigned long)s->tx_packets);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(8);

        int hn = sysmon_history(g_hist, SYSMON_HISTORY);
        for (int i = 0; i < hn; i++) {
            g_series_a[i] = g_hist[i].rx_bps;
            g_series_b[i] = g_hist[i].tx_bps;
        }
        uint32_t graph_h = U(48);
        uint32_t graph_w = inner_w;
        uint32_t foot = y + h - ch - pad;
        uint32_t smin, smax;
        sysmon_sparkline_range(g_series_a, hn, &smin, &smax);
        snprintf(line, sizeof(line), "RX B/s  range %u–%u",
                 (unsigned)smin, (unsigned)smax);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot) {
            draw_spark(text_x, row, graph_w, graph_h, g_series_a, hn, accent, surface);
            row += graph_h + U(6);
        }
        sysmon_sparkline_range(g_series_b, hn, &smin, &smax);
        snprintf(line, sizeof(line), "TX B/s  range %u–%u",
                 (unsigned)smin, (unsigned)smax);
        fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot)
            draw_spark(text_x, row, graph_w, graph_h, g_series_b, hn, t->danger, surface);
    }

    uint32_t foot = y + h - ch - pad;
    uint32_t bw = U(72), bh = ch + U(4);
    uint32_t bx = text_x + (inner_w > bw ? inner_w - bw : 0);
    uint32_t by = foot > bh + U(6) ? foot - bh - U(6) : foot;
    monitor_export_btn(bx, by, bw, bh, fg, surface, accent);
    fb_draw_string_fit(text_x, foot, inner_w > bw ? inner_w - bw - U(4) : inner_w,
                       paused ? "PAUSED  P resume  1-3  R reset  E export"
                              : "1-3  P pause  R reset  E export",
                       dim, bg);
    if (mon_last_export[0])
        fb_draw_string_fit(text_x, foot + ch + U(2), inner_w, mon_last_export, dim, bg);
    needs_redraw = 0;
}

int monitor_export_click_at(int32_t mx, int32_t my) {
    if (!mon_export_w) return 0;
    if ((uint32_t)mx < mon_export_x || (uint32_t)my < mon_export_y) return 0;
    if ((uint32_t)mx >= mon_export_x + mon_export_w || (uint32_t)my >= mon_export_y + mon_export_h) return 0;
    if (monitor_do_export() == 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Saved export");
        notify_push(msg);
        return 1;
    }
    notify_push("Export failed"); return 1;
}
