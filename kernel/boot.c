#include "peak_boot.h"
#include "types.h"
#include "serial.h"
#include "fb.h"
#include "console.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "vfs.h"
#include "arch.h"
#include "platform.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "sched.h"
#include "syscall.h"
#include "agent.h"
#include "shell.h"
#include "theme.h"
#include "wallpaper.h"
#include "settings.h"
#include "net.h"
#include "netdev.h"
#include "ctr.h"
#include "sysmon.h"
#include "util.h"
#include "rtc.h"
#include "sound.h"
#include "power.h"
#include "peakdisk.h"
#include "blobstore.h"
#include "peakvec.h"
#include "clipboard.h"
#include "notify.h"
#include "guiproto.h"
#include "surface.h"
#include "js.h"
#include "random.h"
#include "cap.h"

static int g_have_fb;

/* Packed name list (avoids pointer tables in .rodata on aarch64). */
static const char g_peak_bins_blob[] =
    "sh\0ls\0cat\0edit\0peak\0pwd\0cd\0mkdir\0touch\0rm\0"
    "cp\0mv\0ln\0stat\0du\0df\0truncate\0head\0tail\0wc\0"
    "grep\0hexdump\0strings\0echo\0clear\0tree\0find\0date\0"
    "free\0env\0export\0which\0seq\0sleep\0theme\0wallpaper\0scale\0"
    "help\0man\0ask\0audit\0memory\0policy\0privacy\0gui\0uname\0"
    "true\0false\0reboot\0ctr\0ctrd\0"
    "ping\0ifconfig\0wget\0top\0sysmon\0ps\0js\0";

static void hang(void) {
    for (;;)
        arch_idle();
}

static void status_ok(const char *msg) {
    if (g_have_fb)
        console_status_ok(msg);
    else {
        serial_write_str("ok: ");
        serial_write_str(msg);
        serial_write_str("\n");
    }
}

static void status_fail(const char *msg) {
    if (g_have_fb)
        console_status_fail(msg);
    else {
        serial_write_str("fail: ");
        serial_write_str(msg);
        serial_write_str("\n");
    }
}

void kernel_entry(struct peak_bootinfo *info) {
    arch_early_init();
    serial_init();
    serial_write_str("PeakOS booting...\n");

    if (!info || info->magic != PEAK_BOOT_MAGIC ||
        info->version != PEAK_BOOT_VERSION) {
        serial_write_str("Invalid Peak BootInfo\n");
        hang();
    }

    if (info->mmap_count == 0 || info->hhdm_offset == 0) {
        serial_write_str("Missing HHDM or memmap\n");
        hang();
    }

    g_have_fb = (info->fb.addr != 0 && info->fb.width != 0 &&
                 info->fb.height != 0);
    if (!g_have_fb)
        serial_write_str("No framebuffer — serial console only\n");

    if (g_have_fb) {
        struct framebuffer fb = {
            .addr = (uint8_t *)info->fb.addr,
            .width = info->fb.width,
            .height = info->fb.height,
            .pitch = info->fb.pitch,
            .bpp = info->fb.bpp,
            .red_mask_size = info->fb.red_mask_size,
            .red_mask_shift = info->fb.red_mask_shift,
            .green_mask_size = info->fb.green_mask_size,
            .green_mask_shift = info->fb.green_mask_shift,
            .blue_mask_size = info->fb.blue_mask_size,
            .blue_mask_shift = info->fb.blue_mask_shift,
        };
        fb_init(&fb);
        fb_set_ui_scale(fb_recommend_scale());
        console_init();
        console_write("\n  PeakOS\n\n");
        status_ok("Framebuffer");
    }

    pmm_init(info);
    status_ok("Physical memory");

    if (info->kernel_phys_base)
        vmm_set_kernel_phys_base(info->kernel_phys_base);
    vmm_init(info->hhdm_offset);
    status_ok("Virtual memory");

    heap_init();
    status_ok("Kernel heap");

    random_init(info);
    if (info->entropy_len)
        memzero_explicit(info->entropy, sizeof(info->entropy));
    info->entropy_len = 0;
#if defined(__GNUC__) || defined(__clang__)
    extern void __stack_chk_guard_setup(void);
    __stack_chk_guard_setup();
#endif
    if (random_ready(RANDOM_DOMAIN_CRYPTO))
        status_ok("Entropy (crypto ready)");
    else
        status_fail("Entropy (degraded)");
    cap_init();
    privacy_init();
    status_ok("Capabilities");

    if (g_have_fb)
        fb_alloc_backbuffer();

    vfs_init();
    vfs_seed_defaults();
    status_ok("Virtual filesystem");

    for (const char *n = g_peak_bins_blob; *n; ) {
        char path[64];
        char *p = path;
        const char *pfx = "/bin/";
        while (*pfx)
            *p++ = *pfx++;
        while (*n && p < path + sizeof(path) - 1)
            *p++ = *n++;
        *p = '\0';
        if (*n == '\0')
            n++; /* advance past NUL to next name or end */
        (void)vfs_write_file(path, "PEAKBUILTIN", 11);
    }
    status_ok("Core utilities");

    arch_fpu_init();
    status_ok("FPU");

    arch_cpu_init();
    sched_init();
    status_ok("CPU and scheduler");

    arch_park_secondaries(info);

    js_runtime_init();
    status_ok("Peak JS runtime");

    arch_irq_init();
    status_ok("Interrupts");

    syscall_init();
    status_ok("System calls");

    timer_init(100);
    status_ok("Timer");

    keyboard_init();
    status_ok("Keyboard");

    mouse_init();
    status_ok("Mouse");

    if (platform_init(info) != 0)
        status_fail("Platform");
    else
        status_ok("Platform");

    sysmon_init();
    status_ok("System monitor");

    arch_irq_enable();

    net_set_boot_config(&info->net);
    if (net_init() != 0) {
        serial_write_str("net: unavailable\n");
        status_fail("Network");
    } else {
        const struct netdev_ops *nd = netdev_get();
        status_ok(nd && nd->name ? nd->name : "Network");
    }

    theme_init();
    status_ok("Theme");

    if (g_have_fb) {
        wallpaper_init();
        status_ok("Wallpaper");
    }

    rtc_init();
    sound_init();
    power_init();
    clipboard_init();
    notify_init();
    surface_init();
    guiproto_init();
    peakdisk_init();
    blobstore_init();
    if (peakdisk_available()) {
        if (peakdisk_load() == 0)
            status_ok("Disk (PeakFS restore)");
        else
            status_ok("Disk (empty — use Save disk)");
    }
    peakvec_init();
    status_ok("PeakVec");
    agent_init();
    status_ok("Peak agent");

    platform_late_init(info);

    settings_init();
    if (g_have_fb) {
        if (settings_gui_scale() != fb_ui_scale()) {
            fb_set_ui_scale(settings_gui_scale());
            console_init();
            theme_apply_console();
        } else {
            status_ok("Settings");
        }
    }
    status_ok("Boot complete");

    if (g_have_fb) {
        console_boot_logo();
        console_printf("\n  display %lux%lu  ui scale %ux\n\n",
                       (unsigned long)fb_get()->width,
                       (unsigned long)fb_get()->height,
                       (unsigned)fb_ui_scale());
        serial_write_str("platform: ");
        serial_write_str(platform_name());
        serial_write_str("\n");
    }

    sched_start_background();
    shell_init();

    for (;;) {
        if (shell_mode() == MODE_CLI)
            shell_run_once();
        platform_poll();
        net_poll();
        ctr_poll();
        sysmon_poll();
        sysmon_idle_enter();
        arch_idle();
        sysmon_idle_leave();
    }
}
