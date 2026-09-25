# SYPAS build system
# ===================
# Targets:
#   make                 build bootloader + kernel + ESP + ISO
#   make bootloader      SYPAS UEFI loader (BOOTX64.EFI)
#   make biosstub        legacy-BIOS diagnostic image (biosstub.bin)
#   make kernel          SYPAS kernel (kernel.elf)
#   make esp             FAT16 EFI system partition image
#   make iso             dual-entry bootable ISO (release/sypas-$(VERSION).iso)
#   make run             boot the ISO in QEMU (normal profile: 4 CPU/4G)
#   make run-lowend      boot with the low-end profile (2 CPU/2G)
#   make test-media      validate ISO/catalog/FAT and execute BIOS stub
#   make test-boot       automated UEFI boot test matrix (machine-readable)
#   make clean
#
# Toolchain: gcc + binutils (see docs/toolchain.md).  QEMU + OVMF paths
# are overridable for local setups.

VERSION := 0.1.0

CC      := gcc
LD      := ld
OBJCOPY := objcopy
PYTHON  := python3

BUILD   := build
RELEASE := release
ISO     := $(RELEASE)/sypas-$(VERSION).iso
BIOSSTUB := $(BUILD)/biosstub.bin

QEMU      ?= $(HOME)/sysroot/bin/qemu-system-x86_64
OVMF_CODE ?= $(HOME)/firmware/OVMF_CODE.fd
OVMF_VARS ?= $(HOME)/firmware/OVMF_VARS.fd

# ---- Legacy BIOS diagnostic image -------------------------------------------

BIOS_DIR := bootloader/bios

$(BUILD)/bios/stub.o: $(BIOS_DIR)/stub.S
	@mkdir -p $(dir $@)
	$(CC) -m32 -c $< -o $@

$(BIOSSTUB): $(BUILD)/bios/stub.o
	@mkdir -p $(dir $@)
	$(LD) -m elf_i386 -Ttext 0x7C00 --oformat binary -e start -o $@ $<
	@test "$$(wc -c < $@)" -eq 2048

# ---- Bootloader (PE32+ via ELF shared object + objcopy) --------------------

LDR_DIR  := bootloader/uefi
LDR_SRCS := $(LDR_DIR)/main.c $(LDR_DIR)/reloc.c
LDR_OBJS := $(BUILD)/loader/main.o $(BUILD)/loader/reloc.o $(BUILD)/loader/start.o

LDR_CFLAGS := -std=c17 -O2 -Wall -Wextra -ffreestanding -fpic -fshort-wchar \
              -fno-stack-protector -fno-stack-check -mno-red-zone \
              -fno-asynchronous-unwind-tables -nostdlib -MMD

# ---- Kernel -----------------------------------------------------------------

KRN_CSRCS := kernel/main.c \
             kernel/arch/x86_64/gdt.c \
             kernel/arch/x86_64/idt.c \
             kernel/arch/x86_64/pic.c \
             kernel/arch/x86_64/pit.c \
             kernel/arch/x86_64/serial.c \
             kernel/arch/x86_64/cpu.c \
             kernel/mm/pmm.c \
             kernel/graphics/fbcon.c \
             kernel/debug/kprintf.c \
             kernel/debug/panic.c
KRN_ASRCS := kernel/arch/x86_64/entry.S kernel/arch/x86_64/isr.S
KRN_OBJS  := $(patsubst %.c,$(BUILD)/%.o,$(KRN_CSRCS)) \
             $(patsubst %.S,$(BUILD)/%.o,$(KRN_ASRCS))

KRN_CFLAGS := -std=c17 -O2 -g -Wall -Wextra -ffreestanding -fno-pic -fno-pie \
              -fno-stack-protector -fno-stack-check -mno-red-zone \
              -mgeneral-regs-only -mcmodel=small \
              -fno-asynchronous-unwind-tables -nostdlib -MMD $(EXTRA_KCFLAGS)

.PHONY: all bootloader biosstub kernel esp iso run run-lowend test-media test-boot benchmark clean

all: iso

bootloader: $(BUILD)/BOOTX64.EFI
biosstub:   $(BIOSSTUB)
kernel:     $(BUILD)/kernel.elf
esp:        $(BUILD)/esp.img
iso:        $(ISO)

# Bootloader
$(BUILD)/loader/%.o: $(LDR_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LDR_CFLAGS) -c $< -o $@

$(BUILD)/loader/start.o: $(LDR_DIR)/start.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

$(BUILD)/loader.so: $(LDR_OBJS) $(LDR_DIR)/loader.ld
	$(LD) -shared -Bsymbolic -z nocombreloc -T $(LDR_DIR)/loader.ld \
	      -o $@ $(LDR_OBJS)
	@if $(LD) --version >/dev/null && nm -u $@ | grep -q .; then \
	    echo "loader has undefined symbols:"; nm -u $@; exit 1; fi

$(BUILD)/BOOTX64.EFI: $(BUILD)/loader.so
	$(OBJCOPY) -j .text -j .reloc -j .data -j .dynamic -j .rela \
	           -j .dynsym -j .dynstr -j .hash -j .gnu.hash \
	           --target efi-app-x86_64 $< $@

# Kernel
$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KRN_CFLAGS) -c $< -o $@

$(BUILD)/kernel/%.o: kernel/%.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

$(BUILD)/kernel.elf: $(KRN_OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -nostdlib -static -o $@ $(KRN_OBJS)

# Images
$(BUILD)/esp.img: $(BUILD)/BOOTX64.EFI $(BUILD)/kernel.elf tools/mkfat.py
	$(PYTHON) tools/mkfat.py $@ 4 \
	    $(BUILD)/BOOTX64.EFI=/EFI/BOOT/BOOTX64.EFI \
	    $(BUILD)/kernel.elf=/SYPAS/KERNEL.ELF

$(ISO): $(BUILD)/biosstub.bin $(BUILD)/esp.img tools/mkiso.py
	@mkdir -p $(RELEASE)
	$(PYTHON) tools/mkiso.py $@ $(BUILD)/esp.img $(BUILD)/biosstub.bin \
	    '$(BUILD)/kernel.elf=/KERNEL.ELF;1'
	@ls -la $(ISO)

# Run / test
run: iso
	scripts/run-qemu.sh normal $(ISO)

run-lowend: iso
	scripts/run-qemu.sh lowend $(ISO)

test-media: $(ISO)
	$(PYTHON) tests/media/validate_media.py --iso $(ISO) \
	    --bios $(BUILD)/biosstub.bin --esp $(BUILD)/esp.img \
	    --bootloader $(BUILD)/BOOTX64.EFI --kernel $(BUILD)/kernel.elf
	$(PYTHON) tests/media/test_bios_stub.py --stub $(BUILD)/biosstub.bin

test-boot: iso
	$(PYTHON) tests/boot/boot_test.py --iso $(ISO) \
	    --qemu $(QEMU) --ovmf-code $(OVMF_CODE) --ovmf-vars $(OVMF_VARS)

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
