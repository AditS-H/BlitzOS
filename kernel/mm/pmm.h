#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

// Page size (4 KB)
#define PAGE_SIZE 4096

// Page frame number
typedef uint64_t pfn_t;

// Initialize physical memory manager
void pmm_init(void);

// Allocate a physical page frame
void* pmm_alloc_page(void);

// Free a physical page frame
void pmm_free_page(void* page);

// Allocate `count` physically contiguous page frames. Returns NULL if no run
// that long is free. Required by anything that treats several pages as one
// object - the kernel heap, DMA buffers, page tables.
void* pmm_alloc_pages(uint64_t count);

// Free a run previously returned by pmm_alloc_pages().
void pmm_free_pages(void* pages, uint64_t count);

// Get total memory in bytes
uint64_t pmm_get_total_memory(void);

// Get free memory in bytes
uint64_t pmm_get_free_memory(void);

// Get used memory in bytes
uint64_t pmm_get_used_memory(void);

#endif // PMM_H
