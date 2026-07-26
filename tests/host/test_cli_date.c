#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

struct rtc_time {
    uint8_t sec, min, hour;
    uint8_t day, month;
    uint16_t year;
};

static int rtc_is_leap(unsigned y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static uint64_t rtc_unix_secs(const struct rtc_time *t) {
    static const int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint64_t days = 0;
    for (unsigned y = 1970; y < t->year; y++)
        days += rtc_is_leap(y) ? 366u : 365u;
    for (unsigned m = 1; m < t->month; m++) {
        days += (uint64_t)mdays[m - 1];
        if (m == 2 && rtc_is_leap(t->year))
            days++;
    }
    days += (uint64_t)(t->day - 1);
    return days * 86400ull + (uint64_t)t->hour * 3600ull + (uint64_t)t->min * 60ull +
           (uint64_t)t->sec;
}

static void date_pad2(unsigned v, char *out, size_t *o) {
    out[(*o)++] = (char)('0' + (v / 10) % 10);
    out[(*o)++] = (char)('0' + v % 10);
}

static void date_format(const struct rtc_time *t, const char *fmt, char *out, size_t cap) {
    size_t o = 0;
    for (const char *p = fmt; *p && o + 1 < cap; p++) {
        if (*p != '%') {
            out[o++] = *p;
            continue;
        }
        p++;
        if (*p == 's') {
            uint64_t u = rtc_unix_secs(t);
            char tmp[24];
            snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)u);
            for (size_t i = 0; tmp[i] && o + 1 < cap; i++)
                out[o++] = tmp[i];
            continue;
        }
        if (*p == 'Y') {
            unsigned y = t->year;
            out[o++] = (char)('0' + (y / 1000) % 10);
            out[o++] = (char)('0' + (y / 100) % 10);
            out[o++] = (char)('0' + (y / 10) % 10);
            out[o++] = (char)('0' + y % 10);
            continue;
        }
        if (*p == 'm') {
            date_pad2(t->month, out, &o);
            continue;
        }
        if (*p == 'd') {
            date_pad2(t->day, out, &o);
            continue;
        }
    }
    out[o] = '\0';
}

int main(void) {
    struct rtc_time t = { .sec = 0, .min = 0, .hour = 0, .day = 2, .month = 1, .year = 1970 };
    expect(rtc_unix_secs(&t) == 86400, "1970-01-02 unix");
    char buf[32];
    date_format(&t, "%Y-%m-%d", buf, sizeof(buf));
    expect(!strcmp(buf, "1970-01-02"), "Y-m-d format");
    date_format(&t, "%s", buf, sizeof(buf));
    expect(!strcmp(buf, "86400"), "%s format");
    if (fails)
        return 1;
    printf("test_cli_date: ok\n");
    return 0;
}
