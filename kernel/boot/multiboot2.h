#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include <stdint.h>

// Multiboot2 header magic
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289

// Tag types
#define MULTIBOOT_TAG_TYPE_END               0
#define MULTIBOOT_TAG_TYPE_CMDLINE           1
#define MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME  2
#define MULTIBOOT_TAG_TYPE_MODULE            3
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO     4
#define MULTIBOOT_TAG_TYPE_BOOTDEV           5
#define MULTIBOOT_TAG_TYPE_MMAP              6
#define MULTIBOOT_TAG_TYPE_VBE               7
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER       8
#define MULTIBOOT_TAG_TYPE_ELF_SECTIONS      9
#define MULTIBOOT_TAG_TYPE_APM               10
#define MULTIBOOT_TAG_TYPE_EFI32             11
#define MULTIBOOT_TAG_TYPE_EFI64             12
#define MULTIBOOT_TAG_TYPE_SMBIOS            13
#define MULTIBOOT_TAG_TYPE_ACPI_OLD          14
#define MULTIBOOT_TAG_TYPE_ACPI_NEW          15
#define MULTIBOOT_TAG_TYPE_NETWORK           16
#define MULTIBOOT_TAG_TYPE_EFI_MMAP          17
#define MULTIBOOT_TAG_TYPE_EFI_BS            18
#define MULTIBOOT_TAG_TYPE_EFI32_IH          19
#define MULTIBOOT_TAG_TYPE_EFI64_IH          20
#define MULTIBOOT_TAG_TYPE_LOAD_BASE_ADDR    21

// Memory map entry types
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5

// Basic tag structure
typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) multiboot_tag_t;

// Basic memory info
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;  // KB of lower memory
    uint32_t mem_upper;  // KB of upper memory
} __attribute__((packed)) multiboot_tag_basic_meminfo_t;

// Memory map entry
typedef struct {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed)) multiboot_mmap_entry_t;

// Memory map tag
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    multiboot_mmap_entry_t entries[0];
} __attribute__((packed)) multiboot_tag_mmap_t;

// Boot loader name
typedef struct {
    uint32_t type;
    uint32_t size;
    char string[0];
} __attribute__((packed)) multiboot_tag_string_t;

// Multiboot info structure
typedef struct {
    uint32_t total_size;
    uint32_t reserved;
    multiboot_tag_t tags[0];
} __attribute__((packed)) multiboot_info_t;

// Parse multiboot2 information
void multiboot2_parse(uint32_t magic, uint64_t addr);

// Get memory map
const multiboot_tag_mmap_t* multiboot2_get_mmap(void);

// Get basic memory info
const multiboot_tag_basic_meminfo_t* multiboot2_get_basic_meminfo(void);

// Get bootloader name
const char* multiboot2_get_bootloader_name(void);

#endif // MULTIBOOT2_H
