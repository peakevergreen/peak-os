#include "fdt.h"
#include "util.h"

uint32_t fdt_ld32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static uint64_t fdt_ld64(const void *p) {
    return ((uint64_t)fdt_ld32(p) << 32) | fdt_ld32((const uint8_t *)p + 4);
}

int fdt_check(const void *fdt) {
    if (!fdt)
        return -1;
    const struct fdt_header *h = (const struct fdt_header *)fdt;
    if (fdt_ld32(&h->magic) != FDT_MAGIC)
        return -1;
    return 0;
}

uint32_t fdt_totalsize(const void *fdt) {
    const struct fdt_header *h = (const struct fdt_header *)fdt;
    return fdt_ld32(&h->totalsize);
}

/* Minimal structure walker: find node whose name starts with prefix,
 * then read a property. */
static const char *fdt_strings(const void *fdt) {
    const struct fdt_header *h = (const struct fdt_header *)fdt;
    return (const char *)fdt + fdt_ld32(&h->off_dt_strings);
}

const void *fdt_getprop(const void *fdt, const char *node_name,
                        const char *prop_name, uint32_t *len_out) {
    if (fdt_check(fdt) != 0 || !node_name || !prop_name)
        return 0;
    const struct fdt_header *h = (const struct fdt_header *)fdt;
    const uint8_t *p = (const uint8_t *)fdt + fdt_ld32(&h->off_dt_struct);
    const uint8_t *end = (const uint8_t *)fdt + fdt_ld32(&h->totalsize);
    const char *strings = fdt_strings(fdt);
    int in_node = 0;
    size_t nlen = strlen(node_name);

    while (p + 4 <= end) {
        uint32_t token = fdt_ld32(p);
        p += 4;
        if (token == 1) { /* FDT_BEGIN_NODE */
            const char *name = (const char *)p;
            size_t l = strlen(name);
            in_node = (strncmp(name, node_name, nlen) == 0);
            p += (l + 4) & ~3u;
        } else if (token == 2) { /* FDT_END_NODE */
            in_node = 0;
        } else if (token == 3) { /* FDT_PROP */
            uint32_t len = fdt_ld32(p);
            uint32_t nameoff = fdt_ld32(p + 4);
            p += 8;
            const char *pname = strings + nameoff;
            if (in_node && strcmp(pname, prop_name) == 0) {
                if (len_out)
                    *len_out = len;
                return p;
            }
            p += (len + 3) & ~3u;
        } else if (token == 9) { /* FDT_END */
            break;
        } else if (token == 4) { /* FDT_NOP */
            continue;
        } else {
            break;
        }
    }
    return 0;
}

int fdt_memory_range(const void *fdt, uint64_t *base, uint64_t *size) {
    uint32_t len = 0;
    const uint8_t *reg = (const uint8_t *)fdt_getprop(fdt, "memory", "reg", &len);
    if (!reg || len < 16) {
        /* Fallback common Pi 3: 1GiB at 0 */
        if (base) *base = 0;
        if (size) *size = 0x40000000ULL;
        return 0;
    }
    /* Assume #address-cells=2 #size-cells=2 */
    if (base) *base = fdt_ld64(reg);
    if (size) *size = fdt_ld64(reg + 8);
    return 0;
}
