#include "rtc.h"
#include "timer.h"
#include "util.h"

void rtc_init(void) {}

int rtc_read(struct rtc_time *out) {
    if (!out)
        return -1;
    uint64_t s = timer_uptime_secs();
    out->sec = (uint8_t)(s % 60);
    out->min = (uint8_t)((s / 60) % 60);
    out->hour = (uint8_t)((s / 3600) % 24);
    out->day = 1;
    out->month = 1;
    out->year = 2026;
    return 0;
}

void rtc_format_clock(char *buf, size_t cap) {
    struct rtc_time t;
    if (rtc_read(&t) != 0 || cap < 6) {
        if (cap)
            buf[0] = '\0';
        return;
    }
    snprintf(buf, cap, "%02u:%02u", (unsigned)t.hour, (unsigned)t.min);
}

void rtc_format_date(char *buf, size_t cap) {
    struct rtc_time t;
    if (rtc_read(&t) != 0 || !cap) {
        if (cap)
            buf[0] = '\0';
        return;
    }
    snprintf(buf, cap, "%04u-%02u-%02u %02u:%02u:%02u",
             (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
             (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec);
}
