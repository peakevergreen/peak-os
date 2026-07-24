#include "boot_util.h"

void boot_memset(void *dst, int v, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--)
        *d++ = (uint8_t)v;
}

void boot_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *d++ = *s++;
}

int boot_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

size_t boot_strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int boot_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return (int)ca - (int)cb;
        if (ca == 0)
            return 0;
    }
    return 0;
}

#if (defined(__x86_64__) || defined(__i386__)) && !defined(PEAK_HOST_TEST)
#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void boot_serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void boot_serial_write(char c) {
    while (!(inb(COM1 + 5) & 0x20))
        ;
    outb(COM1, (uint8_t)c);
}
#else
void boot_serial_init(void) {}
void boot_serial_write(char c) { (void)c; }
#endif

void boot_serial_write_str(const char *s) {
    while (*s)
        boot_serial_write(*s++);
}

void boot_serial_write_hex(uint64_t v) {
    static const char hex[] = "0123456789abcdef";
    boot_serial_write_str("0x");
    for (int i = 60; i >= 0; i -= 4)
        boot_serial_write(hex[(v >> i) & 0xF]);
}

void boot_hang(void) {
    for (;;) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("hlt");
#else
        __asm__ volatile("" ::: "memory");
#endif
    }
}
