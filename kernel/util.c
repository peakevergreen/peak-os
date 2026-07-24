#include "util.h"
#include "peak_errno.h"
#include "stdarg.h"

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

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s) {
        for (size_t i = 0; i < n; i++)
            d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
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

char *strchr(const char *s, int c) {
    for (; *s; s++)
        if ((unsigned char)*s == (unsigned char)c)
            return (char *)s;
    return c == 0 ? (char *)s : NULL;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle)
        return NULL;
    if (!needle[0])
        return (char *)haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (strncmp(p, needle, nlen) == 0)
            return (char *)p;
    }
    return NULL;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    if (!buf || size == 0)
        return 0;
    va_list ap;
    va_start(ap, fmt);
    size_t o = 0;
    for (const char *p = fmt; *p && o + 1 < size; p++) {
        if (*p != '%') {
            buf[o++] = *p;
            continue;
        }
        p++;
        /* Optional zero-pad width, e.g. %02u. */
        int pad_zero = 0;
        int width = 0;
        if (*p == '0') {
            pad_zero = 1;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (*s && o + 1 < size)
                buf[o++] = *s++;
        } else if (*p == 'd' || *p == 'u') {
            uint64_t v = (*p == 'd') ? (uint64_t)(int64_t)va_arg(ap, int)
                                     : (uint64_t)va_arg(ap, unsigned);
            char tmp[24];
            itoa_u(v, tmp, 10);
            int len = 0;
            while (tmp[len])
                len++;
            for (int w = len; w < width && o + 1 < size; w++)
                buf[o++] = pad_zero ? '0' : ' ';
            for (char *t = tmp; *t && o + 1 < size; t++)
                buf[o++] = *t;
        } else if (*p == 'l' && *(p + 1) == 'u') {
            p++;
            uint64_t v = va_arg(ap, uint64_t);
            char tmp[24];
            itoa_u(v, tmp, 10);
            for (char *t = tmp; *t && o + 1 < size; t++)
                buf[o++] = *t;
        } else if (*p == 'x' || *p == 'X') {
            uint64_t v = (uint64_t)va_arg(ap, unsigned);
            char tmp[24];
            itoa_u(v, tmp, 16);
            int len = 0;
            while (tmp[len])
                len++;
            for (int w = len; w < width && o + 1 < size; w++)
                buf[o++] = pad_zero ? '0' : ' ';
            for (char *t = tmp; *t && o + 1 < size; t++) {
                char c = *t;
                if (*p == 'X' && c >= 'a' && c <= 'f')
                    c = (char)(c - 'a' + 'A');
                buf[o++] = c;
            }
        } else if (*p == '%') {
            buf[o++] = '%';
        } else {
            buf[o++] = '%';
            if (o + 1 < size)
                buf[o++] = *p;
        }
    }
    buf[o] = '\0';
    va_end(ap);
    return (int)o;
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

const char *peak_strerror(int code) {
    switch (code) {
    case PEAK_OK:           return "ok";
    case PEAK_EINVAL:       return "invalid argument";
    case PEAK_ENOENT:       return "not found";
    case PEAK_ENOMEM:       return "out of memory";
    case PEAK_EEXIST:       return "already exists";
    case PEAK_ENOTDIR:      return "not a directory";
    case PEAK_EISDIR:       return "is a directory";
    case PEAK_ENOSPC:       return "no space";
    case PEAK_EIO:          return "I/O error";
    case PEAK_EACCES:       return "permission denied";
    case PEAK_ETIMEOUT:     return "timed out";
    case PEAK_ENETDOWN:     return "network down";
    case PEAK_ENOTCONN:     return "not connected";
    case PEAK_ENOBUFS:      return "buffer too small";
    case PEAK_ENETUNREACH:  return "host unreachable (ARP)";
    case PEAK_EBUSY:        return "connection table full";
    case PEAK_EDHCP:        return "DHCP failed";
    case PEAK_EAGAIN:       return "try again";
    default:                return "unknown error";
    }
}

void reboot(void) {
#if defined(__x86_64__)
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
#else
    extern void platform_reboot(void);
    platform_reboot();
#endif
    for (;;)
        hlt();
}
