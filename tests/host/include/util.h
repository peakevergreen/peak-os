#ifndef PEAK_UTIL_H
#define PEAK_UTIL_H

#include <types.h>

void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
int   snprintf(char *buf, size_t size, const char *fmt, ...);
void  itoa_u(uint64_t val, char *buf, int base);

#endif
