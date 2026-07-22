#ifndef PEAK_FDT_H
#define PEAK_FDT_H

#include "types.h"

#define FDT_MAGIC 0xd00dfeed

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

uint32_t fdt_ld32(const void *p);
int fdt_check(const void *fdt);
uint32_t fdt_totalsize(const void *fdt);
/* Find first memory@* reg; returns 0 on success. */
int fdt_memory_range(const void *fdt, uint64_t *base, uint64_t *size);
/* Find prop by path-ish scan; returns pointer into FDT or NULL. */
const void *fdt_getprop(const void *fdt, const char *node_name,
                        const char *prop_name, uint32_t *len_out);

#endif
