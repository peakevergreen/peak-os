#include "bootinfo.h"
#include "peak_boot.h"
#include "platform.h"
#include "util.h"

static uint32_t g_boot_version;
static uint32_t g_boot_flags;

void bootinfo_init(const struct peak_bootinfo *info) {
    if (!info)
        return;
    g_boot_version = info->version;
    g_boot_flags = info->flags;
}

const char *bootinfo_sysname(void) {
    return "PeakOS";
}

const char *bootinfo_release(void) {
    return "0.3.0-ai";
}

const char *bootinfo_machine(void) {
#if defined(__aarch64__)
    return "aarch64";
#else
    return "x86_64";
#endif
}

const char *bootinfo_platform(void) {
    return platform_name();
}

uint32_t bootinfo_abi_version(void) {
    return g_boot_version;
}

uint32_t bootinfo_flags(void) {
    return g_boot_flags;
}

void bootinfo_format_version(char *buf, size_t cap) {
    if (!buf || cap == 0)
        return;
    snprintf(buf, cap, "#1 boot:v%u %s",
             (unsigned)g_boot_version, platform_name());
}
