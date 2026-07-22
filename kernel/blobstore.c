#include "blobstore.h"
#include "blockdev.h"
#include "heap.h"
#include "util.h"
#include "cap.h"

#define BLOB_MAGIC "PEAKBLOB"
/* super (1 sector) + object table (256 objs * 20 B = 10 sectors), padded. */
#define BLOB_META_SECTORS 16u
#define BLOB_DATA_LBA (BLOBSTORE_LBA_BASE + BLOB_META_SECTORS)
#define BLOB_MAX_PAGES 65536u /* 256 MiB of blob data at 4 KiB pages */

struct blob_obj {
    uint32_t id;
    uint32_t start_page;
    uint32_t npages;
    uint32_t size;
    uint8_t  in_use;
    uint8_t  _pad[3];
};

struct blob_super {
    char     magic[8];
    uint32_t version;
    uint32_t nobjects;
    uint32_t next_id;
    uint32_t next_page;
    uint32_t total_pages;
    uint32_t _reserved;
};

struct cache_page {
    uint32_t global_page; /* absolute page index in blob data area */
    uint8_t  data[BLOBSTORE_PAGE_SIZE];
    uint8_t  valid;
    uint8_t  dirty;
    uint32_t lru;
};

static struct blob_obj objects[BLOBSTORE_MAX_OBJECTS];
static struct blob_super super;
static struct cache_page cache[BLOBSTORE_CACHE_PAGES];
static uint32_t lru_tick;
static int ready;
/* Open-addressed map: global_page -> cache slot (0xFF = empty). */
#define CACHE_HASH_SIZE 64u
static uint8_t cache_hash[CACHE_HASH_SIZE];

static uint32_t cache_hash_slot(uint32_t global_page) {
    return global_page & (CACHE_HASH_SIZE - 1u);
}

static void cache_hash_clear(void) {
    memset(cache_hash, 0xFF, sizeof(cache_hash));
}

static void cache_hash_insert(uint32_t global_page, uint32_t idx) {
    uint32_t h = cache_hash_slot(global_page);
    for (uint32_t n = 0; n < CACHE_HASH_SIZE; n++) {
        uint32_t s = (h + n) & (CACHE_HASH_SIZE - 1u);
        if (cache_hash[s] == 0xFF) {
            cache_hash[s] = (uint8_t)idx;
            return;
        }
    }
}

static void cache_hash_remove(uint32_t global_page, uint32_t idx) {
    uint32_t h = cache_hash_slot(global_page);
    for (uint32_t n = 0; n < CACHE_HASH_SIZE; n++) {
        uint32_t s = (h + n) & (CACHE_HASH_SIZE - 1u);
        if (cache_hash[s] == 0xFF)
            return;
        if (cache_hash[s] == (uint8_t)idx) {
            cache_hash[s] = 0xFF;
            /* Linear probe deletion: reinsert following cluster. */
            uint32_t next = (s + 1) & (CACHE_HASH_SIZE - 1u);
            while (cache_hash[next] != 0xFF) {
                uint8_t re = cache_hash[next];
                cache_hash[next] = 0xFF;
                if (re < BLOBSTORE_CACHE_PAGES && cache[re].valid)
                    cache_hash_insert(cache[re].global_page, re);
                next = (next + 1) & (CACHE_HASH_SIZE - 1u);
            }
            return;
        }
    }
}

static int cache_hash_find(uint32_t global_page) {
    uint32_t h = cache_hash_slot(global_page);
    for (uint32_t n = 0; n < CACHE_HASH_SIZE; n++) {
        uint32_t s = (h + n) & (CACHE_HASH_SIZE - 1u);
        uint8_t idx = cache_hash[s];
        if (idx == 0xFF)
            return -1;
        if (idx < BLOBSTORE_CACHE_PAGES && cache[idx].valid &&
            cache[idx].global_page == global_page)
            return (int)idx;
    }
    return -1;
}

static uint64_t page_to_lba(uint32_t page) {
    return (uint64_t)BLOB_DATA_LBA +
           (uint64_t)page * (BLOBSTORE_PAGE_SIZE / BLOCKDEV_SECTOR_SIZE);
}

static int write_meta(void) {
    uint8_t sector[BLOCKDEV_SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    memcpy(sector, &super, sizeof(super));
    if (blockdev_write(BLOBSTORE_LBA_BASE, 1, sector) != 0)
        return -1;
    /* Object table: pack into following sectors. */
    size_t need = sizeof(objects);
    uint32_t sectors = (uint32_t)((need + BLOCKDEV_SECTOR_SIZE - 1) / BLOCKDEV_SECTOR_SIZE);
    if (sectors + 1 > BLOB_META_SECTORS)
        return -1;
    uint8_t *buf = kmalloc(sectors * BLOCKDEV_SECTOR_SIZE);
    if (!buf)
        return -1;
    memset(buf, 0, sectors * BLOCKDEV_SECTOR_SIZE);
    memcpy(buf, objects, sizeof(objects));
    int r = blockdev_write(BLOBSTORE_LBA_BASE + 1, sectors, buf);
    kfree(buf);
    return r;
}

static int read_meta(void) {
    uint8_t sector[BLOCKDEV_SECTOR_SIZE];
    if (blockdev_read(BLOBSTORE_LBA_BASE, 1, sector) != 0)
        return -1;
    memcpy(&super, sector, sizeof(super));
    if (memcmp(super.magic, BLOB_MAGIC, 8) != 0)
        return -1;
    if (super.version != 1 || super.total_pages == 0 ||
        super.total_pages > BLOB_MAX_PAGES)
        return -1;
    size_t need = sizeof(objects);
    uint32_t sectors = (uint32_t)((need + BLOCKDEV_SECTOR_SIZE - 1) / BLOCKDEV_SECTOR_SIZE);
    uint8_t *buf = kmalloc(sectors * BLOCKDEV_SECTOR_SIZE);
    if (!buf)
        return -1;
    if (blockdev_read(BLOBSTORE_LBA_BASE + 1, sectors, buf) != 0) {
        kfree(buf);
        return -1;
    }
    memcpy(objects, buf, sizeof(objects));
    kfree(buf);
    return 0;
}

static void format_new(void) {
    memset(&super, 0, sizeof(super));
    memcpy(super.magic, BLOB_MAGIC, 8);
    super.version = 1;
    super.nobjects = 0;
    super.next_id = 1;
    super.next_page = 0;
    super.total_pages = BLOB_MAX_PAGES;
    memset(objects, 0, sizeof(objects));
    (void)write_meta();
}

static int flush_page(struct cache_page *cp) {
    if (!cp->valid || !cp->dirty)
        return 0;
    uint32_t secs = BLOBSTORE_PAGE_SIZE / BLOCKDEV_SECTOR_SIZE;
    if (blockdev_write(page_to_lba(cp->global_page), secs, cp->data) != 0)
        return -1;
    cp->dirty = 0;
    return 0;
}

static struct cache_page *cache_evict(void) {
    struct cache_page *victim = &cache[0];
    uint32_t victim_i = 0;
    for (uint32_t i = 1; i < BLOBSTORE_CACHE_PAGES; i++) {
        if (!cache[i].valid) {
            victim = &cache[i];
            victim_i = i;
            break;
        }
        if (cache[i].lru < victim->lru) {
            victim = &cache[i];
            victim_i = i;
        }
    }
    if (victim->valid) {
        if (victim->dirty)
            (void)flush_page(victim);
        cache_hash_remove(victim->global_page, victim_i);
    }
    victim->valid = 0;
    victim->dirty = 0;
    return victim;
}

static struct cache_page *cache_get(uint32_t global_page, int for_write) {
    int hit = cache_hash_find(global_page);
    if (hit >= 0) {
        cache[hit].lru = ++lru_tick;
        return &cache[hit];
    }
    struct cache_page *cp = cache_evict();
    uint32_t idx = (uint32_t)(cp - cache);
    cp->global_page = global_page;
    uint32_t secs = BLOBSTORE_PAGE_SIZE / BLOCKDEV_SECTOR_SIZE;
    if (blockdev_read(page_to_lba(global_page), secs, cp->data) != 0) {
        if (!for_write)
            return NULL;
        memset(cp->data, 0, BLOBSTORE_PAGE_SIZE);
    }
    cp->valid = 1;
    cp->dirty = 0;
    cp->lru = ++lru_tick;
    cache_hash_insert(global_page, idx);
    return cp;
}

static struct blob_obj *find_obj(uint32_t id) {
    for (uint32_t i = 0; i < BLOBSTORE_MAX_OBJECTS; i++) {
        if (objects[i].in_use && objects[i].id == id)
            return &objects[i];
    }
    return NULL;
}

void blobstore_init(void) {
    cache_hash_clear();
    memset(objects, 0, sizeof(objects));
    memset(cache, 0, sizeof(cache));
    memset(&super, 0, sizeof(super));
    lru_tick = 0;
    ready = 0;
    if (!blockdev_present())
        return;
    if (read_meta() != 0)
        format_new();
    ready = 1;
}

int blobstore_available(void) {
    return ready && blockdev_present();
}

int blobstore_create(uint32_t *out_id, size_t size) {
    if (!blobstore_available() || !out_id)
        return -1;
    if (!cap_check(CAP_DISK_PERSIST) && !cap_check(CAP_FS_WRITE))
        return -1;
    uint32_t npages = (uint32_t)((size + BLOBSTORE_PAGE_SIZE - 1) / BLOBSTORE_PAGE_SIZE);
    if (npages == 0)
        npages = 1;
    if (super.next_page + npages > super.total_pages)
        return -1;
    struct blob_obj *slot = NULL;
    for (uint32_t i = 0; i < BLOBSTORE_MAX_OBJECTS; i++) {
        if (!objects[i].in_use) {
            slot = &objects[i];
            break;
        }
    }
    if (!slot)
        return -1;
    slot->id = super.next_id++;
    slot->start_page = super.next_page;
    slot->npages = npages;
    slot->size = (uint32_t)size;
    slot->in_use = 1;
    super.next_page += npages;
    super.nobjects++;
    *out_id = slot->id;
    (void)write_meta();
    return 0;
}

int blobstore_delete(uint32_t id) {
    struct blob_obj *o = find_obj(id);
    if (!o)
        return -1;
    /* Simple bump allocator: mark free but do not reclaim pages yet. */
    o->in_use = 0;
    if (super.nobjects)
        super.nobjects--;
    (void)write_meta();
    return 0;
}

int blobstore_resize(uint32_t id, size_t new_size) {
    struct blob_obj *o = find_obj(id);
    if (!o)
        return -1;
    uint32_t need = (uint32_t)((new_size + BLOBSTORE_PAGE_SIZE - 1) / BLOBSTORE_PAGE_SIZE);
    if (need == 0)
        need = 1;
    if (need <= o->npages) {
        o->size = (uint32_t)new_size;
        (void)write_meta();
        return 0;
    }
    /* Grow only if object is at the tip of the bump allocator. */
    if (o->start_page + o->npages != super.next_page)
        return -1;
    uint32_t extra = need - o->npages;
    if (super.next_page + extra > super.total_pages)
        return -1;
    o->npages = need;
    o->size = (uint32_t)new_size;
    super.next_page += extra;
    (void)write_meta();
    return 0;
}

size_t blobstore_size(uint32_t id) {
    struct blob_obj *o = find_obj(id);
    return o ? o->size : 0;
}

int blobstore_read(uint32_t id, size_t off, void *buf, size_t len) {
    struct blob_obj *o = find_obj(id);
    if (!o || !buf)
        return -1;
    if (off >= o->size)
        return 0;
    if (off + len > o->size)
        len = o->size - off;
    uint8_t *dst = buf;
    size_t done = 0;
    while (done < len) {
        size_t pos = off + done;
        uint32_t page = o->start_page + (uint32_t)(pos / BLOBSTORE_PAGE_SIZE);
        size_t page_off = pos % BLOBSTORE_PAGE_SIZE;
        size_t chunk = BLOBSTORE_PAGE_SIZE - page_off;
        if (chunk > len - done)
            chunk = len - done;
        struct cache_page *cp = cache_get(page, 0);
        if (!cp)
            return -1;
        memcpy(dst + done, cp->data + page_off, chunk);
        done += chunk;
    }
    return (int)done;
}

int blobstore_write(uint32_t id, size_t off, const void *buf, size_t len) {
    struct blob_obj *o = find_obj(id);
    if (!o || !buf)
        return -1;
    if (off + len > o->size) {
        if (blobstore_resize(id, off + len) != 0)
            return -1;
        o = find_obj(id);
        if (!o)
            return -1;
    }
    const uint8_t *src = buf;
    size_t done = 0;
    while (done < len) {
        size_t pos = off + done;
        uint32_t page = o->start_page + (uint32_t)(pos / BLOBSTORE_PAGE_SIZE);
        size_t page_off = pos % BLOBSTORE_PAGE_SIZE;
        size_t chunk = BLOBSTORE_PAGE_SIZE - page_off;
        if (chunk > len - done)
            chunk = len - done;
        struct cache_page *cp = cache_get(page, 1);
        if (!cp)
            return -1;
        memcpy(cp->data + page_off, src + done, chunk);
        cp->dirty = 1;
        done += chunk;
    }
    return (int)done;
}

int blobstore_sync(void) {
    if (!blobstore_available())
        return -1;
    for (uint32_t i = 0; i < BLOBSTORE_CACHE_PAGES; i++) {
        if (cache[i].valid && cache[i].dirty) {
            if (flush_page(&cache[i]) != 0)
                return -1;
        }
    }
    return write_meta();
}

int blobstore_load(void) {
    if (!blockdev_present())
        return -1;
    memset(cache, 0, sizeof(cache));
    if (read_meta() != 0) {
        format_new();
        ready = 1;
        return 0;
    }
    ready = 1;
    return 0;
}

uint32_t blobstore_object_count(void) {
    return super.nobjects;
}

uint32_t blobstore_cache_pages_used(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < BLOBSTORE_CACHE_PAGES; i++) {
        if (cache[i].valid)
            n++;
    }
    return n;
}
