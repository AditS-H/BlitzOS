// Physical Memory Manager (PMM) Implementation

#include "pmm.h"
#include "../boot/multiboot2.h"
#include "../../drivers/vga.h"

// Bitmap to track page allocation (1 bit per page)
static uint8_t* bitmap = 0;
static uint64_t total_pages = 0;
static uint64_t used_pages = 0;
static uint64_t memory_size = 0;

// Kernel end address (defined in linker script)
extern uint8_t kernel_end;

// Helper functions
static inline void bitmap_set(uint64_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(uint64_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline int bitmap_test(uint64_t bit) {
    return bitmap[bit / 8] & (1 << (bit % 8));
}

// Helper to convert number to string
static void uint64_to_str_dec(uint64_t num, char* buf) {
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

// Initialize physical memory manager
void pmm_init(void) {
    vga_print("[*] Initializing physical memory manager...\n", VGA_COLOR_BROWN);
    
    // Get memory map from multiboot
    const multiboot_tag_mmap_t* mmap = multiboot2_get_mmap();
    if (!mmap) {
        vga_print("[ERROR] No memory map available!\n", VGA_COLOR_LIGHT_RED);
        return;
    }
    
    // Find the largest available memory region
    uint64_t max_addr = 0;
    const multiboot_mmap_entry_t* entry = mmap->entries;
    
    for (; (uint8_t*)entry < (uint8_t*)mmap + mmap->size;
         entry = (multiboot_mmap_entry_t*)((uint64_t)entry + mmap->entry_size)) {
        
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t end = entry->addr + entry->len;
            if (end > max_addr) {
                max_addr = end;
            }
            memory_size += entry->len;
        }
    }
    
    // Calculate number of pages
    total_pages = max_addr / PAGE_SIZE;
    
    // Calculate bitmap size (1 bit per page)
    uint64_t bitmap_size = (total_pages + 7) / 8;
    
    // Place bitmap after kernel
    bitmap = (uint8_t*)((uint64_t)&kernel_end);
    
    // Initialize bitmap - mark all as used initially
    for (uint64_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;
    }
    used_pages = total_pages;
    
    // Mark available regions as free
    entry = mmap->entries;
    for (; (uint8_t*)entry < (uint8_t*)mmap + mmap->size;
         entry = (multiboot_mmap_entry_t*)((uint64_t)entry + mmap->entry_size)) {
        
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t start_page = entry->addr / PAGE_SIZE;
            uint64_t num_pages = entry->len / PAGE_SIZE;
            
            for (uint64_t i = 0; i < num_pages; i++) {
                if (!bitmap_test(start_page + i)) {
                    continue;
                }
                bitmap_clear(start_page + i);
                used_pages--;
            }
        }
    }
    
    // Reserve kernel and bitmap
    uint64_t kernel_start = 0x100000; // 1 MB (where kernel is loaded)
    uint64_t kernel_pages = ((uint64_t)&kernel_end - kernel_start + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (uint64_t i = kernel_start / PAGE_SIZE; i < (kernel_start / PAGE_SIZE + kernel_pages + bitmap_pages); i++) {
        if (i < total_pages && !bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
        }
    }
    
    char buf[32];
    vga_print("    Total memory: ", VGA_COLOR_WHITE);
    uint64_to_str_dec(memory_size / 1024 / 1024, buf);
    vga_print(buf, VGA_COLOR_LIGHT_CYAN);
    vga_print(" MB\n", VGA_COLOR_WHITE);
    
    vga_print("    Total pages: ", VGA_COLOR_WHITE);
    uint64_to_str_dec(total_pages, buf);
    vga_print(buf, VGA_COLOR_LIGHT_CYAN);
    vga_print("\n", VGA_COLOR_WHITE);
    
    vga_print("    Free pages: ", VGA_COLOR_WHITE);
    uint64_to_str_dec(total_pages - used_pages, buf);
    vga_print(buf, VGA_COLOR_LIGHT_CYAN);
    vga_print("\n", VGA_COLOR_WHITE);
    
    vga_print("[OK] PMM initialized!\n", VGA_COLOR_LIGHT_GREEN);
}

// Allocate a physical page
void* pmm_alloc_page(void) {
    // Find first free page
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
            return (void*)(i * PAGE_SIZE);
        }
    }
    
    // Out of memory
    return 0;
}

// Free a physical page
void pmm_free_page(void* page) {
    uint64_t pfn = (uint64_t)page / PAGE_SIZE;
    
    if (pfn >= total_pages) {
        return; // Invalid page
    }
    
    if (!bitmap_test(pfn)) {
        return; // Already free
    }
    
    bitmap_clear(pfn);
    used_pages--;
}

// Get total memory
uint64_t pmm_get_total_memory(void) {
    return total_pages * PAGE_SIZE;
}

// Get free memory
uint64_t pmm_get_free_memory(void) {
    return (total_pages - used_pages) * PAGE_SIZE;
}

// Get used memory
uint64_t pmm_get_used_memory(void) {
    return used_pages * PAGE_SIZE;
}
