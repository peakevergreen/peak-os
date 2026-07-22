#ifndef PEAK_BOOT_H
#define PEAK_BOOT_H

#include <stdint.h>

#define PEAK_BOOT_MAGIC   0x5045414B42544F4FULL
#define PEAK_BOOT_VERSION 4

#define PEAK_HHDM_OFFSET  0xffff800000000000ULL
#define PEAK_KERNEL_VMA   0xffffffff80000000ULL

#define PEAK_MMAP_MAX     128
#define PEAK_BOOT_STACK_SIZE 65536ULL
#define PEAK_BOOT_ENTROPY_MAX 64

/* Force identical layout on i386 loaders and x86_64/aarch64 kernel. */
typedef uint64_t peak_u64 __attribute__((aligned(8)));
typedef uint32_t peak_u32 __attribute__((aligned(4)));
typedef uint16_t peak_u16 __attribute__((aligned(2)));

enum peak_mmap_type {
    PEAK_MMAP_USABLE = 1,
    PEAK_MMAP_RESERVED = 2,
    PEAK_MMAP_ACPI_RECLAIM = 3,
    PEAK_MMAP_ACPI_NVS = 4,
    PEAK_MMAP_BAD = 5,
    PEAK_MMAP_BOOTLOADER = 6,
    PEAK_MMAP_KERNEL = 7,
    PEAK_MMAP_FRAMEBUFFER = 8,
};

enum peak_net_mode {
    PEAK_NET_DHCP_FALLBACK = 0,
    PEAK_NET_STATIC = 1,
    PEAK_NET_DHCP_ONLY = 2,
};

/* peak_bootinfo.flags */
#define PEAK_BOOT_FLAG_HAS_DTB       (1u << 0)
#define PEAK_BOOT_FLAG_HAS_FB        (1u << 1)
#define PEAK_BOOT_FLAG_ENTROPY_OK    (1u << 2) /* trusted firmware/HW seed */
#define PEAK_BOOT_FLAG_ENTROPY_WEAK  (1u << 3) /* supplemental-only seed */
#define PEAK_BOOT_FLAG_KASLR_SLIDE   (1u << 4)

struct peak_mmap_entry {
    peak_u64 base;
    peak_u64 length;
    peak_u32 type;
    peak_u32 reserved;
};

struct peak_framebuffer_info {
    peak_u64 addr;
    peak_u64 width;
    peak_u64 height;
    peak_u64 pitch;
    peak_u16 bpp;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t _pad[2];
};

struct peak_net_config {
    peak_u32 mode;
    peak_u32 ip;                 /* host order */
    peak_u32 mask;
    peak_u32 gw;
    peak_u32 dns;
    peak_u32 dhcp_timeout_ticks; /* timer ticks @ ~100Hz */
};

struct peak_bootinfo {
    peak_u64 magic;
    peak_u32 version;
    peak_u32 flags;
    peak_u64 hhdm_offset;
    peak_u64 rsdp_phys;          /* ACPI RSDP; unused on Raspberry Pi */
    peak_u64 dtb_phys;           /* flattened device tree physical addr */
    peak_u64 dtb_size;           /* DTB byte length (0 if unknown) */
    peak_u64 kernel_phys_base;
    peak_u64 kernel_phys_size;
    peak_u64 stack_phys;
    peak_u64 stack_size;
    struct peak_framebuffer_info fb;
    struct peak_net_config net;
    peak_u32 mmap_count;
    peak_u16 entropy_len;        /* v4: bytes valid in entropy[] */
    peak_u16 kaslr_slide_pages;  /* v4: optional kernel slide (pages) */
    uint8_t entropy[PEAK_BOOT_ENTROPY_MAX]; /* v4: wiped after random_init */
    struct peak_mmap_entry mmap[PEAK_MMAP_MAX];
} __attribute__((aligned(8)));

typedef void (*peak_kernel_entry_fn)(struct peak_bootinfo *info);

#endif /* PEAK_BOOT_H */
