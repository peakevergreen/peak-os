#include "heap.h"
#include "pmm.h"
#include "sync.h"
#include "util.h"
#include "vmm.h"

struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;
};

static struct heap_block *blocks;
static struct spinlock heap_lock;

void heap_init(void) {
    spin_init(&heap_lock, "heap");
    blocks = NULL;
    for (int i = 0; i < 128; i++) {
        void *phys = pmm_alloc();
        if (!phys)
            break;
        struct heap_block *b = (struct heap_block *)vmm_phys_to_virt((uint64_t)phys);
        b->size = 4096 - sizeof(struct heap_block);
        b->free = 1;
        b->next = blocks;
        blocks = b;
    }
}

static void *alloc_pages_block(size_t size) {
    size_t total = size + sizeof(struct heap_block);
    size_t npages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    void *phys = pmm_alloc_pages(npages);
    if (!phys)
        return NULL;
    struct heap_block *b = (struct heap_block *)vmm_phys_to_virt((uint64_t)phys);
    b->size = npages * (size_t)PAGE_SIZE - sizeof(struct heap_block);
    b->free = 0;
    b->next = blocks;
    blocks = b;
    return (void *)(b + 1);
}

void *kmalloc(size_t size) {
    if (size == 0)
        return NULL;
    size = (size + 15) & ~(size_t)15;

    spin_lock(&heap_lock);
    for (struct heap_block *b = blocks; b; b = b->next) {
        if (b->free && b->size >= size) {
            if (b->size >= size + sizeof(struct heap_block) + 64) {
                uint8_t *split_at = (uint8_t *)(b + 1) + size;
                struct heap_block *n = (struct heap_block *)split_at;
                n->size = b->size - size - sizeof(struct heap_block);
                n->free = 1;
                n->next = b->next;
                b->next = n;
                b->size = size;
            }
            b->free = 0;
            void *ret = (void *)(b + 1);
            spin_unlock(&heap_lock);
            return ret;
        }
    }
    void *ret = alloc_pages_block(size);
    spin_unlock(&heap_lock);
    return ret;
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    size = (size + 15) & ~(size_t)15;
    struct heap_block *b = ((struct heap_block *)ptr) - 1;
    if (b->size >= size)
        return ptr;
    void *n = kmalloc(size);
    if (!n)
        return NULL;
    size_t copy = b->size < size ? b->size : size;
    memcpy(n, ptr, copy);
    kfree(ptr);
    return n;
}

uint64_t heap_total_allocated(void) {
    uint64_t used = 0, freeb = 0, n = 0;
    heap_get_stats(&used, &freeb, &n);
    (void)freeb;
    (void)n;
    return used;
}

static void heap_coalesce(void) {
    for (struct heap_block *b = blocks; b; b = b->next) {
        while (b->free && b->next && b->next->free) {
            uint8_t *end = (uint8_t *)(b + 1) + b->size;
            if (end != (uint8_t *)b->next)
                break;
            b->size += sizeof(struct heap_block) + b->next->size;
            b->next = b->next->next;
        }
    }
}

void kfree_sensitive(void *ptr, size_t len) {
    if (!ptr)
        return;
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0; i < len; i++)
        p[i] = 0;
    __asm__ volatile ("" ::: "memory");
    kfree(ptr);
}

void kfree(void *ptr) {
    if (!ptr)
        return;
    spin_lock(&heap_lock);
    struct heap_block *b = ((struct heap_block *)ptr) - 1;
    size_t total = b->size + sizeof(*b);
    if (total > PAGE_SIZE) {
        if (blocks == b) {
            blocks = b->next;
        } else {
            for (struct heap_block *p = blocks; p; p = p->next) {
                if (p->next == b) {
                    p->next = b->next;
                    break;
                }
            }
        }
        size_t npages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
        pmm_free_n((void *)vmm_virt_to_phys(b), npages);
        spin_unlock(&heap_lock);
        return;
    }
    b->free = 1;
    heap_coalesce();
    spin_unlock(&heap_lock);
}

uint32_t heap_fragmentation_pct(void) {
    spin_lock(&heap_lock);
    uint64_t freeb = 0, largest = 0;
    int free_n = 0;
    for (struct heap_block *b = blocks; b; b = b->next) {
        if (!b->free)
            continue;
        free_n++;
        freeb += b->size;
        if (b->size > largest)
            largest = b->size;
    }
    spin_unlock(&heap_lock);
    if (freeb == 0 || free_n <= 1)
        return 0;
    /* High when free space is split across many small blocks. */
    uint32_t util = (uint32_t)((largest * 100) / freeb);
    return util < 100 ? 100 - util : 0;
}

void heap_get_stats(uint64_t *used_bytes, uint64_t *free_bytes, uint64_t *blocks_out) {
    spin_lock(&heap_lock);
    uint64_t used = 0, freeb = 0, n = 0;
    for (struct heap_block *b = blocks; b; b = b->next) {
        n++;
        if (b->free)
            freeb += b->size;
        else
            used += b->size;
    }
    spin_unlock(&heap_lock);
    if (used_bytes)
        *used_bytes = used;
    if (free_bytes)
        *free_bytes = freeb;
    if (blocks_out)
        *blocks_out = n;
}
