#ifndef PEAK_RTC_H
#define PEAK_RTC_H

#include "types.h"

struct rtc_time {
    uint8_t sec, min, hour;
    uint8_t day, month;
    uint16_t year;
};

void rtc_init(void);
int  rtc_read(struct rtc_time *out);
/* Format "HH:MM" or "YYYY-MM-DD HH:MM:SS" into buf. */
void rtc_format_clock(char *buf, size_t cap);
void rtc_format_date(char *buf, size_t cap);

#endif
