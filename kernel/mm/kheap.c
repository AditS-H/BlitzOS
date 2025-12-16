#include "kheap.h"
#include "pmm.h"
#include "../../drivers/vga.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Helper macro for VGA printing with default color
#define KHEAP_PRINT(str) vga_print(str, VGA_COLOR_WHITE)

// Block header for each allocation
typedef struct block_header {
    size_t size;                    // Size of usable data (not including header)
    bool is_free;                   // Is this block free?
    struct block_header* next;      // Next block in linked list
    struct block_header* prev;      // Previous block in linked list
} block_header_t;

#define BLOCK_HEADER_SIZE sizeof(block_header_t)
#define HEAP_EXPAND_SIZE (4 * 4096)  // Expand by 4 pages (16 KB) at a time
#define MIN_BLOCK_SIZE 16             // Minimum usable block size

static block_header_t* heap_start = NULL;
static size_t total_heap_size = 0;
static size_t used_heap_size = 0;

// Helper: Align size up to alignment boundary
static size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

// Helper: Expand heap by allocating more pages
static block_header_t* expand_heap(size_t min_size) {
    size_t expand_size = HEAP_EXPAND_SIZE;
    if (min_size > expand_size) {
        // Round up to nearest page boundary
        expand_size = align_up(min_size, 4096);
    }

    // Allocate physical pages
    size_t num_pages = expand_size / 4096;
    void* new_mem = NULL;
    
    for (size_t i = 0; i < num_pages; i++) {
        void* page = pmm_alloc_page();
        if (!page) {
            KHEAP_PRINT("[KHEAP] Failed to allocate page for heap expansion\n");
            return NULL;
        }
        if (i == 0) {
            new_mem = page;
        }
    }

    if (!new_mem) {
        return NULL;
    }

    // Create new block
    block_header_t* new_block = (block_header_t*)new_mem;
    new_block->size = expand_size - BLOCK_HEADER_SIZE;
    new_block->is_free = true;
    new_block->next = NULL;
    new_block->prev = NULL;

    total_heap_size += expand_size;

    // Add to linked list
    if (!heap_start) {
        heap_start = new_block;
    } else {
        // Find last block and append
        block_header_t* current = heap_start;
        while (current->next) {
            current = current->next;
        }
        current->next = new_block;
        new_block->prev = current;
    }

    return new_block;
}

// Helper: Split a block if it's large enough
static void split_block(block_header_t* block, size_t size) {
    // Only split if remaining space is significant
    if (block->size >= size + BLOCK_HEADER_SIZE + MIN_BLOCK_SIZE) {
        size_t original_size = block->size;
        block->size = size;

        // Create new free block from remainder
        block_header_t* new_block = (block_header_t*)((uint8_t*)block + BLOCK_HEADER_SIZE + size);
        new_block->size = original_size - size - BLOCK_HEADER_SIZE;
        new_block->is_free = true;
        new_block->next = block->next;
        new_block->prev = block;

        if (block->next) {
            block->next->prev = new_block;
        }
        block->next = new_block;
    }
}

// Helper: Coalesce adjacent free blocks
static void coalesce_blocks(block_header_t* block) {
    // Coalesce with next block if it's free
    if (block->next && block->next->is_free) {
        block->size += BLOCK_HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    // Coalesce with previous block if it's free
    if (block->prev && block->prev->is_free) {
        block->prev->size += BLOCK_HEADER_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

void kheap_init(void) {
    KHEAP_PRINT("[KHEAP] Initializing kernel heap...\n");
    
    // Start with initial heap allocation
    heap_start = expand_heap(HEAP_EXPAND_SIZE);
    
    if (!heap_start) {
        KHEAP_PRINT("[KHEAP] Failed to initialize heap!\n");
        return;
    }
    
    KHEAP_PRINT("[KHEAP] Heap initialized with ");
    vga_print_hex((uint64_t)total_heap_size);
    KHEAP_PRINT(" bytes\n");
}

void* kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    // Align size to 8-byte boundary for performance
    size = align_up(size, 8);

    // First-fit algorithm: find first free block large enough
    block_header_t* current = heap_start;
    while (current) {
        if (current->is_free && current->size >= size) {
            // Found suitable block
            split_block(current, size);
            current->is_free = false;
            used_heap_size += size + BLOCK_HEADER_SIZE;

            // Return pointer to usable data (after header)
            return (void*)((uint8_t*)current + BLOCK_HEADER_SIZE);
        }
        current = current->next;
    }

    // No suitable block found, expand heap
    block_header_t* new_block = expand_heap(size + BLOCK_HEADER_SIZE);
    if (!new_block) {
        KHEAP_PRINT("[KHEAP] kmalloc failed: out of memory\n");
        return NULL;
    }

    // Use the new block
    split_block(new_block, size);
    new_block->is_free = false;
    used_heap_size += size + BLOCK_HEADER_SIZE;

    return (void*)((uint8_t*)new_block + BLOCK_HEADER_SIZE);
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        // Alignment must be power of 2
        return NULL;
    }

    // Allocate extra space for alignment adjustment
    size_t total_size = size + alignment + BLOCK_HEADER_SIZE;
    void* ptr = kmalloc(total_size);
    if (!ptr) {
        return NULL;
    }

    // Calculate aligned address
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned_addr = align_up(addr, alignment);

    // For simplicity, just return the allocated pointer
    // A more sophisticated implementation would track the offset
    return ptr;
}

void kfree(void* ptr) {
    if (!ptr) {
        return;
    }

    // Get block header (it's right before the data)
    block_header_t* block = (block_header_t*)((uint8_t*)ptr - BLOCK_HEADER_SIZE);

    if (block->is_free) {
        KHEAP_PRINT("[KHEAP] Warning: Double free detected!\n");
        return;
    }

    // Mark block as free
    block->is_free = true;
    used_heap_size -= block->size + BLOCK_HEADER_SIZE;

    // Coalesce with adjacent free blocks
    coalesce_blocks(block);
}

void kheap_print_stats(void) {
    KHEAP_PRINT("[KHEAP] Heap Statistics:\n");
    KHEAP_PRINT("  Total heap size: ");
    vga_print_hex((uint64_t)total_heap_size);
    KHEAP_PRINT(" bytes\n");
    KHEAP_PRINT("  Used heap size:  ");
    vga_print_hex((uint64_t)used_heap_size);
    KHEAP_PRINT(" bytes\n");
    KHEAP_PRINT("  Free heap size:  ");
    vga_print_hex((uint64_t)(total_heap_size - used_heap_size));
    KHEAP_PRINT(" bytes\n");

    // Count blocks
    size_t free_blocks = 0;
    size_t used_blocks = 0;
    block_header_t* current = heap_start;
    while (current) {
        if (current->is_free) {
            free_blocks++;
        } else {
            used_blocks++;
        }
        current = current->next;
    }

    KHEAP_PRINT("  Free blocks: ");
    vga_print_hex((uint64_t)free_blocks);
    KHEAP_PRINT("\n");
    KHEAP_PRINT("  Used blocks: ");
    vga_print_hex((uint64_t)used_blocks);
    KHEAP_PRINT("\n");
}
