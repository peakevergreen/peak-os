/*
 * Host stubs for linking kernel/gui/dom_core.c under PEAK_HOST_TEST.
 */
#include "heap.h"

#include <stdlib.h>

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
