// Physical Memory Manager (PMM) Implementation

#include "pmm.h"
#include "../boot/multiboot2.h"
#include "../../drivers/vga.h"

// Bitmap to track page allocation (1 bit per page)
static uint8_t* bitmap = 0;
static uint64_t total_pages = 0;
static uint64_t used_pages = 0;
static uint64_t memory_size = 0;

// Kernel end address (defined in the linker script).
//
// Declared as an unbounded array, not a scalar. With `extern uint8_t
// kernel_end;` GCC treats it as exactly one byte, so every bitmap[] access
// derived from its address looked like an out-of-bounds write and produced a
// -Warray-bounds warning on each build.
extern uint8_t kernel_end[];

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
    bitmap = kernel_end;
    
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
    uint64_t kernel_pages = ((uint64_t)kernel_end - kernel_start + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = kernel_start / PAGE_SIZE; i < (kernel_start / PAGE_SIZE + kernel_pages + bitmap_pages); i++) {
        if (i < total_pages && !bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
        }
    }

    // Reserve everything below 1 MB.
    //
    // Two reasons. First, the low megabyte holds the BIOS data area, the real
    // mode IVT, VGA memory and other things we must not hand out. Second, and
    // more subtly: the firmware memory map marks 0x0-0x9FC00 as available, so
    // the very first pmm_alloc_page() used to return physical address 0 - a
    // pointer indistinguishable from NULL. Every caller then treated a
    // successful allocation as an out-of-memory failure.
    for (uint64_t i = 0; i < (0x100000 / PAGE_SIZE) && i < total_pages; i++) {
        if (!bitmap_test(i)) {
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

// Where the last successful allocation finished. Scanning from here instead of
// from page 0 every time turns allocation from O(total_pages) into roughly
// O(1) once the low pages are exhausted.
static uint64_t search_hint = 0;

// Allocate a physical page
void* pmm_alloc_page(void) {
    // Two passes: from the hint to the end, then from the start to the hint.
    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t start = (pass == 0) ? search_hint : 0;
        uint64_t end   = (pass == 0) ? total_pages : search_hint;

        for (uint64_t i = start; i < end; i++) {
            if (!bitmap_test(i)) {
                bitmap_set(i);
                used_pages++;
                search_hint = i + 1;
                return (void*)(i * PAGE_SIZE);
            }
        }
    }

    // Out of memory
    return 0;
}

// Allocate `count` *physically contiguous* pages.
//
// The heap needs this. Its expand_heap() used to call pmm_alloc_page() in a
// loop and assume the results were adjacent, then treat the whole run as one
// block. That happened to hold on a freshly booted machine and quietly
// corrupted the heap once allocation and freeing had fragmented the bitmap.
void* pmm_alloc_pages(uint64_t count) {
    if (count == 0) {
        return 0;
    }
    if (count == 1) {
        return pmm_alloc_page();
    }

    uint64_t run_start = 0;
    uint64_t run_length = 0;

    for (uint64_t i = 0; i < total_pages; i++) {
        if (bitmap_test(i)) {
            run_length = 0;
            continue;
        }

        if (run_length == 0) {
            run_start = i;
        }
        run_length++;

        if (run_length == count) {
            for (uint64_t j = 0; j < count; j++) {
                bitmap_set(run_start + j);
            }
            used_pages += count;
            search_hint = run_start + count;
            return (void*)(run_start * PAGE_SIZE);
        }
    }

    return 0;  // no contiguous run large enough
}

// Claim an exact physical range.
//
// Unlike the allocators above, the caller has no choice about the address - a
// fixed-address ELF must land where it was linked. So this is all-or-nothing:
// check the whole range is free first, and only then mark it, rather than
// grabbing pages one at a time and having to unwind halfway through.
int pmm_reserve_range(uint64_t address, uint64_t length) {
    if (length == 0) {
        return 0;
    }

    uint64_t first = address / PAGE_SIZE;
    uint64_t last  = (address + length + PAGE_SIZE - 1) / PAGE_SIZE;

    if (last > total_pages) {
        return 0;   // runs past the end of physical memory
    }

    for (uint64_t i = first; i < last; i++) {
        if (bitmap_test(i)) {
            return 0;   // something already owns part of this range
        }
    }

    for (uint64_t i = first; i < last; i++) {
        bitmap_set(i);
        used_pages++;
    }

    return 1;
}

// Free a run allocated with pmm_alloc_pages().
void pmm_free_pages(void* pages, uint64_t count) {
    uint64_t pfn = (uint64_t)pages / PAGE_SIZE;

    for (uint64_t i = 0; i < count; i++) {
        if (pfn + i >= total_pages || !bitmap_test(pfn + i)) {
            continue;
        }
        bitmap_clear(pfn + i);
        used_pages--;
    }

    if (pfn < search_hint) {
        search_hint = pfn;
    }
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

    // Reuse the hole on the next allocation instead of stranding it.
    if (pfn < search_hint) {
        search_hint = pfn;
    }
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
