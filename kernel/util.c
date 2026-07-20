#include "util.h"

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    for (size_t i = 0; i < n; i++)
        d[i] = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i] || !b[i])
            return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
    }
    return 0;
}

void itoa_u(uint64_t val, char *buf, int base) {
    if (base < 2 || base > 16) {
        buf[0] = '\0';
        return;
    }
    char tmp[32];
    int i = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val) {
        int d = (int)(val % (uint64_t)base);
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        val /= (uint64_t)base;
    }
    int j = 0;
    while (i)
        buf[j++] = tmp[--i];
    buf[j] = '\0';
}

void reboot(void) {
    /* Pulse reset via keyboard controller */
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    /* Fallback: triple fault */
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idtr = { 0, 0 };
    __asm__ volatile ("lidt %0; int $0" : : "m"(null_idtr));
    for (;;)
        hlt();
}
