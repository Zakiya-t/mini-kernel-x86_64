# ==============================================================================
# Makefile - Unified Educational MiniKernel (Pure x86-64)
# ==============================================================================

CC := gcc
LD := ld
NASM := nasm

# Kernel compilation flags
CFLAGS := -std=gnu11 -m64 -ffreestanding -fno-builtin -fno-stack-protector \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -Wall -Wextra -O2 -g -Iinclude -Ikernel -I.

# Pure 64-bit assembly and linker flags
NASMFLAGS := -f elf64
LDFLAGS := -n -T linker.ld -m elf_x86_64

BUILD_DIR := build
BIN_DIR := bin
ISO_DIR := iso
DISK_IMG := disk.img

# ── Pure Ring 0 Kernel sources (compiled into kernel.bin) ─────────────────────
C_SRCS := $(wildcard kernel/core/*.c) \
          $(wildcard kernel/arch/x86_64/*.c) \
          $(wildcard kernel/memory/*.c) \
          $(wildcard kernel/process/*.c) \
          $(wildcard kernel/syscall/*.c) \
          $(wildcard kernel/drivers/*.c) \
          $(wildcard kernel/fs/*.c) \
          $(wildcard kernel/exec/*.c)

ASM_SRCS := boot/boot.asm \
            kernel/arch/x86_64/gdt_asm.asm \
            kernel/arch/x86_64/idt_asm.asm \
            kernel/process/context_switch.asm

C_OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, $(C_SRCS))
ASM_OBJS := $(patsubst %.asm, $(BUILD_DIR)/%.o, $(ASM_SRCS))

OBJS := $(ASM_OBJS) $(C_OBJS)
KERNEL_BIN := $(BIN_DIR)/kernel.bin
KERNEL_ISO := $(BIN_DIR)/minikernel.iso

.PHONY: all clean qemu qemu-display iso dirs disk myshell

all: dirs $(KERNEL_BIN) iso disk myshell

myshell:
	@$(MAKE) -C myshell-indp
	@python3 scripts/install_shell.py 2>/dev/null || python scripts/install_shell.py

dirs:
	@mkdir -p $(BUILD_DIR)/boot
	@mkdir -p $(BUILD_DIR)/kernel/core
	@mkdir -p $(BUILD_DIR)/kernel/arch/x86_64
	@mkdir -p $(BUILD_DIR)/kernel/memory
	@mkdir -p $(BUILD_DIR)/kernel/process
	@mkdir -p $(BUILD_DIR)/kernel/syscall
	@mkdir -p $(BUILD_DIR)/kernel/drivers
	@mkdir -p $(BUILD_DIR)/kernel/fs
	@mkdir -p $(BUILD_DIR)/kernel/exec
	@mkdir -p $(BUILD_DIR)/user/lib
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub

$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_BIN): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@
	@echo "=========================================================="
	@echo " [SUCCESS] Pure 64-bit ELF Kernel built: $@"
	@readelf -h $@ | grep -E "Class|Machine"
	@echo "=========================================================="

iso: dirs $(KERNEL_BIN)
	@cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
	grub-mkrescue -o $(KERNEL_ISO) $(ISO_DIR) 2>/dev/null || grub-mkrescue -o $(KERNEL_ISO) $(ISO_DIR)
	@echo "=========================================================="
	@echo " [SUCCESS] Bootable GRUB ISO created: $(KERNEL_ISO)"
	@echo "=========================================================="

disk:
	@if [ ! -f $(DISK_IMG) ]; then \
		echo "Creating 1 GiB virtual disk image..."; \
		dd if=/dev/zero of=$(DISK_IMG) bs=1M count=1024 status=none; \
	fi

# Boot pure 64-bit kernel in QEMU via serial console with 512M RAM and 1G disk
qemu: all
	qemu-system-x86_64 -cdrom $(KERNEL_ISO) -m 512M \
		-drive file=$(DISK_IMG),format=raw,index=0,media=disk,file.locking=off \
		-chardev stdio,id=char0,signal=off -serial chardev:char0 -display none -no-reboot

# Boot in QEMU with VGA graphical display window
qemu-display: all
	qemu-system-x86_64 -cdrom $(KERNEL_ISO) -m 512M \
		-drive file=$(DISK_IMG),format=raw,index=0,media=disk,file.locking=off \
		-chardev stdio,id=char0,signal=off -serial chardev:char0 -no-reboot

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(ISO_DIR)/boot/kernel.bin
