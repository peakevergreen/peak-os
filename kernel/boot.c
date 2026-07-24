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
#include "cap.h"
#include "privacy.h"
#include "blobstore.h"
#include "blobstore.h"
#include "peakvec.h"
#include "clipboard.h"
#include "notify.h"
#include "guiproto.h"
#include "surface.h"
#include "js.h"
#include "random.h"
#include "ubin.h"

static int g_have_fb;

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
    {
        uint32_t rf = random_status_flags();
        char rng_line[96];
        snprintf(rng_line, sizeof(rng_line), "Entropy flags=0x%x%s%s%s%s",
                 (unsigned)rf,
                 (rf & RANDOM_READY_CRYPTO) ? " CRYPTO" : "",
                 (rf & RANDOM_READY_ANY) ? " ANY" : "",
                 (rf & RANDOM_FLAG_WEAK) ? " WEAK" : "",
                 (rf & RANDOM_FLAG_HW) ? " HW" : "");
        if (random_ready(RANDOM_DOMAIN_CRYPTO))
            status_ok(rng_line);
        else
            status_fail(rng_line);
    }
    cap_init();
    privacy_init();
    status_ok("Capabilities");

    if (g_have_fb)
        fb_alloc_backbuffer();

    vfs_init();
    vfs_seed_defaults();
    status_ok("Virtual filesystem");

    ubin_seed_vfs();
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
        status_fail("Network");
    } else {
        struct net_info ni;
        char msg[72];
        char ipb[32];
        const struct netdev_ops *nd = netdev_get();
        const char *name = (nd && nd->name) ? nd->name : "Network";
        net_get_info(&ni);
        if (ni.up && ni.addr_mode && ni.ip) {
            net_format_ip(ni.ip, ipb, sizeof(ipb));
            snprintf(msg, sizeof(msg), "%s (%s %s)", name, ni.addr_mode, ipb);
            status_ok(msg);
        } else {
            status_ok(name);
        }
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
    } else {
        status_ok("Disk (none)");
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

    if ((info->flags & PEAK_BOOT_FLAG_SMOKE_PERSIST) && peakdisk_available()) {
        size_t mlen = 0;
        char mbuf[8];
        /* Marker under /home so workspace persist profile exports it. */
        if (vfs_read_file("/home/dev/workspace/.peak_smoke_ok", mbuf, sizeof(mbuf) - 1,
                          &mlen) == 0 &&
            mlen > 0) {
            serial_write_str("peakdisk: smoke restore ok\n");
        } else {
            privacy_set_persist_profile(1);
            if (vfs_write_file("/home/dev/workspace/.peak_smoke_ok", "1", 1) != 0) {
                serial_write_str("peakdisk: smoke marker write failed\n");
            } else {
                int saved = -1;
                /* ATA PIO on loaded CI/TCG hosts can miss a tight wait once. */
                for (int attempt = 0; attempt < 3; attempt++) {
                    if (peakdisk_save() == 0) {
                        saved = 0;
                        break;
                    }
                }
                if (saved == 0)
                    serial_write_str("peakdisk: smoke save ok\n");
                else
                    serial_write_str("peakdisk: smoke save failed\n");
            }
        }
    }

    if (g_have_fb) {
        /* Boot status lines leave cells to the right of short banner rows.
         * Clear so green [ ok ] remnants do not sit beside the wordmark. */
        console_clear();
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
