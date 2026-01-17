# BlitzOS Makefile - Build system for x86-64 operating system
# Cross-compiler toolchain
AS = nasm
CC = /usr/local/cross/bin/x86_64-elf-gcc
LD = /usr/local/cross/bin/x86_64-elf-ld

# Compiler and assembler flags
ASFLAGS = -f elf64
CFLAGS = -ffreestanding -O2 -Wall -Wextra -std=c11 -mno-red-zone -mcmodel=large -mno-mmx -mno-sse -mno-sse2
LDFLAGS = -T scripts/linker.ld -nostdlib -z max-page-size=0x1000

# Directories
KERNEL_DIR = kernel
ARCH_DIR = $(KERNEL_DIR)/arch/x86_64
BOOT_DIR = $(ARCH_DIR)/boot
CORE_DIR = $(KERNEL_DIR)/core
DRIVERS_DIR = drivers
BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/iso
ISOBOOT_DIR = $(ISO_DIR)/boot
GRUB_DIR = $(ISOBOOT_DIR)/grub

# Source files
ASM_SOURCES = $(BOOT_DIR)/boot.asm $(ARCH_DIR)/idt_load.asm $(ARCH_DIR)/isr.asm $(ARCH_DIR)/context_switch.asm $(KERNEL_DIR)/sys/syscall_asm.asm
C_SOURCES = $(CORE_DIR)/kernel.c $(KERNEL_DIR)/boot/multiboot2.c $(KERNEL_DIR)/mm/pmm.c $(KERNEL_DIR)/mm/paging.c $(KERNEL_DIR)/mm/kheap.c $(KERNEL_DIR)/proc/process.c $(DRIVERS_DIR)/vga.c $(DRIVERS_DIR)/pit.c $(DRIVERS_DIR)/keyboard.c $(ARCH_DIR)/idt.c $(ARCH_DIR)/interrupts.c $(KERNEL_DIR)/sys/syscall.c $(KERNEL_DIR)/test_syscalls.c

# Object files
ASM_OBJECTS = $(BUILD_DIR)/boot.o $(BUILD_DIR)/idt_load.o $(BUILD_DIR)/isr.o $(BUILD_DIR)/context_switch.o $(BUILD_DIR)/syscall_asm.o
C_OBJECTS = $(BUILD_DIR)/kernel.o $(BUILD_DIR)/multiboot2.o $(BUILD_DIR)/pmm.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/kheap.o $(BUILD_DIR)/process.o $(BUILD_DIR)/vga.o $(BUILD_DIR)/pit.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/idt.o $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/test_syscalls.o

ALL_OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Output
KERNEL_BIN = $(ISOBOOT_DIR)/kernel.bin
ISO_FILE = BlitzOS.iso

# Default target
.PHONY: all
all: $(ISO_FILE)

# Create build directories
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(ISOBOOT_DIR)
	@mkdir -p $(GRUB_DIR)

# Compile assembly files
$(BUILD_DIR)/boot.o: $(BOOT_DIR)/boot.asm | $(BUILD_DIR)
	@echo "[AS] $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Compile C files
$(BUILD_DIR)/kernel.o: $(CORE_DIR)/kernel.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/multiboot2.o: $(KERNEL_DIR)/boot/multiboot2.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pmm.o: $(KERNEL_DIR)/mm/pmm.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: $(KERNEL_DIR)/mm/paging.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kheap.o: $(KERNEL_DIR)/mm/kheap.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vga.o: $(DRIVERS_DIR)/vga.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pit.o: $(DRIVERS_DIR)/pit.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: $(DRIVERS_DIR)/keyboard.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: $(ARCH_DIR)/idt.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts.o: $(ARCH_DIR)/interrupts.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt_load.o: $(ARCH_DIR)/idt_load.asm | $(BUILD_DIR)
	@echo "[AS] $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/isr.o: $(ARCH_DIR)/isr.asm | $(BUILD_DIR)
	@echo "[AS] $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/context_switch.o: $(ARCH_DIR)/context_switch.asm | $(BUILD_DIR)
	@echo "[AS] $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/syscall_asm.o: $(KERNEL_DIR)/sys/syscall_asm.asm | $(BUILD_DIR)
	@echo "[AS] $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/process.o: $(KERNEL_DIR)/proc/process.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall.o: $(KERNEL_DIR)/sys/syscall.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_syscalls.o: $(KERNEL_DIR)/test_syscalls.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Link kernel
$(KERNEL_BIN): $(ALL_OBJECTS) scripts/linker.ld | $(BUILD_DIR)
	@echo "[LD] Linking kernel..."
	@$(LD) $(LDFLAGS) $(ALL_OBJECTS) -o $@
	@echo "[OK] Kernel binary created: $@"

# Create ISO with GRUB
$(ISO_FILE): $(KERNEL_BIN) boot/grub/grub.cfg
	@echo "[GRUB] Creating bootable ISO..."
	@cp boot/grub/grub.cfg $(GRUB_DIR)/grub.cfg
	@grub-mkrescue -o $(ISO_FILE) $(ISO_DIR) 2>/dev/null
	@echo "[OK] Bootable ISO created: $(ISO_FILE)"

# Run in QEMU
.PHONY: run
run: $(ISO_FILE)
	@echo "[QEMU] Starting BlitzOS..."
	@qemu-system-x86_64 -cdrom $(ISO_FILE)

# Run with serial output (useful for debugging)
.PHONY: run-serial
run-serial: $(ISO_FILE)
	@echo "[QEMU] Starting BlitzOS with serial output..."
	@qemu-system-x86_64 -cdrom $(ISO_FILE) -serial stdio

# Debug with QEMU (waits for GDB connection on port 1234)
.PHONY: debug
debug: $(ISO_FILE)
	@echo "[QEMU] Starting in debug mode (waiting for GDB on port 1234)..."
	@qemu-system-x86_64 -cdrom $(ISO_FILE) -s -S

# Clean build artifacts
.PHONY: clean
clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f $(ISO_FILE)
	@echo "[OK] Clean complete"

# Clean everything including downloads
.PHONY: distclean
distclean: clean
	@echo "[DISTCLEAN] Removing all generated files..."
	@echo "[OK] Distclean complete"

# Display help
.PHONY: help
help:
	@echo "BlitzOS Build System"
	@echo "================="
	@echo "Available targets:"
	@echo "  all          - Build kernel and create bootable ISO (default)"
	@echo "  run          - Build and run OS in QEMU"
	@echo "  run-serial   - Build and run OS in QEMU with serial output"
	@echo "  debug        - Build and start QEMU in debug mode for GDB"
	@echo "  clean        - Remove build artifacts"
	@echo "  distclean    - Remove all generated files"
	@echo "  help         - Display this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make           # Build the OS"
	@echo "  make run       # Build and run in QEMU"
	@echo "  make clean     # Clean build files"

# Show build configuration
.PHONY: config
config:
	@echo "Build Configuration:"
	@echo "==================="
	@echo "AS      = $(AS)"
	@echo "CC      = $(CC)"
	@echo "LD      = $(LD)"
	@echo "ASFLAGS = $(ASFLAGS)"
	@echo "CFLAGS  = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"
