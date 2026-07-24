#include "heap.h"
#include "pmm.h"
#include "sync.h"
#include "util.h"
#include "vmm.h"

struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;      /* address-adjacent chain (stats / coalesce) */
    struct heap_block *prev;      /* bidirectional coalesce without full scan */
    struct heap_block *free_next; /* size-class / large freelist when free */
};

/*
 * Segregated free lists for small allocations (16-byte aligned).
 * Max class covers a full page payload so init/bootstrap blocks stay listed.
 */
#define HEAP_NCLASSES 9
static const size_t heap_class_size[HEAP_NCLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

static struct heap_block *blocks;
static struct heap_block *free_lists[HEAP_NCLASSES];
static struct heap_block *large_free; /* free blocks above max size class */
static struct spinlock heap_lock;

static int heap_size_class(size_t size) {
    for (int i = 0; i < HEAP_NCLASSES; i++) {
        if (size <= heap_class_size[i])
            return i;
    }
    return -1;
}

static void block_insert_head(struct heap_block *b) {
    b->prev = NULL;
    b->next = blocks;
    if (blocks)
        blocks->prev = b;
    blocks = b;
}

static void block_unlink(struct heap_block *b) {
    if (b->prev)
        b->prev->next = b->next;
    else
        blocks = b->next;
    if (b->next)
        b->next->prev = b->prev;
    b->prev = NULL;
    b->next = NULL;
}

static void freelist_push(struct heap_block *b) {
    int cls = heap_size_class(b->size);
    if (cls < 0) {
        b->free_next = large_free;
        large_free = b;
        return;
    }
    b->free_next = free_lists[cls];
    free_lists[cls] = b;
}

static void freelist_remove(struct heap_block *b) {
    int cls = heap_size_class(b->size);
    struct heap_block **pp = (cls < 0) ? &large_free : &free_lists[cls];
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
    large_free = NULL;
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
        block_insert_head(b);
        freelist_push(b);
    }
}

static void *take_free_block(struct heap_block *b, size_t size) {
    if (b->size >= size + sizeof(struct heap_block) + 64) {
        uint8_t *split_at = (uint8_t *)(b + 1) + size;
        struct heap_block *n = (struct heap_block *)split_at;
        n->size = b->size - size - sizeof(struct heap_block);
        n->free = 1;
        n->free_next = NULL;
        n->prev = b;
        n->next = b->next;
        if (b->next)
            b->next->prev = n;
        b->next = n;
        b->size = size;
        freelist_push(n);
    }
    b->free = 0;
    b->free_next = NULL;
    return (void *)(b + 1);
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
    block_insert_head(b);
    return (void *)(b + 1);
}

/* First-fit on the oversized freelist; split leftovers back into classes. */
static void *alloc_from_large(size_t size) {
    struct heap_block **pp = &large_free;
    while (*pp) {
        struct heap_block *b = *pp;
        if (b->size >= size) {
            *pp = b->free_next;
            b->free_next = NULL;
            return take_free_block(b, size);
        }
        pp = &b->free_next;
    }
    return NULL;
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
            void *ret = take_free_block(b, size);
            spin_unlock(&heap_lock);
            return ret;
        }
    }

    /* Oversized freelist (coalesced leftovers or prior large frees). */
    void *from_large = alloc_from_large(size);
    if (from_large) {
        spin_unlock(&heap_lock);
        return from_large;
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

/*
 * Merge physically adjacent free neighbors of b. b itself is free but not yet
 * on a freelist; neighbors that merge are removed from theirs. Returns the
 * surviving block (still not on a freelist).
 */
static struct heap_block *heap_coalesce_block(struct heap_block *b) {
    while (b->next && b->next->free) {
        uint8_t *end = (uint8_t *)(b + 1) + b->size;
        if (end != (uint8_t *)b->next)
            break;
        struct heap_block *n = b->next;
        freelist_remove(n);
        b->size += sizeof(struct heap_block) + n->size;
        b->next = n->next;
        if (n->next)
            n->next->prev = b;
    }
    while (b->prev && b->prev->free) {
        uint8_t *end = (uint8_t *)(b->prev + 1) + b->prev->size;
        if (end != (uint8_t *)b)
            break;
        struct heap_block *p = b->prev;
        freelist_remove(p);
        p->size += sizeof(struct heap_block) + b->size;
        p->next = b->next;
        if (b->next)
            b->next->prev = p;
        b = p;
    }
    return b;
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
    if (b->free) {
        /* Double-free: leave heap unchanged. */
        spin_unlock(&heap_lock);
        return;
    }
    size_t total = b->size + sizeof(*b);
    if (total > PAGE_SIZE) {
        block_unlink(b);
        size_t npages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
        pmm_free_n((void *)vmm_virt_to_phys(b), npages);
        spin_unlock(&heap_lock);
        return;
    }
    b->free = 1;
    b->free_next = NULL;
    /* Coalesce before freelist insert so size-class is final. */
    b = heap_coalesce_block(b);
    freelist_push(b);
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
