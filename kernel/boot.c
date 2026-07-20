#define LIMINE_API_REVISION 0
#include <limine.h>
#include "types.h"
#include "serial.h"
#include "fb.h"
#include "console.h"
#include "pmm.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "shell.h"
#include "util.h"

/* Limine v8 macros expand to full declarations; set sections explicitly. */
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[4] = {
    0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf,
    0x785c6ed015d3e316, 0x181e920a7852b9d9
};

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[3] = {
    0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 2
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_stack_size_request stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST,
    .revision = 0,
    .response = NULL,
    .stack_size = 65536
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[2] = {
    0xadc0e0531bb10d03, 0x9572709f31764c62
};

static void hang(void) {
    for (;;)
        hlt();
}

void kernel_entry(void) {
    serial_init();
    serial_write_str("Peak OS booting...\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        serial_write_str("Limine base revision not supported\n");
        hang();
    }

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        serial_write_str("No framebuffer\n");
        hang();
    }

    if (hhdm_request.response == NULL || memmap_request.response == NULL) {
        serial_write_str("Missing HHDM or memmap\n");
        hang();
    }

    struct limine_framebuffer *lfb = framebuffer_request.response->framebuffers[0];
    struct framebuffer fb = {
        .addr = (uint8_t *)lfb->address,
        .width = lfb->width,
        .height = lfb->height,
        .pitch = lfb->pitch,
        .bpp = lfb->bpp,
        .red_mask_size = lfb->red_mask_size,
        .red_mask_shift = lfb->red_mask_shift,
        .green_mask_size = lfb->green_mask_size,
        .green_mask_shift = lfb->green_mask_shift,
        .blue_mask_size = lfb->blue_mask_size,
        .blue_mask_shift = lfb->blue_mask_shift,
    };
    fb_init(&fb);

    pmm_init(memmap_request.response, hhdm_request.response->offset);

    console_init();
    console_write("Peak OS kernel online.\n");

    pic_init();
    idt_init();
    timer_init(100);
    keyboard_init();
    mouse_init();
    sti();

    shell_init();

    for (;;) {
        if (shell_mode() == MODE_CLI)
            shell_run_once();
        __asm__ volatile ("hlt");
    }
}
