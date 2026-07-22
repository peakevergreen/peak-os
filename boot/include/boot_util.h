#ifndef PEAK_BOOT_UTIL_H
#define PEAK_BOOT_UTIL_H

#include <stdint.h>

#ifndef _PEAK_BOOT_SIZE_T
#define _PEAK_BOOT_SIZE_T
typedef __SIZE_TYPE__ size_t;
#endif

void boot_memset(void *dst, int v, size_t n);
void boot_memcpy(void *dst, const void *src, size_t n);
int  boot_memcmp(const void *a, const void *b, size_t n);
size_t boot_strlen(const char *s);
int  boot_strncasecmp(const char *a, const char *b, size_t n);

void boot_serial_init(void);
void boot_serial_write(char c);
void boot_serial_write_str(const char *s);
void boot_serial_write_hex(uint64_t v);
void boot_hang(void);

#endif
