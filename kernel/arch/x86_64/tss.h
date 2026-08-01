#ifndef KERNEL_ARCH_X86_64_TSS_H
#define KERNEL_ARCH_X86_64_TSS_H

#include <stdint.h>

// Task State Segment for x86-64.
//
// In long mode the TSS no longer holds a task's registers. What it does hold is
// RSP0: the stack the CPU switches to when an interrupt arrives while running
// in ring 3. Without a loaded TSS, ring 3 code faults the instant it takes an
// interrupt - which is why user mode cannot work until this is real.
//
// Note this used to be defined entirely in the header, including the TSS
// object itself as a `static` variable. Every translation unit that included
// it got its own private copy, so tss_init() in one file and tss_get() in
// another would have been operating on different structures.
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;        // stack pointer used on entry to ring 0
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;        // Interrupt Stack Table entries
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset; // I/O permission bitmap offset
} tss_t;

// Initialise the single kernel TSS with the given ring 0 stack top.
void tss_init(uint64_t kernel_stack_top);

// Point RSP0 at a new stack. Call this on every context switch once processes
// start running in ring 3, so an interrupt lands on the right kernel stack.
void tss_set_kernel_stack(uint64_t stack_top);

tss_t* tss_get(void);

#endif // KERNEL_ARCH_X86_64_TSS_H
