#include "heap.h"
#include "timer.h"
#include <stdlib.h>
#include <string.h>

static uint64_t g_ticks;
static uint64_t g_alloc;

void *kmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (p)
        g_alloc += size;
    return p;
}

void *kzalloc(size_t size) {
    void *p = calloc(1, size ? size : 1);
    if (p)
        g_alloc += size;
    return p;
}

void *krealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size ? size : 1);
    if (p)
        g_alloc += size;
    return p;
}

void kfree(void *ptr) {
    free(ptr);
}

uint64_t heap_total_allocated(void) {
    return g_alloc;
}

uint64_t timer_ticks(void) {
    return g_ticks++;
}
