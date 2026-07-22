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

# ---- Shared portable kernel sources ----
KERNEL_COMMON_SRCS := \
	kernel/boot.c \
	kernel/serial.c \
	kernel/fb.c \
	kernel/display.c \
	kernel/console.c \
	kernel/pmm.c \
	kernel/heap.c \
	kernel/vmm.c \
	kernel/vfs.c \
	kernel/sched.c \
	kernel/elf.c \
	kernel/syscall.c \
	kernel/agent.c \
	kernel/timer.c \
	kernel/keyboard.c \
	kernel/mouse.c \
	kernel/shell.c \
	kernel/theme.c \
	kernel/wallpaper.c \
	kernel/settings.c \
	kernel/sync.c \
	kernel/util.c \
	kernel/ctr.c \
	kernel/sysmon.c \
	kernel/clipboard.c \
	kernel/notify.c \
	kernel/peakdisk.c \
	kernel/blobstore.c \
	kernel/peakvec.c \
	kernel/guiproto.c \
	kernel/irq.c \
	kernel/blockdev.c \
	kernel/netdev.c \
	kernel/fdt.c \
	kernel/js/js_core.c \
	kernel/js/js_compile.c \
	kernel/js/js_vm.c \
	kernel/gui/dom.c \
	kernel/gui/css.c \
	kernel/gui/browser_js.c \
	kernel/gui/webapi.c \
	kernel/net/dhcp_util.c \
	kernel/net/http_util.c \
	kernel/net/crypto.c \
	kernel/net/tls.c \
	kernel/net/net.c \
	kernel/random.c \
	kernel/cap.c \
	kernel/stackchk.c \
	kernel/user/libpeak.c \
	kernel/user/ubin.c \
	kernel/user/utils_file.c \
	kernel/user/utils_text.c \
	kernel/user/utils_sys.c \
	kernel/user/utils_ctr.c \
	kernel/user/utils_js.c \
	kernel/gui/font.c \
	kernel/gui/window.c \
	kernel/gui/surface.c \
	kernel/gui/desktop.c \
	kernel/gui/game.c \
	kernel/gui/browser.c \
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
           -Ikernel/include -Iboot/include
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
	kernel/drivers/net/e1000_nd.c

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
           -Ikernel/include -Iboot/include -Ikernel/platform/rpi \
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
	boot/common/peak_conf.c

BIOS_S_SRCS := boot/bios/entry.S boot/bios/lm_enter.S
BIOS_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(BIOS_C_SRCS)) \
             $(patsubst %.S,$(BUILD)/%.o,$(BIOS_S_SRCS))

UEFI_SRCS := \
	boot/uefi/efi_main.c \
	boot/common/util.c \
	boot/common/elf_load.c \
	boot/common/paging.c
UEFI_OBJS := $(patsubst %.c,$(BUILD)/uefi/%.o,$(notdir $(UEFI_SRCS)))

.PHONY: all iso kernel bootloaders run clean test test-host smoke smoke-qemu \
        smoke-bios smoke-uefi purity doctor pi-image pi-image-check flash-pi \
        kernel8 run-aarch64-virt smoke-aarch64 firmware-fetch

ifeq ($(ARCH),x86_64)
all: iso
else
all: pi-image
endif

test: test-host
test-host:
	@mkdir -p $(BUILD)/tests
	$(CC) -std=c11 -Wall -Wextra -O2 \
		-o $(BUILD)/tests/test_phase7 tests/host/test_phase7.c
	$(CC) -std=c11 -Wall -Wextra -O2 \
		-o $(BUILD)/tests/test_gfx tests/host/test_gfx.c
	$(CC) -std=c11 -Wall -Wextra -O2 -Iboot/include \
		-o $(BUILD)/tests/test_boot tests/host/test_boot.c \
		boot/common/elf_load.c boot/common/util.c
	$(CC) -std=c11 -Wall -Wextra -O2 -DPEAK_HOST_TEST \
		-Iboot/include \
		-o $(BUILD)/tests/test_lan tests/host/test_lan.c \
		kernel/net/dhcp_util.c kernel/net/http_util.c \
		boot/common/peak_conf.c boot/common/util.c
	$(CC) -std=c11 -Wall -Wextra -Wno-incompatible-library-redeclaration -O2 \
		-DPEAK_HOST_TEST \
		-Itests/host/include -Ikernel/include -Ikernel/js \
		-o $(BUILD)/tests/test_js tests/host/test_js.c \
		tests/host/js_host_stubs.c \
		kernel/js/js_core.c kernel/js/js_compile.c kernel/js/js_vm.c
	$(CC) -std=c11 -Wall -Wextra -Wno-incompatible-library-redeclaration -O2 \
		-DPEAK_HOST_TEST -DPEAK_DEV_INSECURE_RNG=1 \
		-Itests/host/include -Iboot/include -Ikernel/include \
		-o $(BUILD)/tests/test_random tests/host/test_random.c \
		kernel/random.c kernel/net/crypto.c
	$(BUILD)/tests/test_phase7
	$(BUILD)/tests/test_gfx
	$(BUILD)/tests/test_boot
	$(BUILD)/tests/test_lan
	$(BUILD)/tests/test_js
	$(BUILD)/tests/test_random

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

$(BUILD)/uefi/peak_conf.o: boot/common/peak_conf.c
	@mkdir -p $(dir $@)
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(UEFI_EFI): $(BUILD)/uefi/efi_main.o $(BUILD)/uefi/util.o $(BUILD)/uefi/elf_load.o \
             $(BUILD)/uefi/paging.o $(BUILD)/uefi/peak_conf.o
	@mkdir -p $(dir $@)
	$(LLDLINK) /subsystem:efi_application /entry:efi_main /out:$@ \
		$(BUILD)/uefi/efi_main.o $(BUILD)/uefi/util.o \
		$(BUILD)/uefi/elf_load.o $(BUILD)/uefi/paging.o \
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
