#ifndef PEAK_BOOTINFO_H
#define PEAK_BOOTINFO_H

#include "types.h"

struct peak_bootinfo;

void bootinfo_init(const struct peak_bootinfo *info);

const char *bootinfo_sysname(void);
const char *bootinfo_release(void);
const char *bootinfo_machine(void);
const char *bootinfo_platform(void);
uint32_t bootinfo_abi_version(void);
uint32_t bootinfo_flags(void);
void bootinfo_format_version(char *buf, size_t cap);

#endif
