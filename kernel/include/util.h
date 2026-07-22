#ifndef PEAK_UTIL_H
#define PEAK_UTIL_H

#include "types.h"

#if defined(__x86_64__)
#include "x86_io.h"
#elif defined(__aarch64__)

/* Port I/O is meaningless on aarch64; keep stubs for shared call sites. */
static inline void outb(uint16_t port, uint8_t val) {
    (void)port;
    (void)val;
}

static inline uint8_t inb(uint16_t port) {
    (void)port;
    return 0;
}

static inline void outw(uint16_t port, uint16_t val) {
    (void)port;
    (void)val;
}

static inline uint16_t inw(uint16_t port) {
    (void)port;
    return 0;
}

static inline void io_wait(void) {}

static inline void cli(void) {
    __asm__ volatile ("msr daifset, #2" ::: "memory");
}

static inline void sti(void) {
    __asm__ volatile ("msr daifclr, #2" ::: "memory");
}

static inline void hlt(void) {
    __asm__ volatile ("wfi" ::: "memory");
}

static inline void hlt_if_enabled(void) {
    uint64_t daif;
    __asm__ volatile ("mrs %0, daif" : "=r"(daif));
    if ((daif & (1ull << 7)) == 0)
        __asm__ volatile ("wfi" ::: "memory");
}

static inline void lidt(void *base, uint16_t size) {
    (void)base;
    (void)size;
}

#else
#error "Unsupported architecture"
#endif

void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void  itoa_u(uint64_t val, char *buf, int base);
int   snprintf(char *buf, size_t size, const char *fmt, ...);
void  reboot(void);

#endif
