#include "heap.h"
#include "pmm.h"
#include "sync.h"
#include "util.h"
#include "vmm.h"

struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;      /* all blocks (stats / coalesce) */
    struct heap_block *free_next; /* size-class freelist when free */
};

/* Segregated free lists for small allocations (16-byte aligned). */
#define HEAP_NCLASSES 9
static const size_t heap_class_size[HEAP_NCLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4032
};

static struct heap_block *blocks;
static struct heap_block *free_lists[HEAP_NCLASSES];
static struct spinlock heap_lock;

static int heap_size_class(size_t size) {
    for (int i = 0; i < HEAP_NCLASSES; i++) {
        if (size <= heap_class_size[i])
            return i;
    }
    return -1;
}

static void freelist_push(struct heap_block *b) {
    int cls = heap_size_class(b->size);
    if (cls < 0) {
        b->free_next = NULL;
        return;
    }
    b->free_next = free_lists[cls];
    free_lists[cls] = b;
}

static void freelist_remove(struct heap_block *b) {
    int cls = heap_size_class(b->size);
    if (cls < 0)
        return;
    struct heap_block **pp = &free_lists[cls];
    while (*pp) {
        if (*pp == b) {
            *pp = b->free_next;
            b->free_next = NULL;
            return;
        }
        pp = &(*pp)->free_next;
    }
}

void heap_init(void) {
    spin_init(&heap_lock, "heap");
    blocks = NULL;
    for (int i = 0; i < HEAP_NCLASSES; i++)
        free_lists[i] = NULL;
    for (int i = 0; i < 128; i++) {
        void *phys = pmm_alloc();
        if (!phys)
            break;
        struct heap_block *b = (struct heap_block *)vmm_phys_to_virt((uint64_t)phys);
        b->size = 4096 - sizeof(struct heap_block);
        b->free = 1;
        b->free_next = NULL;
        b->next = blocks;
        blocks = b;
        freelist_push(b);
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
    b->free_next = NULL;
    b->next = blocks;
    blocks = b;
    return (void *)(b + 1);
}

void *kmalloc(size_t size) {
    if (size == 0)
        return NULL;
    size = (size + 15) & ~(size_t)15;

    spin_lock(&heap_lock);

    int cls = heap_size_class(size);
    if (cls >= 0) {
        /* Exact class first, then larger classes (split leftover). */
        for (int c = cls; c < HEAP_NCLASSES; c++) {
            struct heap_block *b = free_lists[c];
            if (!b)
                continue;
            free_lists[c] = b->free_next;
            b->free_next = NULL;
            if (b->size >= size + sizeof(struct heap_block) + 64) {
                uint8_t *split_at = (uint8_t *)(b + 1) + size;
                struct heap_block *n = (struct heap_block *)split_at;
                n->size = b->size - size - sizeof(struct heap_block);
                n->free = 1;
                n->free_next = NULL;
                n->next = b->next;
                b->next = n;
                b->size = size;
                freelist_push(n);
            }
            b->free = 0;
            void *ret = (void *)(b + 1);
            spin_unlock(&heap_lock);
            return ret;
        }
    }

    /* Large request or empty freelists: grow from PMM. */
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
            freelist_remove(b);
            freelist_remove(b->next);
            b->size += sizeof(struct heap_block) + b->next->size;
            b->next = b->next->next;
            freelist_push(b);
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
    freelist_push(b);
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
