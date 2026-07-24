# Peak OS — freestanding kernel (x86_64 PC + aarch64 Raspberry Pi)
KERNEL_NAME := peak-os
ARCH        ?= x86_64
PLATFORM    ?= pc
BUILD       := build/$(ARCH)

ISO_ROOT    := $(BUILD)/iso_root
ISO         := build/$(KERNEL_NAME).iso
KERNEL_ELF  := $(BUILD)/kernel.elf
KERNEL8_IMG := $(BUILD)/kernel8.img
PI_IMAGE    := build/peak-os-rpi-arm64.img

BIOS_ELF    := $(BUILD)/boot/bios.elf
BIOS_BIN    := $(BUILD)/boot/peak-bios.bin
UEFI_EFI    := $(BUILD)/boot/BOOTX64.EFI
ESP_IMG     := $(BUILD)/boot/efi.img

CC      := clang
LD      := ld.lld
NASM    ?= nasm
LLDLINK := lld-link
OBJCOPY := llvm-objcopy
# Homebrew LLVM often isn't on PATH; prefer it when llvm-objcopy is missing.
ifeq ($(shell command -v $(OBJCOPY) >/dev/null 2>&1 && echo yes),)
  ifneq ($(wildcard /opt/homebrew/opt/llvm/bin/llvm-objcopy),)
    OBJCOPY := /opt/homebrew/opt/llvm/bin/llvm-objcopy
  else ifneq ($(wildcard /usr/local/opt/llvm/bin/llvm-objcopy),)
    OBJCOPY := /usr/local/opt/llvm/bin/llvm-objcopy
  endif
endif

# ---- Shared portable kernel sources ----
KERNEL_COMMON_SRCS := \
	kernel/boot.c \
	kernel/serial.c \
	kernel/fb.c \
	kernel/display.c \
	kernel/display_clip.c \
	kernel/console.c \
	kernel/pmm.c \
	kernel/heap.c \
	kernel/vmm.c \
	kernel/vfs.c \
	kernel/vfs_path_util.c \
	kernel/vfs_peakfs.c \
	kernel/sched.c \
	kernel/elf.c \
	kernel/syscall.c \
	kernel/agent.c \
	kernel/agent_policy.c \
	kernel/agent_tools.c \
	kernel/agent_planner.c \
	kernel/timer.c \
	kernel/keyboard.c \
	kernel/mouse.c \
	kernel/shell.c \
	kernel/shell_dispatch.c \
	kernel/shell_builtins.c \
	kernel/shell_split.c \
	kernel/theme.c \
	kernel/wallpaper.c \
	kernel/settings.c \
	kernel/sync.c \
	kernel/util.c \
	kernel/console_scroll.c \
	kernel/ctr.c \
	kernel/ctr_path.c \
	kernel/ctr_build.c \
	kernel/ctr_http.c \
	kernel/sysmon.c \
	kernel/clipboard.c \
	kernel/notify.c \
	kernel/peakdisk.c \
	kernel/blobstore.c \
	kernel/peakvec.c \
	kernel/gui/guiproto.c \
	kernel/irq.c \
	kernel/blockdev.c \
	kernel/netdev.c \
	kernel/fdt.c \
	kernel/js/js_core.c \
	kernel/js/js_compile.c \
	kernel/js/js_lex.c \
	kernel/js/js_codegen.c \
	kernel/js/js_parse.c \
	kernel/js/js_vm.c \
	kernel/gui/dom_core.c \
	kernel/gui/dom_parse.c \
	kernel/gui/css_parse.c \
	kernel/gui/css_layout.c \
	kernel/gui/browser_js.c \
	kernel/gui/browser_parse.c \
	kernel/gui/webapi.c \
	kernel/gui/webapi_stubs.c \
	kernel/net/dhcp_util.c \
	kernel/net/http_util.c \
	kernel/net/tcp_util.c \
	kernel/net/arp_util.c \
	kernel/net/crypto.c \
	kernel/net/crypto_hash.c \
	kernel/net/crypto_sha384.c \
	kernel/net/crypto_aead.c \
	kernel/net/crypto_x25519.c \
	kernel/net/crypto_p256.c \
	kernel/net/crypto_p384.c \
	kernel/net/hacl_p384/Hacl_P384.c \
	kernel/net/crypto_rsa.c \
	kernel/net/crypto_hkdf.c \
	kernel/net/tls_util.c \
	kernel/net/tls_clienthello.c \
	kernel/net/tls_session.c \
	kernel/net/tls_hsts.c \
	kernel/net/tls_ech.c \
	kernel/net/http2.c \
	kernel/net/x509.c \
	kernel/net/webpki.c \
	kernel/net/webpki_roots_data.c \
	kernel/net/tls.c \
	kernel/net/tls_record.c \
	kernel/net/tls_handshake.c \
	kernel/net/tls13.c \
	kernel/net/tls_trust.c \
	kernel/net/net.c \
	kernel/net/arp.c \
	kernel/net/dhcp.c \
	kernel/net/dns.c \
	kernel/net/tcp.c \
	kernel/net/http.c \
	kernel/random.c \
	kernel/cap.c \
	kernel/privacy.c \
	kernel/stackchk.c \
	kernel/user/libpeak.c \
	kernel/user/ubin.c \
	kernel/user/ubin_registry.c \
	kernel/user/utils_file.c \
	kernel/user/utils_text.c \
	kernel/user/utils_text2.c \
	kernel/user/utils_text3.c \
	kernel/user/utils_sys.c \
	kernel/user/utils_agent.c \
	kernel/user/utils_net.c \
	kernel/user/utils_tar.c \
	kernel/user/utils_monitor.c \
	kernel/user/utils_ctr.c \
	kernel/user/utils_js.c \
	kernel/gui/font.c \
	kernel/gui/font_render.c \
	kernel/gui/window.c \
	kernel/gui/surface.c \
	kernel/gui/desktop.c \
	kernel/gui/desktop_damage.c \
	kernel/gui/desktop_windows.c \
	kernel/gui/desktop_compose.c \
	kernel/gui/desktop_login.c \
	kernel/gui/desktop_menus.c \
	kernel/gui/desktop_overlays.c \
	kernel/gui/desktop_terminal.c \
	kernel/gui/desktop_files.c \
	kernel/gui/desktop_settings.c \
	kernel/gui/desktop_agent.c \
	kernel/gui/game.c \
	kernel/gui/browser.c \
	kernel/gui/browser_draw.c \
	kernel/gui/browser_nav.c \
	kernel/gui/monitor.c

KERNEL_COMMON_ASMS := kernel/wallpaper_data.S

ifeq ($(ARCH),x86_64)
PLATFORM := pc
CFLAGS  := -target x86_64-unknown-none-elf \
           -ffreestanding -fno-stack-protector -fno-stack-check \
           -fno-PIC -fno-PIE -msse2 -mfpmath=sse \
           -mno-red-zone -mcmodel=kernel \
           -Wall -Wextra -Werror -std=c11 -O2 -g \
           -MMD -MP \
           -Ikernel/include -Ikernel/gui -Iboot/include
ASFLAGS := -target x86_64-unknown-none-elf -c
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
           -T kernel/arch/x86_64/linker.ld
LINKER_SCRIPT := kernel/arch/x86_64/linker.ld

KERNEL_ARCH_SRCS := \
	kernel/arch/x86_64/gdt.c \
	kernel/arch/x86_64/idt.c \
	kernel/arch/x86_64/pic.c \
	kernel/arch/x86_64/fpu.c \
	kernel/arch/x86_64/ata.c \
	kernel/arch/x86_64/rtc.c \
	kernel/arch/x86_64/sound.c \
	kernel/power.c \
	kernel/net/pci.c \
	kernel/net/e1000.c \
	kernel/arch/x86_64/arch.c \
	kernel/arch/x86_64/timer.c \
	kernel/arch/x86_64/serial_arch.c \
	kernel/arch/x86_64/irq_lines.c \
	kernel/platform/pc.c \
	kernel/drivers/block/ata_bd.c \
	kernel/drivers/net/e1000_nd.c \
	kernel/drivers/net/virtio_net.c \
	kernel/drivers/virtio_rng.c

KERNEL_ARCH_ASMS := \
	kernel/arch/x86_64/isr.S \
	kernel/arch/x86_64/context.S

BIOS_CFLAGS := -target i386-unknown-none-elf \
               -m32 -ffreestanding -fno-stack-protector -fno-PIC \
               -mno-sse -mno-mmx -Wall -Wextra -Werror -std=c11 -O2 \
               -Iboot/include
BIOS_ASFLAGS := -target i386-unknown-none-elf -m32 -c
BIOS_LDFLAGS := -m elf_i386 -nostdlib -static -T boot/bios/linker.ld

UEFI_CFLAGS := -target x86_64-unknown-windows \
               -ffreestanding -fno-stack-protector -fno-stack-check \
               -fno-PIC -fshort-wchar -mno-red-zone \
               -Wall -Wextra -Werror -std=c11 -O2 \
               -Iboot/include -Iboot/uefi

else ifeq ($(ARCH),aarch64)
PLATFORM ?= rpi
CFLAGS  := -target aarch64-unknown-none-elf \
           -ffreestanding -fno-stack-protector -fno-stack-check \
           -fno-PIC -fno-PIE \
           -Wall -Wextra -Werror -std=c11 -O2 -g \
           -MMD -MP \
           -Ikernel/include -Ikernel/gui -Iboot/include -Ikernel/platform/rpi \
           -DPEAK_ELF_MACHINE=183
# Boot shim must not use NEON before CPACR is set — compile without SIMD.
BOOT_SHIM_CFLAGS := $(CFLAGS) -mgeneral-regs-only
ASFLAGS := -target aarch64-unknown-none-elf -c
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 \
           -T kernel/arch/aarch64/linker.ld
LINKER_SCRIPT := kernel/arch/aarch64/linker.ld

KERNEL_ARCH_SRCS := \
	boot/rpi/boot_shim.c \
	kernel/arch/aarch64/arch.c \
	kernel/arch/aarch64/timer.c \
	kernel/arch/aarch64/serial_arch.c \
	kernel/arch/aarch64/fpu.c \
	kernel/arch/aarch64/rtc_stub.c \
	kernel/arch/aarch64/sound_stub.c \
	kernel/power.c \
	kernel/platform/rpi/platform.c \
	kernel/platform/rpi/uart.c \
	kernel/platform/rpi/mailbox.c \
	kernel/platform/rpi/gpio.c \
	kernel/platform/rpi/sdhci.c \
	kernel/platform/rpi/usb.c \
	kernel/platform/rpi/pcie.c \
	kernel/platform/rpi/net.c \
	kernel/platform/rpi/wifi.c \
	kernel/platform/rpi/sound.c \
	kernel/platform/rpi/gpu.c \
	kernel/drivers/usb/usb_core.c \
	kernel/drivers/usb/dwc2.c \
	kernel/drivers/usb/dwc2_hub.c \
	kernel/drivers/usb/dwc2_hid.c \
	kernel/drivers/usb/xhci.c \
	kernel/drivers/net/usb_lan.c \
	kernel/drivers/net/genet.c \
	kernel/drivers/net/rp1_eth.c

KERNEL_ARCH_ASMS := \
	kernel/arch/aarch64/start.S \
	kernel/arch/aarch64/boot_pagetables.S \
	kernel/arch/aarch64/context.S \
	kernel/arch/aarch64/exceptions.S

else
$(error Unsupported ARCH=$(ARCH). Use x86_64 or aarch64)
endif

KERNEL_SRCS := $(KERNEL_COMMON_SRCS) $(KERNEL_ARCH_SRCS)
KERNEL_ASMS := $(KERNEL_COMMON_ASMS) $(KERNEL_ARCH_ASMS)
ifeq ($(ARCH),aarch64)
# start.S before C objects so .boot.data stack precedes page tables
KERNEL_OBJS := $(patsubst %.S,$(BUILD)/%.o,$(KERNEL_ASMS)) \
               $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_SRCS))
else
KERNEL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_SRCS)) \
               $(patsubst %.S,$(BUILD)/%.o,$(KERNEL_ASMS))
endif

BIOS_C_SRCS := \
	boot/bios/main32.c \
	boot/bios/bios_call.c \
	boot/bios/iso9660.c \
	boot/bios/vbe.c \
	boot/bios/e820.c \
	boot/common/util.c \
	boot/common/elf_load.c \
	boot/common/paging.c \
	boot/common/load_ctx.c \
	boot/common/peak_conf.c

BIOS_S_SRCS := boot/bios/entry.S boot/bios/lm_enter.S
BIOS_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(BIOS_C_SRCS)) \
             $(patsubst %.S,$(BUILD)/%.o,$(BIOS_S_SRCS))

UEFI_SRCS := \
	boot/uefi/efi_main.c \
	boot/common/util.c \
	boot/common/elf_load.c \
	boot/common/paging.c \
	boot/common/load_ctx.c
UEFI_OBJS := $(patsubst %.c,$(BUILD)/uefi/%.o,$(notdir $(UEFI_SRCS)))

.PHONY: all iso kernel bootloaders run clean test test-host smoke smoke-qemu \
        smoke-bios smoke-uefi smoke-peakfs smoke-tls-live purity doctor pi-image pi-image-check flash-pi \
        kernel8 run-aarch64-virt smoke-aarch64 firmware-fetch

ifeq ($(ARCH),x86_64)
all: iso
else
all: pi-image
endif

HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
HOST_CFLAGS_REDECL := $(HOST_CFLAGS) -Wno-incompatible-library-redeclaration
HOST_TEST_DIR := $(BUILD)/tests
HOST_TEST_JOBS ?= $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
HOST_TEST_INC_KERNEL := -Itests/host/include -Ikernel/include
HOST_TEST_INC_BOOT := -Iboot/include
HOST_TEST_INC_HOST_BOOT_KERNEL := -Itests/host/include -Iboot/include -Ikernel/include

# $(1) = test name suffix (e.g. phase7), $(2) = sources, $(3) = cflags
define HOST_TEST_RULE
$(HOST_TEST_DIR)/test_$(1): $(2) | $(HOST_TEST_DIR)
	$$(CC) $(3) -o $$@ $$^
endef

HOST_TEST_NAMES := \
	phase7 gfx boot lan http_tcp js webapi random tls libpeak ubin_registry \
	shell_split console_scroll display_present wallpaper_cache \
	peakdisk peakvec guiproto vmm_usercopy blobstore heap_pmm agent_policy \
	ctr_path dom vfs
HOST_TEST_BINS := $(addprefix $(HOST_TEST_DIR)/test_,$(HOST_TEST_NAMES))

test: test-host
# Compile host tests in parallel; run them sequentially for deterministic output.
test-host:
	@$(MAKE) -j$(HOST_TEST_JOBS) $(HOST_TEST_BINS)
	@for t in $(HOST_TEST_BINS); do $$t || exit 1; done

$(HOST_TEST_DIR):
	@mkdir -p $@

$(eval $(call HOST_TEST_RULE,phase7,tests/host/test_phase7.c kernel/vfs_path_util.c,\
	$(HOST_CFLAGS) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL)))
$(eval $(call HOST_TEST_RULE,gfx,tests/host/test_gfx.c,$(HOST_CFLAGS)))
$(eval $(call HOST_TEST_RULE,boot,tests/host/test_boot.c boot/common/elf_load.c boot/common/util.c,\
	$(HOST_CFLAGS) $(HOST_TEST_INC_BOOT)))
$(eval $(call HOST_TEST_RULE,lan,tests/host/test_lan.c kernel/net/dhcp_util.c kernel/net/http_util.c \
	kernel/net/arp_util.c boot/common/peak_conf.c boot/common/util.c,\
	$(HOST_CFLAGS) -DPEAK_HOST_TEST $(HOST_TEST_INC_BOOT)))
$(eval $(call HOST_TEST_RULE,http_tcp,tests/host/test_http_tcp.c kernel/net/http_util.c \
	kernel/net/tcp_util.c,\
	$(HOST_CFLAGS) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL)))
$(eval $(call HOST_TEST_RULE,js,tests/host/test_js.c tests/host/js_host_stubs.c \
	kernel/js/js_core.c kernel/js/js_compile.c kernel/js/js_lex.c \
	kernel/js/js_codegen.c kernel/js/js_parse.c kernel/js/js_vm.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL) -Ikernel/js))
$(eval $(call HOST_TEST_RULE,webapi,tests/host/test_webapi.c tests/host/js_host_stubs.c \
	tests/host/webapi_host_stubs.c kernel/gui/webapi.c kernel/gui/webapi_stubs.c \
	kernel/net/http_util.c kernel/js/js_core.c kernel/js/js_compile.c \
	kernel/js/js_lex.c kernel/js/js_codegen.c kernel/js/js_parse.c kernel/js/js_vm.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL) -Ikernel/js -Ikernel/gui))
$(eval $(call HOST_TEST_RULE,random,tests/host/test_random.c kernel/random.c \
	kernel/net/crypto.c kernel/net/crypto_hash.c kernel/net/crypto_sha384.c kernel/net/crypto_aead.c kernel/net/crypto_x25519.c \
	kernel/net/crypto_p256.c kernel/net/crypto_p384.c kernel/net/hacl_p384/Hacl_P384.c \
	kernel/net/crypto_rsa.c kernel/net/crypto_hkdf.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST -DPEAK_DEV_INSECURE_RNG=1 $(HOST_TEST_INC_HOST_BOOT_KERNEL) \
	-Ikernel/net/hacl_p384 -Ikernel/net/hacl_p384/karamel/include \
	-Ikernel/net/hacl_p384/karamel/krmllib/dist/minimal -DHACL_CAN_COMPILE_UINT128 \
	-Wno-unused-function))
$(eval $(call HOST_TEST_RULE,tls,tests/host/test_tls.c tests/host/tls_host_stubs.c \
	kernel/net/tls_util.c kernel/net/tls_trust.c kernel/net/tls_clienthello.c kernel/net/tls_session.c \
	kernel/net/tls_hsts.c kernel/net/tls_ech.c \
	kernel/net/x509.c \
	kernel/net/webpki.c kernel/net/webpki_roots_data.c kernel/random.c \
	kernel/net/crypto.c kernel/net/crypto_hash.c kernel/net/crypto_sha384.c kernel/net/crypto_aead.c kernel/net/crypto_x25519.c \
	kernel/net/crypto_p256.c kernel/net/crypto_p384.c kernel/net/hacl_p384/Hacl_P384.c \
	kernel/net/crypto_rsa.c kernel/net/crypto_hkdf.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST $(HOST_TEST_INC_HOST_BOOT_KERNEL) -Ikernel/net \
	-Ikernel/net/hacl_p384 -Ikernel/net/hacl_p384/karamel/include \
	-Ikernel/net/hacl_p384/karamel/krmllib/dist/minimal -DHACL_CAN_COMPILE_UINT128 \
	-Wno-unused-function))
$(eval $(call HOST_TEST_RULE,libpeak,tests/host/test_libpeak.c kernel/user/libpeak.c,\
	$(HOST_CFLAGS) -DPEAK_HOST_TEST))
$(eval $(call HOST_TEST_RULE,ubin_registry,tests/host/test_ubin_registry.c,$(HOST_CFLAGS)))
$(eval $(call HOST_TEST_RULE,shell_split,tests/host/test_shell_split.c kernel/shell_split.c,\
	$(HOST_CFLAGS) -DPEAK_HOST_TEST))
$(eval $(call HOST_TEST_RULE,console_scroll,tests/host/test_console_scroll.c kernel/console_scroll.c,\
	$(HOST_CFLAGS) -DPEAK_HOST_TEST))
$(eval $(call HOST_TEST_RULE,display_present,tests/host/test_display_present.c kernel/display_clip.c,\
	$(HOST_CFLAGS) -DPEAK_HOST_TEST))
$(eval $(call HOST_TEST_RULE,wallpaper_cache,tests/host/test_wallpaper_cache.c,$(HOST_CFLAGS)))
$(eval $(call HOST_TEST_RULE,peakdisk,tests/host/test_peakdisk.c,\
	$(HOST_CFLAGS) -DPEAK_HOST_TEST))
$(eval $(call HOST_TEST_RULE,peakvec,tests/host/test_peakvec.c tests/host/peakvec_host_stubs.c \
	kernel/peakvec.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL)))
$(eval $(call HOST_TEST_RULE,guiproto,tests/host/test_guiproto.c tests/host/guiproto_host_stubs.c \
	kernel/gui/guiproto.c kernel/gui/surface.c kernel/display_clip.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL) -Ikernel/gui \
	-DSURFACE_BUDGET_BYTES=524288))
$(eval $(call HOST_TEST_RULE,vmm_usercopy,tests/host/test_vmm_usercopy.c tests/host/vmm_host_stubs.c \
	kernel/vmm.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL)))
$(eval $(call HOST_TEST_RULE,blobstore,tests/host/test_blobstore.c tests/host/blobstore_host_stubs.c \
	kernel/blobstore.c kernel/blockdev.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST -DBLOBSTORE_CACHE_PAGES=4 $(HOST_TEST_INC_KERNEL)))
$(eval $(call HOST_TEST_RULE,heap_pmm,tests/host/test_heap_pmm.c tests/host/heap_pmm_host_stubs.c \
	kernel/heap.c kernel/pmm.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL) -Iboot/include))
$(eval $(call HOST_TEST_RULE,agent_policy,tests/host/test_agent_policy.c tests/host/agent_host_stubs.c \
	kernel/agent_policy.c kernel/agent_tools.c kernel/agent_planner.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL) -Ikernel))
$(eval $(call HOST_TEST_RULE,ctr_path,tests/host/test_ctr_path.c kernel/ctr_path.c,\
	$(HOST_CFLAGS) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL) -Ikernel))
$(eval $(call HOST_TEST_RULE,dom,tests/host/test_dom.c tests/host/dom_host_stubs.c \
	kernel/gui/dom_core.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST $(HOST_TEST_INC_KERNEL) -Ikernel/gui))
$(eval $(call HOST_TEST_RULE,vfs,tests/host/test_vfs.c tests/host/vfs_host_stubs.c \
	kernel/vfs.c kernel/vfs_peakfs.c kernel/vfs_path_util.c kernel/blobstore.c kernel/blockdev.c,\
	$(HOST_CFLAGS_REDECL) -DPEAK_HOST_TEST -DBLOBSTORE_CACHE_PAGES=4 $(HOST_TEST_INC_KERNEL)))

smoke:
	./scripts/smoke-cli.sh
	$(MAKE) test-host

smoke-qemu: smoke-bios

smoke-bios: iso
	chmod +x scripts/smoke-qemu.sh
	PEAK_FIRMWARE=bios ./scripts/smoke-qemu.sh

smoke-uefi: iso
	chmod +x scripts/smoke-qemu.sh
	PEAK_FIRMWARE=uefi ./scripts/smoke-qemu.sh

smoke-peakfs: iso
	chmod +x scripts/smoke-peakfs.sh
	./scripts/smoke-peakfs.sh

smoke-tls-live:
	chmod +x scripts/smoke-tls-live.sh
	./scripts/smoke-tls-live.sh

purity:
	chmod +x scripts/purity-check.sh
	./scripts/purity-check.sh

doctor:
	chmod +x scripts/doctor.sh
	./scripts/doctor.sh $(ARCH) $(if $(filter aarch64,$(ARCH)),$(or $(PLATFORM),rpi),$(or $(PLATFORM),pc))

firmware-fetch:
	chmod +x scripts/fetch-rpi-firmware.sh
	./scripts/fetch-rpi-firmware.sh

kernel: $(KERNEL_ELF)
bootloaders: $(BIOS_BIN) $(UEFI_EFI) $(ESP_IMG)

$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# HACL* P-384: freestanding libc shims first, then HACL/Karamel headers.
HACL_P384_CFLAGS := -Ikernel/net/hacl_p384/freestanding -Ikernel/net/hacl_p384 \
	-Ikernel/net/hacl_p384/karamel/include \
	-Ikernel/net/hacl_p384/karamel/krmllib/dist/minimal -DHACL_CAN_COMPILE_UINT128 \
	-Wno-unused-function -Wno-unused-parameter

$(BUILD)/kernel/net/hacl_p384/Hacl_P384.o: kernel/net/hacl_p384/Hacl_P384.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(HACL_P384_CFLAGS) -c $< -o $@

$(BUILD)/kernel/net/crypto_p384.o: kernel/net/crypto_p384.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(HACL_P384_CFLAGS) -c $< -o $@

$(BUILD)/boot/rpi/%.o: boot/rpi/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BOOT_SHIM_CFLAGS) -c $< -o $@

$(BUILD)/kernel/wallpaper_data.o: kernel/wallpaper_data.S assets/wallpapers/evergreen.ppm
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) $< -o $@

$(BUILD)/kernel/%.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS) $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)
	@chmod +x scripts/check-kernel-size.sh
	./scripts/check-kernel-size.sh $@

ifeq ($(ARCH),aarch64)
kernel8: $(KERNEL8_IMG)
$(KERNEL8_IMG): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "Built $@"

pi-image: firmware-fetch $(KERNEL8_IMG)
	chmod +x scripts/mkpiimg.py
	python3 scripts/mkpiimg.py $(KERNEL8_IMG) $(PI_IMAGE) \
		--config boot/rpi/config.txt \
		--cmdline boot/rpi/cmdline.txt \
		--firmware-dir third_party/rpi-firmware
	$(MAKE) ARCH=aarch64 pi-image-check

pi-image-check: $(PI_IMAGE)
	chmod +x scripts/pi-image-check.py
	python3 scripts/pi-image-check.py $(PI_IMAGE)

flash-pi:
	chmod +x scripts/flash-pi.sh
	./scripts/flash-pi.sh $(DEVICE) $(PI_IMAGE)

run-aarch64-virt: $(KERNEL8_IMG)
	chmod +x scripts/run-qemu-aarch64.sh
	PEAK_QEMU_MACHINE=virt ./scripts/run-qemu-aarch64.sh

smoke-aarch64: $(KERNEL8_IMG)
	chmod +x scripts/smoke-aarch64.sh
	./scripts/smoke-aarch64.sh
else
kernel8 pi-image pi-image-check flash-pi run-aarch64-virt smoke-aarch64:
	$(error $@ requires ARCH=aarch64 (got ARCH=$(ARCH)))
endif

# Header dependency files from -MMD
KERNEL_DEPS := $(KERNEL_OBJS:.o=.d)
-include $(KERNEL_DEPS)

# ---- BIOS loader (32-bit flat binary at 0x7C00) ----
$(BUILD)/boot/bios/%.o: boot/bios/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BIOS_CFLAGS) -c $< -o $@

$(BUILD)/boot/common/%.o: boot/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BIOS_CFLAGS) -c $< -o $@

$(BUILD)/boot/bios/%.o: boot/bios/%.S
	@mkdir -p $(dir $@)
	$(CC) $(BIOS_ASFLAGS) $< -o $@

$(BIOS_ELF): $(BIOS_OBJS) boot/bios/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(BIOS_LDFLAGS) -o $@ $(BIOS_OBJS)

$(BIOS_BIN): $(BIOS_ELF)
	$(OBJCOPY) -O binary $< $@
	@python3 -c "import os; p='$@'; n=os.path.getsize(p); pad=(512-((n%512) or 512))%512; open(p,'ab').write(b'\\0'*pad); print(f'BIOS loader {n+pad} bytes')"

$(BUILD)/uefi/efi_main.o: boot/uefi/efi_main.c
	@mkdir -p $(dir $@)
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(BUILD)/uefi/util.o: boot/common/util.c
	@mkdir -p $(dir $@)
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(BUILD)/uefi/elf_load.o: boot/common/elf_load.c
	@mkdir -p $(dir $@)
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(BUILD)/uefi/paging.o: boot/common/paging.c
	@mkdir -p $(dir $@)
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(BUILD)/uefi/load_ctx.o: boot/common/load_ctx.c
	@mkdir -p $(dir $@)
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(BUILD)/uefi/peak_conf.o: boot/common/peak_conf.c
	@mkdir -p $(dir $@)
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(UEFI_EFI): $(BUILD)/uefi/efi_main.o $(BUILD)/uefi/util.o $(BUILD)/uefi/elf_load.o \
             $(BUILD)/uefi/paging.o $(BUILD)/uefi/load_ctx.o $(BUILD)/uefi/peak_conf.o
	@mkdir -p $(dir $@)
	$(LLDLINK) /subsystem:efi_application /entry:efi_main /out:$@ \
		$(BUILD)/uefi/efi_main.o $(BUILD)/uefi/util.o \
		$(BUILD)/uefi/elf_load.o $(BUILD)/uefi/paging.o $(BUILD)/uefi/load_ctx.o \
		$(BUILD)/uefi/peak_conf.o

$(ESP_IMG): $(UEFI_EFI) $(KERNEL_ELF) boot/peak.conf scripts/mkesp.py
	python3 scripts/mkesp.py $(UEFI_EFI) $@ --size-kib 8192 --kernel $(KERNEL_ELF) \
		--extra boot/peak.conf:PEAK.CONF

iso: $(ISO)

$(ISO): $(KERNEL_ELF) $(BIOS_BIN) $(ESP_IMG) $(UEFI_EFI) boot/peak.conf
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot $(ISO_ROOT)/EFI/BOOT $(ISO_ROOT)/EFI/PEAK
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/kernel.elf
	cp $(KERNEL_ELF) $(ISO_ROOT)/EFI/PEAK/KERNEL.ELF
	cp $(BIOS_BIN) $(ISO_ROOT)/boot/peak-bios.bin
	cp $(ESP_IMG) $(ISO_ROOT)/boot/efi.img
	cp $(UEFI_EFI) $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	cp boot/peak.conf $(ISO_ROOT)/boot/peak.conf
	@BLS=$$(( ($$(stat -f%z $(BIOS_BIN) 2>/dev/null || stat -c%s $(BIOS_BIN)) + 511) / 512 )); \
	xorriso -as mkisofs -R -r -J \
		-V "PEAKOS" \
		-b boot/peak-bios.bin \
		-no-emul-boot -boot-load-size $$BLS -boot-info-table \
		-eltorito-alt-boot \
		-e boot/efi.img -no-emul-boot \
		-isohybrid-gpt-basdat \
		--efi-boot-part --efi-boot-image \
		$(ISO_ROOT) -o $(ISO)
	@echo "Built $(ISO)"

run: iso
	./scripts/run-qemu.sh

clean:
	rm -rf build
