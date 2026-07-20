# Peak OS — freestanding x86_64 kernel
KERNEL_NAME := peak-os
BUILD       := build
ISO_ROOT    := $(BUILD)/iso_root
ISO         := $(BUILD)/$(KERNEL_NAME).iso
KERNEL_ELF  := $(BUILD)/kernel.elf

LIMINE_DIR  := third_party/limine
LIMINE_BIN  := $(LIMINE_DIR)/limine

CC      := clang
LD      := ld.lld
CFLAGS  := -target x86_64-unknown-none-elf \
           -ffreestanding -fno-stack-protector -fno-stack-check \
           -fno-PIC -fno-PIE -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
           -mno-red-zone -mcmodel=kernel \
           -Wall -Wextra -Werror -std=c11 -O2 -g \
           -Ikernel/include -I$(LIMINE_DIR)
ASFLAGS := -target x86_64-unknown-none-elf -c
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -T linker.ld

KERNEL_SRCS := \
	kernel/boot.c \
	kernel/serial.c \
	kernel/fb.c \
	kernel/console.c \
	kernel/pmm.c \
	kernel/idt.c \
	kernel/pic.c \
	kernel/timer.c \
	kernel/keyboard.c \
	kernel/mouse.c \
	kernel/shell.c \
	kernel/util.c \
	kernel/gui/font.c \
	kernel/gui/window.c \
	kernel/gui/desktop.c

KERNEL_ASMS := kernel/isr.S

KERNEL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_SRCS)) \
               $(patsubst %.S,$(BUILD)/%.o,$(KERNEL_ASMS))

.PHONY: all iso kernel run clean limine

all: iso

limine: $(LIMINE_BIN)

$(LIMINE_BIN):
	$(MAKE) -C $(LIMINE_DIR)

kernel: $(KERNEL_ELF)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

iso: $(ISO)

$(ISO): $(KERNEL_ELF) $(LIMINE_BIN) limine.conf
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot $(ISO_ROOT)/EFI/BOOT $(ISO_ROOT)/boot/limine
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/kernel.elf
	cp limine.conf $(ISO_ROOT)/boot/limine/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_ROOT) -o $(ISO)
	$(LIMINE_BIN) bios-install $(ISO)
	@echo "Built $(ISO)"

run: iso
	./scripts/run-qemu.sh

clean:
	rm -rf $(BUILD)
