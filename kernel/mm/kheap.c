#include "kheap.h"
#include "pmm.h"
#include "../lib/kprintf.h"
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

    // Allocate the pages as ONE contiguous run.
    //
    // The old code called pmm_alloc_page() in a loop, kept only the first
    // address, and assumed the rest followed it. That is true on a freshly
    // booted machine and false as soon as the page bitmap has any holes in it,
    // at which point the heap would hand out memory it did not own.
    size_t num_pages = expand_size / 4096;
    void*  new_mem   = pmm_alloc_pages(num_pages);

    if (!new_mem) {
        KHEAP_PRINT("[KHEAP] Failed to allocate contiguous pages for heap expansion\n");
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

// True only when `first` and `second` sit back-to-back in memory.
//
// This check is the whole point. The free list is ordered, but list-adjacent
// blocks are NOT necessarily memory-adjacent: expand_heap() appends each new
// page run to the tail of the list, and separate runs can land anywhere in
// physical memory. Merging across that gap produced a block whose recorded
// size covered memory the heap did not own - silent corruption that showed up
// much later as a fault in unrelated code.
static int blocks_are_contiguous(const block_header_t* first,
                                 const block_header_t* second) {
    return (const uint8_t*)first + BLOCK_HEADER_SIZE + first->size
           == (const uint8_t*)second;
}

// Helper: Coalesce adjacent free blocks
static void coalesce_blocks(block_header_t* block) {
    // Merge forward.
    if (block->next && block->next->is_free &&
        blocks_are_contiguous(block, block->next)) {

        block->size += BLOCK_HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    // Merge backward.
    if (block->prev && block->prev->is_free &&
        blocks_are_contiguous(block->prev, block)) {

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

// Allocate memory aligned to `alignment` bytes (must be a power of two).
//
// The previous implementation computed the aligned address and then returned
// the *unaligned* one anyway, so callers asking for 4096-byte alignment - page
// tables, DMA buffers - silently got whatever the heap handed out.
//
// Over-allocate, align forward, and stash the original pointer in the word
// immediately before the aligned address so it can be freed. Memory from here
// MUST be released with kfree_aligned(), not kfree().
void* kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL;   // alignment must be a power of two
    }
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    size_t   total = size + alignment + sizeof(void*);
    uint8_t* base  = (uint8_t*)kmalloc(total);
    if (!base) {
        return NULL;
    }

    uintptr_t raw     = (uintptr_t)(base + sizeof(void*));
    uintptr_t aligned = align_up(raw, alignment);

    ((void**)aligned)[-1] = base;   // back-pointer for kfree_aligned()
    return (void*)aligned;
}

void kfree_aligned(void* ptr) {
    if (!ptr) {
        return;
    }
    kfree(((void**)ptr)[-1]);
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

void kheap_get_stats(size_t* total, size_t* used,
                     size_t* free_blocks, size_t* used_blocks) {
    size_t free_count = 0;
    size_t used_count = 0;

    for (block_header_t* current = heap_start; current; current = current->next) {
        if (current->is_free) {
            free_count++;
        } else {
            used_count++;
        }
    }

    if (total)       *total = total_heap_size;
    if (used)        *used = used_heap_size;
    if (free_blocks) *free_blocks = free_count;
    if (used_blocks) *used_blocks = used_count;
}

void kheap_print_stats(void) {
    size_t total, used, free_blocks, used_blocks;
    kheap_get_stats(&total, &used, &free_blocks, &used_blocks);

    // Decimal, not hex. The old version printed every number as a 16-digit
    // hex string, which made "how much memory is left" needlessly hard to read.
    kprintf("  Heap: %lu bytes total, %lu used, %lu free  (%lu blocks in use, "
            "%lu free)\n",
            (uint64_t)total, (uint64_t)used, (uint64_t)(total - used),
            (uint64_t)used_blocks, (uint64_t)free_blocks);
}
