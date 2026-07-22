#include "monitor.h"
#include "sysmon.h"
#include "sched.h"
#include "heap.h"
#include "net.h"
#include "browser.h"
#include "fb.h"
#include "theme.h"
#include "util.h"

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

static void draw_spark(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       const uint32_t *vals, int n, uint32_t color, uint32_t bg) {
    fb_fill_rect(x, y, w, h, bg);
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

    /* Tabs — short labels so they fit at large scale */
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
    snprintf(line, sizeof(line), "fps %u  compose %uus  present %uus",
             (unsigned)s->gui_fps, (unsigned)s->compose_us, (unsigned)s->present_us);
    fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
    row += ch + U(2);
    snprintf(line, sizeof(line), "tasks %u  surf pressure %u%%",
             (unsigned)s->tasks, (unsigned)s->surf_pressure);
    fb_draw_string_fit(text_x, row, inner_w, line, dim, bg);
    row += ch + U(8);

    if (page == MON_PAGE_OVERVIEW) {
        char mb[24], tb[24], pk[24];

        sysmon_format_bytes(s->mem_used_pages * 4096ull, mb, sizeof(mb));
        sysmon_format_bytes(s->mem_total_pages * 4096ull, tb, sizeof(tb));
        sysmon_format_bytes(s->mem_peak_pages * 4096ull, pk, sizeof(pk));
        snprintf(line, sizeof(line), "%s / %s   peak %s", mb, tb, pk);
        draw_meter(text_x, &row, inner_w, "Memory", s->mem_pct, accent, surface,
                   fg, dim, bg, line);

        sysmon_format_bytes(s->heap_used, mb, sizeof(mb));
        sysmon_format_bytes(s->heap_used + s->heap_free, tb, sizeof(tb));
        sysmon_format_bytes(s->heap_peak, pk, sizeof(pk));
        snprintf(line, sizeof(line), "%s / %s   peak %s", mb, tb, pk);
        draw_meter(text_x, &row, inner_w, "Heap", s->heap_pct, t->danger, surface,
                   fg, dim, bg, line);

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

        fb_draw_string_fit(text_x, row, inner_w, "Load %", dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot) {
            draw_spark(text_x, row, graph_w, graph_h, g_series_a, hn, accent, surface);
            row += graph_h + U(4);
        }
        fb_draw_string_fit(text_x, row, inner_w, "Memory %", dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot) {
            draw_spark(text_x, row, graph_w, graph_h, g_series_b, hn, t->danger, surface);
            row += graph_h + U(4);
        }
        fb_draw_string_fit(text_x, row, inner_w, "GUI FPS", dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot)
            draw_spark(text_x, row, graph_w, graph_h, g_series_c, hn, t->title, surface);

    } else if (page == MON_PAGE_TASKS) {
        fb_draw_string_fit(text_x, row, inner_w, "PID  STATE   TICKS   NAME", dim, bg);
        row += ch + U(4);
        int n = sched_list_tasks(g_tasks, MAX_TASKS);
        int cur = sched_current_pid();
        for (int i = 0; i < n && row + ch < y + h - ch; i++) {
            snprintf(line, sizeof(line), "%-4d %-7s %-7lu %s%s",
                     g_tasks[i].pid, state_name(g_tasks[i].state),
                     (unsigned long)g_tasks[i].cpu_ticks, g_tasks[i].name,
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
                           "Cooperative kthreads + IRQ-safe locks.", dim, bg);

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
        fb_draw_string_fit(text_x, row, inner_w, "RX B/s", dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot) {
            draw_spark(text_x, row, graph_w, graph_h, g_series_a, hn, accent, surface);
            row += graph_h + U(6);
        }
        fb_draw_string_fit(text_x, row, inner_w, "TX B/s", dim, bg);
        row += ch + U(2);
        if (row + graph_h < foot)
            draw_spark(text_x, row, graph_w, graph_h, g_series_b, hn, t->danger, surface);
    }

    uint32_t foot = y + h - ch - pad;
    fb_draw_string_fit(text_x, foot, inner_w,
                       paused ? "PAUSED  P resume  1/2/3  R reset" : "1/2/3  P pause  R reset  [/]",
                       dim, bg);
    needs_redraw = 0;
}
