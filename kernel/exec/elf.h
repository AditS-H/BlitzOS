// elf.h - ELF64 program loader.
//
// WHAT "RUNNING A PROGRAM" ACTUALLY INVOLVES
// ------------------------------------------
// A compiled program is not machine code you can jump to. It is an ELF file: a
// container describing where in memory each piece of the program wants to live,
// what has to be zeroed, and which address to start at. Loading one means:
//
//   1. Validate the header. Magic bytes, 64-bit, little endian, x86-64, and an
//      object type we can actually run.
//   2. Walk the program headers. Each PT_LOAD entry says "copy p_filesz bytes
//      from file offset p_offset to virtual address p_vaddr, then zero out to
//      p_memsz". That last part is the .bss section - it takes up no space in
//      the file but must exist, zeroed, in memory.
//   3. Reserve that memory so the page allocator does not hand it to somebody
//      else.
//   4. Relocate, if the file is position independent.
//   5. Create a process whose entry point is e_entry.
//
// TWO KINDS OF EXECUTABLE
// -----------------------
// ET_EXEC is linked to run at one fixed address. Loading it means copying to
// exactly that address - and since this kernel has a single shared address
// space, only one such program can be resident at a time.
//
// ET_DYN (a position-independent executable) has no fixed home. Its internal
// pointers are stored as offsets that need fixing up once the load address is
// known, which is what the R_X86_64_RELATIVE relocations in .rela.dyn describe:
// "add the load base to the 64-bit value at this offset". Handling those is
// about thirty lines, and it buys the ability to load several programs at once,
// each at whatever address the allocator returned.
//
// Both are supported. Build user programs with -static-pie to get ET_DYN.
//
// PRIVILEGE
// ---------
// Loaded programs currently run in ring 0, in the kernel's address space. They
// are real compiled binaries making real syscalls through INT 0x80, but they
// are not isolated: a wild pointer can scribble on the kernel.
//
// The ring 3 path is built and one flag away - the GDT has user segments, the
// TSS is loaded with a valid RSP0, and the syscall gate is already DPL 3. What
// is missing is per-process page tables, because without them "user mode" would
// mean ring 3 code with full read access to kernel memory, which is worse than
// honest ring 0. See ELF-LOADER.md for the exact remaining work.

#ifndef KERNEL_EXEC_ELF_H
#define KERNEL_EXEC_ELF_H

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// ELF64 on-disk structures
// ---------------------------------------------------------------------------

#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ELFCLASS64  2
#define ELFDATA2LSB 1

#define ET_EXEC 2   // fixed load address
#define ET_DYN  3   // position independent

#define EM_X86_64 62

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_PHDR    6

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// Dynamic section tags we care about for relocation.
#define DT_NULL     0
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9

// The only relocation a static PIE needs: add the load base to the value here.
#define R_X86_64_RELATIVE 8

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

typedef struct {
    uint64_t d_tag;
    uint64_t d_val;
} __attribute__((packed)) elf64_dyn_t;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __attribute__((packed)) elf64_rela_t;

#define ELF64_R_TYPE(info) ((uint32_t)((info) & 0xFFFFFFFF))

// ---------------------------------------------------------------------------
// Loader API
// ---------------------------------------------------------------------------

#define ELF_MAX_LOADED 8

typedef struct {
    int      in_use;
    uint64_t load_base;      // where it actually went
    uint64_t entry;          // absolute entry point
    uint64_t span_start;     // reserved physical range
    uint64_t span_end;
    uint32_t pid;            // process running it, 0 if finished
    char     name[32];
} elf_image_t;

// Error codes, all negative.
#define ELF_ERR_BAD_MAGIC   (-1)
#define ELF_ERR_NOT_64BIT   (-2)
#define ELF_ERR_BAD_ARCH    (-3)
#define ELF_ERR_BAD_TYPE    (-4)
#define ELF_ERR_NO_MEMORY   (-5)
#define ELF_ERR_TRUNCATED   (-6)
#define ELF_ERR_NO_SEGMENTS (-7)
#define ELF_ERR_OCCUPIED    (-8)
#define ELF_ERR_TOO_MANY    (-9)

// Copies every GRUB-loaded module into the ramfs under /bin. Called once at
// startup, from a context where interrupts are running.
void elf_load_boot_modules(void);

// Validates and loads an ELF image held in memory. Returns an index into the
// loaded-image table, or a negative ELF_ERR_*.
int32_t elf_load(const void* image, size_t size, const char* name);

// Loads a program out of the ramfs and starts it as a process.
// Returns the new PID, or a negative ELF_ERR_*.
int32_t elf_exec(const char* path, uint32_t priority);

// Frees the memory of an image whose process has exited.
void elf_release(int32_t index);

const elf_image_t* elf_image_at(uint32_t index);
const char*        elf_error_string(int32_t error);

// Prints what an ELF file contains without running it. Used by `readelf`.
void elf_describe(const void* image, size_t size);

#endif // KERNEL_EXEC_ELF_H
