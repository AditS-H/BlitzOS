# BlitzOS user-space programs.
#
# These are ordinary compiled binaries, not part of the kernel. They are built
# separately, shipped inside the ISO, handed to the kernel by GRUB as boot
# modules, and loaded at runtime by kernel/exec/elf.c.
#
# WHY THE FLAGS ARE WHAT THEY ARE
# -------------------------------
#   -ffreestanding      There is no hosted environment. No printf, no malloc,
#                       no assumption that main() exists.
#   -nostdlib           Do not link a C library or the CRT startup objects.
#                       Without this the linker pulls in crt1.o, which calls
#                       __libc_start_main, which does not exist here.
#   -static-pie         Produce a position-independent executable (ET_DYN).
#                       The loader can then place it at any free address and
#                       fix up its internal pointers, instead of demanding one
#                       fixed address that only one program at a time can own.
#   -fno-stack-protector  The guard needs __stack_chk_fail from libc.
#   -mno-red-zone       Interrupt handlers would clobber the 128-byte red zone.
#   -e _start           The entry point. No CRT means nobody calls main().
#
# C++ additionally needs:
#   -fno-exceptions     Unwinding needs libgcc's personality routine.
#   -fno-rtti           typeid/dynamic_cast need libstdc++ type_info objects.

CC  ?= gcc
CXX ?= g++

# The kernel's cross-compiler works too if you have it:
#   make -C userland CC=x86_64-elf-gcc CXX=x86_64-elf-g++

COMMON_FLAGS := -ffreestanding -nostdlib -static-pie -fPIE \
                -fno-stack-protector -mno-red-zone \
                -O2 -Wall -Wextra -Iinclude

CFLAGS   := $(COMMON_FLAGS) -std=c11
CXXFLAGS := $(COMMON_FLAGS) -std=c++17 -fno-exceptions -fno-rtti

LDFLAGS  := -nostdlib -static-pie -Wl,-e,_start -Wl,--build-id=none

BIN_DIR := bin

PROGRAMS := $(BIN_DIR)/hello $(BIN_DIR)/demo

.PHONY: all
all: $(PROGRAMS)
	@echo "[OK] user programs built in $(BIN_DIR)/"
	@ls -l $(BIN_DIR)/

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BIN_DIR)/hello: hello.c include/blitz.h | $(BIN_DIR)
	@echo "[CC ] $<"
	@$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

$(BIN_DIR)/demo: demo.cpp include/blitz.h | $(BIN_DIR)
	@echo "[CXX] $<"
	@$(CXX) $(CXXFLAGS) $(LDFLAGS) $< -o $@

# Show what the loader will see.
.PHONY: inspect
inspect: all
	@for p in $(PROGRAMS); do \
	    echo "=== $$p ==="; \
	    readelf -h $$p | grep -E "Type|Entry|Machine"; \
	    readelf -lW $$p | grep -E "LOAD|DYNAMIC"; \
	    echo; \
	done

.PHONY: clean
clean:
	@rm -rf $(BIN_DIR)
	@echo "[OK] user programs cleaned"
