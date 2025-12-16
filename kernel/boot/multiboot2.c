#include "multiboot2.h"
#include "../../drivers/vga.h"

// Saved pointers to multiboot tags
static const multiboot_tag_mmap_t* mmap_tag = 0;
static const multiboot_tag_basic_meminfo_t* meminfo_tag = 0;
static const multiboot_tag_string_t* bootloader_tag = 0;

// Helper to convert number to string
static void uint64_to_str(uint64_t num, char* buf) {
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    
    char temp[32];
    int i = 0;
    while (num > 0) {
        temp[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

// Parse multiboot2 info structure
void multiboot2_parse(uint32_t magic, uint64_t addr) {
    // Verify magic number
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        vga_print("ERROR: Invalid multiboot2 magic!\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    
    vga_print("[*] Parsing multiboot2 info...\n", VGA_COLOR_BROWN);
    
    multiboot_info_t* mbi = (multiboot_info_t*)addr;
    multiboot_tag_t* tag;
    
    // Iterate through all tags
    for (tag = (multiboot_tag_t*)(addr + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (multiboot_tag_t*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        
        switch (tag->type) {
            case MULTIBOOT_TAG_TYPE_CMDLINE:
                // Command line
                break;
                
            case MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME:
                bootloader_tag = (multiboot_tag_string_t*)tag;
                vga_print("    Bootloader: ", VGA_COLOR_WHITE);
                vga_print(bootloader_tag->string, VGA_COLOR_LIGHT_CYAN);
                vga_print("\n", VGA_COLOR_WHITE);
                break;
                
            case MULTIBOOT_TAG_TYPE_BASIC_MEMINFO:
                meminfo_tag = (multiboot_tag_basic_meminfo_t*)tag;
                {
                    char buf[32];
                    vga_print("    Lower memory: ", VGA_COLOR_WHITE);
                    uint64_to_str(meminfo_tag->mem_lower, buf);
                    vga_print(buf, VGA_COLOR_LIGHT_CYAN);
                    vga_print(" KB\n", VGA_COLOR_WHITE);
                    
                    vga_print("    Upper memory: ", VGA_COLOR_WHITE);
                    uint64_to_str(meminfo_tag->mem_upper, buf);
                    vga_print(buf, VGA_COLOR_LIGHT_CYAN);
                    vga_print(" KB\n", VGA_COLOR_WHITE);
                }
                break;
                
            case MULTIBOOT_TAG_TYPE_MMAP:
                mmap_tag = (multiboot_tag_mmap_t*)tag;
                vga_print("    Memory map found\n", VGA_COLOR_WHITE);
                
                // Display memory regions
                multiboot_mmap_entry_t* entry = mmap_tag->entries;
                for (; (uint8_t*)entry < (uint8_t*)tag + tag->size;
                     entry = (multiboot_mmap_entry_t*)((uint64_t)entry + mmap_tag->entry_size)) {
                    
                    char buf[32];
                    vga_print("      0x", VGA_COLOR_WHITE);
                    
                    // Print address in hex (simplified)
                    uint64_to_str(entry->addr, buf);
                    vga_print(buf, VGA_COLOR_LIGHT_CYAN);
                    vga_print(" - 0x", VGA_COLOR_WHITE);
                    uint64_to_str(entry->addr + entry->len, buf);
                    vga_print(buf, VGA_COLOR_LIGHT_CYAN);
                    
                    vga_print(" (", VGA_COLOR_WHITE);
                    uint64_to_str(entry->len / 1024, buf);
                    vga_print(buf, VGA_COLOR_LIGHT_CYAN);
                    vga_print(" KB) - ", VGA_COLOR_WHITE);
                    
                    switch (entry->type) {
                        case MULTIBOOT_MEMORY_AVAILABLE:
                            vga_print("Available", VGA_COLOR_LIGHT_GREEN);
                            break;
                        case MULTIBOOT_MEMORY_RESERVED:
                            vga_print("Reserved", VGA_COLOR_LIGHT_RED);
                            break;
                        case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
                            vga_print("ACPI Reclaimable", VGA_COLOR_BROWN);
                            break;
                        case MULTIBOOT_MEMORY_NVS:
                            vga_print("ACPI NVS", VGA_COLOR_BROWN);
                            break;
                        case MULTIBOOT_MEMORY_BADRAM:
                            vga_print("Bad RAM", VGA_COLOR_RED);
                            break;
                        default:
                            vga_print("Unknown", VGA_COLOR_LIGHT_GREY);
                            break;
                    }
                    vga_print("\n", VGA_COLOR_WHITE);
                }
                break;
        }
    }
    
    vga_print("[OK] Multiboot2 info parsed!\n", VGA_COLOR_LIGHT_GREEN);
}

// Get memory map tag
const multiboot_tag_mmap_t* multiboot2_get_mmap(void) {
    return mmap_tag;
}

// Get basic memory info
const multiboot_tag_basic_meminfo_t* multiboot2_get_basic_meminfo(void) {
    return meminfo_tag;
}

// Get bootloader name
const char* multiboot2_get_bootloader_name(void) {
    if (bootloader_tag) {
        return bootloader_tag->string;
    }
    return "Unknown";
}
