#ifndef PEAK_ELF_H
#define PEAK_ELF_H

#include "types.h"

struct elf_image {
    uint64_t entry;
    uint64_t load_base;
    uint64_t load_size;
    uint8_t *image; /* kernel VA of loaded segments (contiguous phys mapping) */
};

int elf_load(const uint8_t *file, size_t len, struct elf_image *out);
int proc_exec(const char *path, int argc, char **argv);

#endif
