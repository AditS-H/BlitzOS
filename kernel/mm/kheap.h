#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

// Initialize the kernel heap
void kheap_init(void);

// Allocate memory from kernel heap
void* kmalloc(size_t size);

// Allocate aligned memory from kernel heap
void* kmalloc_aligned(size_t size, size_t alignment);

// Free memory back to kernel heap
void kfree(void* ptr);

// Get heap statistics
void kheap_print_stats(void);

#endif // KHEAP_H
