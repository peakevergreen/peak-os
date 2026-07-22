#ifndef PEAK_X86_IO_H
#define PEAK_X86_IO_H

/*
 * x86 port I/O and interrupt flag helpers.
 * Included from util.h on __x86_64__; do not include on aarch64.
 */

#include "types.h"

#if !defined(__x86_64__)
#error "x86_io.h is only for x86_64"
#endif

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void cli(void) {
    __asm__ volatile ("cli");
}

static inline void sti(void) {
    __asm__ volatile ("sti");
}

static inline void hlt(void) {
    __asm__ volatile ("hlt");
}

static inline void hlt_if_enabled(void) {
    unsigned long flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    if (flags & (1UL << 9))
        __asm__ volatile ("hlt");
}

static inline void lidt(void *base, uint16_t size) {
    struct {
        uint16_t length;
        void    *base;
    } __attribute__((packed)) idtr = { size, base };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

#endif
