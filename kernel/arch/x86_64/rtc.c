#include "rtc.h"
#include "util.h"

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static int bcd_to_bin(uint8_t v) {
    return (int)((v & 0x0F) + ((v >> 4) * 10));
}

void rtc_init(void) {
}

int rtc_read(struct rtc_time *out) {
    if (!out)
        return -1;

    uint8_t sec, min, hour, day, month, year, regb;
    /* Read twice across a stable UIP boundary. */
    for (int attempt = 0; attempt < 4; attempt++) {
        for (int i = 0; i < 10000; i++) {
            if (!(cmos_read(0x0A) & 0x80))
                break;
        }
        sec = cmos_read(0x00);
        min = cmos_read(0x02);
        hour = cmos_read(0x04);
        day = cmos_read(0x07);
        month = cmos_read(0x08);
        year = cmos_read(0x09);
        regb = cmos_read(0x0B);

        for (int i = 0; i < 10000; i++) {
            if (!(cmos_read(0x0A) & 0x80))
                break;
        }
        uint8_t sec2 = cmos_read(0x00);
        uint8_t min2 = cmos_read(0x02);
        uint8_t hour2 = cmos_read(0x04);
        if (sec == sec2 && min == min2 && hour == hour2)
            break;
    }

    int is_binary = (regb & 0x04) != 0;
    int is_24h = (regb & 0x02) != 0;
    int hour_pm = (hour & 0x80) != 0;
    uint8_t hour_raw = (uint8_t)(hour & 0x7F);

    if (!is_binary) {
        sec = (uint8_t)bcd_to_bin(sec);
        min = (uint8_t)bcd_to_bin(min);
        hour_raw = (uint8_t)bcd_to_bin(hour_raw);
        day = (uint8_t)bcd_to_bin(day);
        month = (uint8_t)bcd_to_bin(month);
        year = (uint8_t)bcd_to_bin(year);
    }

    if (!is_24h) {
        /* 12-hour mode: convert to 24-hour. */
        if (hour_pm && hour_raw < 12)
            hour_raw = (uint8_t)(hour_raw + 12);
        if (!hour_pm && hour_raw == 12)
            hour_raw = 0;
    }

    out->sec = sec;
    out->min = min;
    out->hour = hour_raw;
    out->day = day;
    out->month = month;
    out->year = (uint16_t)(2000 + year);
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
