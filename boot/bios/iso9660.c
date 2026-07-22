#include "bios_call.h"
#include "boot_util.h"
#include <stdint.h>

extern uint8_t boot_drive;

struct __attribute__((packed)) dap {
    uint8_t size;
    uint8_t reserved;
    uint16_t count;
    uint16_t offset;
    uint16_t segment;
    uint64_t lba;
};

static int read_sectors(uint64_t lba, void *buf, uint16_t count) {
    uintptr_t addr = (uintptr_t)buf;
    if (addr > 0x90000)
        return -1;

    static struct dap dap;
    dap.size = 16;
    dap.reserved = 0;
    dap.count = count;
    dap.offset = (uint16_t)(addr & 0xF);
    dap.segment = (uint16_t)(addr >> 4);
    dap.lba = lba;

    struct bios_regs r;
    boot_memset(&r, 0, sizeof(r));
    r.eax = 0x4200;
    r.edx = boot_drive;
    r.esi = (uint32_t)(uintptr_t)&dap;
    r.ds = 0;
    bios_int(0x13, &r);
    if (r.flags & 1)
        return -1;
    return 0;
}

static uint8_t sector[2048] __attribute__((aligned(16)));

static int read_sector(uint64_t lba) {
    return read_sectors(lba, sector, 1);
}

struct __attribute__((packed)) iso_pvd {
    uint8_t type;
    char id[5];
    uint8_t version;
    uint8_t unused1;
    char system_id[32];
    char volume_id[32];
    uint8_t unused2[8];
    uint32_t space_le;
    uint32_t space_be;
    uint8_t unused3[32];
    uint16_t set_size_le;
    uint16_t set_size_be;
    uint16_t seq_le;
    uint16_t seq_be;
    uint16_t blk_size_le;
    uint16_t blk_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t path_table_le;
    uint32_t opt_path_table_le;
    uint32_t path_table_be;
    uint32_t opt_path_table_be;
    uint8_t root[34];
};

struct __attribute__((packed)) iso_dirent {
    uint8_t len;
    uint8_t ext_len;
    uint32_t extent_le;
    uint32_t extent_be;
    uint32_t size_le;
    uint32_t size_be;
    uint8_t time[7];
    uint8_t flags;
    uint8_t unit_size;
    uint8_t gap;
    uint16_t seq_le;
    uint16_t seq_be;
    uint8_t name_len;
    char name[];
};

static int name_eq(const char *iso_name, uint8_t nlen, const char *want) {
    size_t wlen = boot_strlen(want);
    uint8_t cmp = nlen;
    for (uint8_t i = 0; i < nlen; i++) {
        if (iso_name[i] == ';') {
            cmp = i;
            break;
        }
    }
    if (cmp != wlen)
        return 0;
    return boot_strncasecmp(iso_name, want, wlen) == 0;
}

static int find_in_dir(uint32_t dir_lba, uint32_t dir_size, const char *name,
                       uint32_t *out_lba, uint32_t *out_size, int *is_dir) {
    uint32_t sectors = (dir_size + 2047) / 2048;
    for (uint32_t s = 0; s < sectors; s++) {
        if (read_sector(dir_lba + s) != 0)
            return -1;
        uint32_t off = 0;
        while (off < 2048) {
            struct iso_dirent *d = (struct iso_dirent *)(sector + off);
            if (d->len == 0)
                break;
            if (d->name_len > 0 && d->name[0] != 0 && d->name[0] != 1) {
                if (name_eq(d->name, d->name_len, name)) {
                    *out_lba = d->extent_le;
                    *out_size = d->size_le;
                    *is_dir = (d->flags & 0x02) ? 1 : 0;
                    return 0;
                }
            }
            off += d->len;
        }
    }
    return -1;
}

int iso_load_file(const char *path, void *dest, uint32_t dest_cap, uint32_t *out_size) {
    if (read_sector(16) != 0)
        return -1;
    struct iso_pvd *pvd = (struct iso_pvd *)sector;
    if (pvd->type != 1 || boot_memcmp(pvd->id, "CD001", 5) != 0)
        return -1;

    struct iso_dirent *root = (struct iso_dirent *)pvd->root;
    uint32_t lba = root->extent_le;
    uint32_t size = root->size_le;

    char comp[32];
    const char *p = path;
    if (*p == '/')
        p++;
    while (*p) {
        size_t n = 0;
        while (p[n] && p[n] != '/' && n < sizeof(comp) - 1) {
            comp[n] = p[n];
            n++;
        }
        comp[n] = '\0';
        p += n;
        if (*p == '/')
            p++;

        uint32_t next_lba = 0, next_size = 0;
        int is_dir = 0;
        if (find_in_dir(lba, size, comp, &next_lba, &next_size, &is_dir) != 0)
            return -1;
        lba = next_lba;
        size = next_size;
        if (*p && !is_dir)
            return -1;
    }

    if (size > dest_cap)
        return -1;

    uint8_t *out = (uint8_t *)dest;
    uint32_t remaining = size;
    uint32_t cur = lba;
    while (remaining) {
        if (read_sector(cur) != 0)
            return -1;
        uint32_t chunk = remaining > 2048 ? 2048 : remaining;
        boot_memcpy(out, sector, chunk);
        out += chunk;
        remaining -= chunk;
        cur++;
    }
    if (out_size)
        *out_size = size;
    return 0;
}
