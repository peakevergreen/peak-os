#ifndef PEAK_SYSMON_H
#define PEAK_SYSMON_H
#include "types.h"
#define SYSMON_HISTORY   96
#define SYSMON_EXPORT_PATH "/tmp/sysmon.txt"
#define SYSMON_SPARK_COLS  48
struct sysmon_sample {
    uint64_t ticks; uint64_t uptime_secs;
    uint64_t mem_used_pages; uint64_t mem_total_pages; uint64_t mem_peak_pages;
    uint64_t heap_used; uint64_t heap_free; uint64_t heap_blocks; uint64_t heap_peak;
    uint32_t heap_frag_pct; uint32_t heap_free_blocks; uint32_t heap_oom;
    uint64_t vfs_nodes; uint64_t rx_bytes; uint64_t tx_bytes;
    uint64_t rx_packets; uint64_t tx_packets; uint64_t ctx_switches; uint64_t irq_count;
    uint32_t tasks; uint32_t mem_pct; uint32_t heap_pct; uint32_t idle_pct; uint32_t load_pct;
    uint32_t rx_bps; uint32_t tx_bps; uint32_t gui_fps; uint32_t compose_us; uint32_t present_us;
    uint32_t surf_pressure; uint32_t peakvec_us; uint32_t agent_audit_us;
};
void sysmon_init(void); void sysmon_idle_enter(void); void sysmon_idle_leave(void);
void sysmon_poll(void); void sysmon_reset_history(void); void sysmon_note_frame(void);
void sysmon_note_compose_us(uint32_t us); void sysmon_note_present_us(uint32_t us);
void sysmon_note_surf_pressure(uint32_t pct); void sysmon_note_peakvec_us(uint32_t us);
void sysmon_note_agent_audit_us(uint32_t us); uint32_t sysmon_now_us(void);
const struct sysmon_sample *sysmon_latest(void);
int sysmon_history(struct sysmon_sample *out, int max); int sysmon_history_len(void);
void sysmon_format_rate(uint32_t bps, char *buf, size_t cap);
void sysmon_format_bytes(uint64_t n, char *buf, size_t cap);
void sysmon_format_us(uint32_t us, char *buf, size_t cap);
void sysmon_format_bar(char *buf, size_t cap, uint32_t pct, int width);
void sysmon_sparkline(const uint32_t *series, int n, char *out, int out_cols);
const char *sysmon_sparkline_legend(void);
void sysmon_sparkline_range(const uint32_t *series, int n, uint32_t *out_min, uint32_t *out_max);
int sysmon_snapshot(char *buf, size_t cap); int sysmon_export(const char *path);
#endif
