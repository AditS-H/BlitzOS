#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

// Initialize the kernel heap
void kheap_init(void);

// Allocate memory from kernel heap
void* kmalloc(size_t size);

// Allocate memory aligned to `alignment` bytes (a power of two).
// Memory from here MUST be released with kfree_aligned(), not kfree().
void* kmalloc_aligned(size_t size, size_t alignment);

// Free memory back to kernel heap
void kfree(void* ptr);

// Free a pointer returned by kmalloc_aligned().
void kfree_aligned(void* ptr);

// Get heap statistics
void kheap_print_stats(void);

// Machine-readable form of the above. Any pointer may be NULL.
void kheap_get_stats(size_t* total, size_t* used,
                     size_t* free_blocks, size_t* used_blocks);

#endif // KHEAP_H
