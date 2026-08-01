# BlitzOS build system
#
# Sources are discovered automatically, so adding a .c or .asm file anywhere
# under kernel/ or drivers/ just works - no Makefile edit needed. The previous
# version listed every object by hand, which meant a new file silently failed
# to link until someone remembered to add it in three places.

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------

AS := nasm
CC := /usr/local/cross/bin/x86_64-elf-gcc
LD := /usr/local/cross/bin/x86_64-elf-ld

ASFLAGS := -f elf64

CFLAGS := -ffreestanding -O2 -std=c11 \
          -Wall -Wextra -Werror=implicit-function-declaration \
          -mno-red-zone -mcmodel=large \
          -mno-mmx -mno-sse -mno-sse2 \
          -fno-stack-protector -fno-pic \
          -fno-omit-frame-pointer \
          -MMD -MP

# -mno-red-zone   : interrupt handlers would clobber the 128-byte red zone
# -mcmodel=large  : no assumptions about the kernel fitting in the low 2 GB
# -mno-sse*       : no FPU state is saved across context switches yet
# -fno-omit-frame-pointer : keeps RBP chained so panic() can walk the stack

LDFLAGS := -T scripts/linker.ld -nostdlib -z max-page-size=0x1000

# ---------------------------------------------------------------------------
# Layout
# ---------------------------------------------------------------------------

BUILD_DIR   := build
ISO_DIR     := $(BUILD_DIR)/iso
ISOBOOT_DIR := $(ISO_DIR)/boot
GRUB_DIR    := $(ISOBOOT_DIR)/grub

KERNEL_BIN := $(ISOBOOT_DIR)/kernel.bin
ISO_FILE   := BlitzOS.iso

# ---------------------------------------------------------------------------
# Source discovery
#
# Object files are named after the full source path with '/' turned into '_',
# so kernel/mm/pmm.c and drivers/pmm.c could coexist without clobbering
# each other's .o.
# ---------------------------------------------------------------------------

C_SOURCES   := $(shell find kernel drivers -name '*.c' 2>/dev/null | sort)
ASM_SOURCES := $(shell find kernel -name '*.asm' 2>/dev/null | sort)

C_OBJECTS   := $(patsubst %,$(BUILD_DIR)/%.o,$(subst /,_,$(basename $(C_SOURCES))))
ASM_OBJECTS := $(patsubst %,$(BUILD_DIR)/%.o,$(subst /,_,$(basename $(ASM_SOURCES))))
ALL_OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS)

DEPS := $(C_OBJECTS:.o=.d)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

.PHONY: all
all: $(ISO_FILE)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR) $(ISOBOOT_DIR) $(GRUB_DIR)

# Per-source compile rules, generated from the discovered file lists.
define COMPILE_C_RULE
$(BUILD_DIR)/$(subst /,_,$(basename $(1))).o: $(1) | $(BUILD_DIR)
	@echo "[CC] $(1)"
	@$$(CC) $$(CFLAGS) -c $$< -o $$@
endef

define COMPILE_ASM_RULE
$(BUILD_DIR)/$(subst /,_,$(basename $(1))).o: $(1) | $(BUILD_DIR)
	@echo "[AS] $(1)"
	@$$(AS) $$(ASFLAGS) $$< -o $$@
endef

$(foreach src,$(C_SOURCES),$(eval $(call COMPILE_C_RULE,$(src))))
$(foreach src,$(ASM_SOURCES),$(eval $(call COMPILE_ASM_RULE,$(src))))

$(KERNEL_BIN): $(ALL_OBJECTS) scripts/linker.ld | $(BUILD_DIR)
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS) $(ALL_OBJECTS) -o $@
	@echo "[OK] Kernel linked ($$(du -h $@ | cut -f1))"

$(ISO_FILE): $(KERNEL_BIN) boot/grub/grub.cfg
	@echo "[ISO] $@"
	@cp boot/grub/grub.cfg $(GRUB_DIR)/grub.cfg
	@# grub-mkrescue and xorriso are extremely chatty on success. Capture the
	@# output and only show it if something actually went wrong.
	@if ! grub-mkrescue -o $(ISO_FILE) $(ISO_DIR) > $(BUILD_DIR)/grub.log 2>&1; then \
	    echo "[FAIL] grub-mkrescue failed:"; cat $(BUILD_DIR)/grub.log; exit 1; \
	fi
	@echo "[OK] Bootable ISO: $(ISO_FILE)"

# ---------------------------------------------------------------------------
# Running
# ---------------------------------------------------------------------------

QEMU       := qemu-system-x86_64
QEMU_FLAGS := -cdrom $(ISO_FILE) -m 256M

.PHONY: run
run: $(ISO_FILE)
	@echo "[QEMU] Starting BlitzOS..."
	@$(QEMU) $(QEMU_FLAGS)

# Mirrors the whole kernel log to your terminal. Since kprintf() writes to both
# VGA and COM1, this is the easiest way to read a panic or copy a stack trace.
.PHONY: run-serial
run-serial: $(ISO_FILE)
	@echo "[QEMU] Starting with serial on stdio..."
	@$(QEMU) $(QEMU_FLAGS) -serial stdio

# Headless: no video window, everything over serial.
.PHONY: run-headless
run-headless: $(ISO_FILE)
	@$(QEMU) $(QEMU_FLAGS) -nographic -serial mon:stdio

# Waits for GDB on :1234. In another terminal:
#   gdb build/iso/boot/kernel.bin -ex 'target remote :1234'
.PHONY: debug
debug: $(ISO_FILE)
	@echo "[QEMU] Waiting for GDB on port 1234..."
	@$(QEMU) $(QEMU_FLAGS) -serial stdio -s -S

# Logs every CPU reset and triple fault - the first thing to reach for when the
# machine reboots in a loop instead of booting.
.PHONY: run-trace
run-trace: $(ISO_FILE)
	@$(QEMU) $(QEMU_FLAGS) -serial stdio -d int,cpu_reset -no-reboot -no-shutdown

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

# Syntax/warning check using the host compiler. Does not produce a bootable
# kernel - it exists so you can catch mistakes on a machine that does not have
# the cross-compiler installed.
.PHONY: check
check:
	@echo "[CHECK] Compiling all C sources with the host toolchain..."
	@mkdir -p $(BUILD_DIR)/check
	@fail=0; \
	for src in $(C_SOURCES); do \
	    gcc $(filter-out -MMD -MP,$(CFLAGS)) -c $$src -o /dev/null || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "[OK] No errors."; else echo "[FAIL]"; exit 1; fi

.PHONY: todo
todo:
	@grep -rn "TODO\|FIXME\|XXX" kernel drivers --include='*.c' --include='*.h' || echo "None."

# ---------------------------------------------------------------------------
# Housekeeping
# ---------------------------------------------------------------------------

.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR)
	@rm -f $(ISO_FILE)
	@echo "[OK] Clean."

.PHONY: distclean
distclean: clean

.PHONY: config
config:
	@echo "AS      = $(AS)"
	@echo "CC      = $(CC)"
	@echo "LD      = $(LD)"
	@echo "CFLAGS  = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"
	@echo ""
	@echo "C sources   ($(words $(C_SOURCES))):"
	@for s in $(C_SOURCES); do echo "  $$s"; done
	@echo "ASM sources ($(words $(ASM_SOURCES))):"
	@for s in $(ASM_SOURCES); do echo "  $$s"; done

.PHONY: help
help:
	@echo "BlitzOS build targets"
	@echo "====================="
	@echo "  all           Build the kernel and bootable ISO (default)"
	@echo "  run           Boot in QEMU"
	@echo "  run-serial    Boot in QEMU with the kernel log on stdio"
	@echo "  run-headless  Boot with no video window, serial only"
	@echo "  run-trace     Boot with interrupt/reset tracing (triple-fault hunting)"
	@echo "  debug         Boot paused, waiting for GDB on :1234"
	@echo "  check         Compile-check every C file with the host gcc"
	@echo "  config        Show the toolchain and discovered sources"
	@echo "  todo          List TODO/FIXME markers"
	@echo "  clean         Remove build artefacts"

# Header dependencies generated by -MMD, so touching a .h rebuilds its users.
-include $(DEPS)
