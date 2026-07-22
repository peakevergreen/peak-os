/*
 * Host stubs for linking kernel/gui/guiproto.c + surface.c under PEAK_HOST_TEST.
 */
#include "heap.h"
#include "fb.h"
#include "serial.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

void *kmalloc(size_t size) {
    return malloc(size ? size : 1);
}

void *kzalloc(size_t size) {
    return calloc(1, size ? size : 1);
}

void *krealloc(void *ptr, size_t size) {
    return realloc(ptr, size ? size : 1);
}

void kfree(void *ptr) {
    free(ptr);
}

uint64_t heap_total_allocated(void) {
    return 0;
}

void fb_blit_argb(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  const uint32_t *src, uint32_t src_stride) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)src;
    (void)src_stride;
}
