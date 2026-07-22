#include "serial.h"
#include "arch.h"
#include "timer.h"
#include "util.h"

static enum serial_log_level log_level = PEAK_SERIAL_LOG_LEVEL;
static uint64_t rl_last;
static const char *once_seen[16];
static int once_count;

void serial_init(void) {
    arch_serial_init();
}

void serial_write(char c) {
    if (c == '\n')
        arch_serial_write('\r');
    arch_serial_write(c);
}

void serial_write_str(const char *s) {
    while (*s)
        serial_write(*s++);
}

void serial_set_log_level(enum serial_log_level level) {
    log_level = level;
}

enum serial_log_level serial_get_log_level(void) {
    return log_level;
}

void serial_log(enum serial_log_level level, const char *msg) {
    if (!msg || level > log_level)
        return;
    serial_write_str(msg);
}

void serial_log_once(enum serial_log_level level, const char *key, const char *msg) {
    if (!key || !msg || level > log_level)
        return;
    for (int i = 0; i < once_count; i++) {
        if (once_seen[i] == key)
            return;
    }
    if (once_count < (int)(sizeof(once_seen) / sizeof(once_seen[0])))
        once_seen[once_count++] = key;
    serial_write_str(msg);
}

void serial_log_rl(enum serial_log_level level, uint32_t interval_ticks,
                   const char *msg) {
    if (!msg || level > log_level)
        return;
    uint64_t now = timer_ticks();
    if (interval_ticks && now - rl_last < interval_ticks)
        return;
    rl_last = now;
    serial_write_str(msg);
}

void serial_log_secret(enum serial_log_level level, const char *label,
                       const void *data, size_t len) {
#if PEAK_DEV_INSECURE_RNG
    if (!label || !data || !len || level > log_level)
        return;
    serial_write_str(label);
    serial_write_str("=");
    const uint8_t *p = data;
    char hex[3];
    for (size_t i = 0; i < len; i++) {
        hex[0] = "0123456789abcdef"[(p[i] >> 4) & 0xF];
        hex[1] = "0123456789abcdef"[p[i] & 0xF];
        hex[2] = '\0';
        serial_write_str(hex);
    }
    serial_write_str("\n");
#else
    (void)level;
    (void)label;
    (void)data;
    (void)len;
#endif
}
