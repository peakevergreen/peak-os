#ifndef PEAK_EFI_H
#define PEAK_EFI_H

#include <stdint.h>

typedef uint64_t efi_status;
typedef uint64_t efi_uintn;
typedef int64_t efi_intn;
typedef uint16_t efi_char16;
typedef void *efi_handle;
typedef void *efi_event;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define EFI_SUCCESS 0
#define EFI_ERROR(x) (0x8000000000000000ULL | (x))
#define EFI_LOAD_ERROR EFI_ERROR(1)
#define EFI_INVALID_PARAMETER EFI_ERROR(2)
#define EFI_UNSUPPORTED EFI_ERROR(3)
#define EFI_BUFFER_TOO_SMALL EFI_ERROR(5)
#define EFI_NOT_FOUND EFI_ERROR(14)

#define EFI_MEMORY_LOADER_CODE 1
#define EFI_MEMORY_LOADER_DATA 2
#define EFI_MEMORY_BS_CODE 3
#define EFI_MEMORY_BS_DATA 4
#define EFI_MEMORY_RT_CODE 5
#define EFI_MEMORY_RT_DATA 6
#define EFI_MEMORY_CONVENTIONAL 7
#define EFI_MEMORY_UNUSABLE 8
#define EFI_MEMORY_ACPI_RECLAIM 9
#define EFI_MEMORY_ACPI_NVS 10
#define EFI_MEMORY_MMIO 11
#define EFI_MEMORY_MMIO_PORT 12
#define EFI_MEMORY_PAL 13

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header;

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor;

typedef struct {
    int32_t max_mode;
    int32_t mode;
    struct {
        uint32_t red_mask;
        uint32_t green_mask;
        uint32_t blue_mask;
        uint32_t reserved_mask;
    } pixel_info;
    uint32_t pixels_per_scan_line;
} efi_gop_mode_info;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    efi_gop_mode_info *info;
    efi_uintn size_of_info;
    uint64_t frame_buffer_base;
    efi_uintn frame_buffer_size;
} efi_gop_mode;

typedef struct efi_graphics_output_protocol {
    efi_status (*query_mode)(struct efi_graphics_output_protocol *, uint32_t,
                             efi_uintn *, efi_gop_mode_info **);
    efi_status (*set_mode)(struct efi_graphics_output_protocol *, uint32_t);
    efi_status (*blt)(void *, void *, uint32_t, efi_uintn, efi_uintn, efi_uintn,
                      efi_uintn, efi_uintn, efi_uintn, efi_uintn);
    efi_gop_mode *mode;
} efi_graphics_output_protocol;

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} efi_guid;

typedef struct efi_simple_fs_protocol efi_simple_fs_protocol;
typedef struct efi_file_protocol efi_file_protocol;

struct efi_file_protocol {
    uint64_t revision;
    efi_status (*open)(efi_file_protocol *, efi_file_protocol **, efi_char16 *,
                       uint64_t, uint64_t);
    efi_status (*close)(efi_file_protocol *);
    efi_status (*delete)(efi_file_protocol *);
    efi_status (*read)(efi_file_protocol *, efi_uintn *, void *);
    efi_status (*write)(efi_file_protocol *, efi_uintn *, void *);
    efi_status (*get_position)(efi_file_protocol *, uint64_t *);
    efi_status (*set_position)(efi_file_protocol *, uint64_t);
    efi_status (*get_info)(efi_file_protocol *, efi_guid *, efi_uintn *, void *);
    efi_status (*set_info)(efi_file_protocol *, efi_guid *, efi_uintn, void *);
    efi_status (*flush)(efi_file_protocol *);
};

struct efi_simple_fs_protocol {
    uint64_t revision;
    efi_status (*open_volume)(efi_simple_fs_protocol *, efi_file_protocol **);
};

typedef struct {
    efi_table_header hdr;
    void *raise_tpl;
    void *restore_tpl;
    efi_status (*allocate_pages)(uint32_t, uint32_t, efi_uintn, uint64_t *);
    efi_status (*free_pages)(uint64_t, efi_uintn);
    efi_status (*get_memory_map)(efi_uintn *, efi_memory_descriptor *, efi_uintn *,
                                 efi_uintn *, uint32_t *);
    efi_status (*allocate_pool)(uint32_t, efi_uintn, void **);
    efi_status (*free_pool)(void *);
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    void *handle_protocol;
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    efi_status (*exit_boot_services)(efi_handle, efi_uintn);
    void *get_next_monotonic_count;
    void *stall;
    void *set_watchdog_timer;
    void *connect_controller;
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    void *locate_handle_buffer;
    efi_status (*locate_protocol)(efi_guid *, void *, void **);
} efi_boot_services;

typedef struct {
    efi_table_header hdr;
    efi_char16 *firmware_vendor;
    uint32_t firmware_revision;
    efi_handle console_in_handle;
    void *con_in;
    efi_handle console_out_handle;
    void *con_out;
    efi_handle standard_error_handle;
    void *std_err;
    void *runtime_services;
    efi_boot_services *boot_services;
    efi_uintn number_of_table_entries;
    void *configuration_table;
} efi_system_table;

#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_FILE_MODE_READ 1ULL

static const efi_guid EFI_GOP_GUID = {
    0x9042a9de, 0x23dc, 0x4a38,
    { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }
};

static const efi_guid EFI_SIMPLE_FS_GUID = {
    0x964e5b22, 0x6459, 0x11d2,
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

static const efi_guid EFI_LOADED_IMAGE_GUID = {
    0x5b1b31a1, 0x9562, 0x11d2,
    { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

/* EFI_RNG_PROTOCOL */
static const efi_guid EFI_RNG_PROTOCOL_GUID = {
    0x3152bca5, 0xeade, 0x433d,
    { 0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44 }
};

typedef struct efi_rng_protocol {
    efi_status (*get_info)(struct efi_rng_protocol *, efi_uintn *, void *);
    efi_status (*get_rng)(struct efi_rng_protocol *, void *, efi_uintn, uint8_t *);
} efi_rng_protocol;

typedef struct {
    uint32_t revision;
    efi_handle parent_handle;
    efi_system_table *system_table;
    efi_handle device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    void *image_code_type;
    void *image_data_type;
    void *unload;
} efi_loaded_image_protocol;

#endif
