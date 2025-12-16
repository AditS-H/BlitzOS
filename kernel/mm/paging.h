#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

// Page table entry flags
#define PAGE_PRESENT        (1 << 0)  // Page is present in memory
#define PAGE_WRITABLE       (1 << 1)  // Page is writable
#define PAGE_USER           (1 << 2)  // Page is user-accessible
#define PAGE_WRITE_THROUGH  (1 << 3)  // Write-through caching
#define PAGE_CACHE_DISABLE  (1 << 4)  // Disable caching
#define PAGE_ACCESSED       (1 << 5)  // Page has been accessed
#define PAGE_DIRTY          (1 << 6)  // Page has been written to
#define PAGE_HUGE           (1 << 7)  // 2MB/1GB page
#define PAGE_GLOBAL         (1 << 8)  // Global page (not flushed on CR3 reload)
#define PAGE_NO_EXECUTE     (1ULL << 63) // No execute (NX bit)

// Page table entry
typedef uint64_t pte_t;

// Page table structures (each 4KB, 512 entries of 8 bytes)
typedef struct {
    pte_t entries[512];
} __attribute__((aligned(4096))) page_table_t;

// Initialize paging system
void paging_init(void);

// Map a virtual address to a physical address
void paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

// Unmap a virtual address
void paging_unmap_page(uint64_t virt);

// Get physical address for virtual address
uint64_t paging_get_physical(uint64_t virt);

// Create a new page directory for a process
page_table_t* paging_create_address_space(void);

// Switch to a different address space
void paging_switch_directory(page_table_t* pml4);

// Get current PML4 address
page_table_t* paging_get_current_directory(void);

#endif // PAGING_H
