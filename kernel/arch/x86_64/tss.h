#ifndef KERNEL_ARCH_X86_64_TSS_H
#define KERNEL_ARCH_X86_64_TSS_H

#include <stdint.h>

// Task State Segment (TSS) structure for x86-64
// Used for stack switching during privilege level changes

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;       // Stack pointer for ring 0
    uint64_t rsp1;       // Stack pointer for ring 1
    uint64_t rsp2;       // Stack pointer for ring 2
    uint64_t reserved1;
    uint64_t ist1;       // Interrupt Stack Table entry 1
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset; // I/O Permission Bitmap offset
} tss_t;

// Global TSS instance
static tss_t kernel_tss __attribute__((aligned(16))) = {0};

// TSS functions
static inline void tss_init(uint64_t kernel_stack) {
    // Set kernel stack for ring 0
    kernel_tss.rsp0 = kernel_stack;
    kernel_tss.iopb_offset = sizeof(tss_t);
}

static inline void tss_set_kernel_stack(uint64_t stack) {
    kernel_tss.rsp0 = stack;
}

static inline tss_t* tss_get(void) {
    return &kernel_tss;
}

#endif // KERNEL_ARCH_X86_64_TSS_H
