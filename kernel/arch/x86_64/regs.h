// regs.h - CPU register frame captured on interrupt entry.
//
// The layout MUST match the push order in isr.asm exactly. The assembly stubs
// build this structure on the stack and hand the C handler a pointer to it,
// which is what lets the panic screen dump real register values instead of
// just printing "Page Fault" and halting.
//
// Stack layout, lowest address first (this is the order of the fields below):
//
//   rsp ->  r15 r14 r13 r12 r11 r10 r9 r8      <- pushed by our stub
//           rbp rdi rsi rdx rcx rbx rax        <- pushed by our stub
//           int_no                             <- pushed by our stub
//           err_code                           <- CPU (some faults) or stub
//           rip cs rflags rsp ss               <- pushed by the CPU

#ifndef KERNEL_ARCH_X86_64_REGS_H
#define KERNEL_ARCH_X86_64_REGS_H

#include <stdint.h>

typedef struct {
    // General purpose registers, saved by the stub.
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

    // Which vector fired, and the CPU error code (0 for vectors that do not
    // push one; the stub pushes a dummy so the frame is always the same size).
    uint64_t int_no;
    uint64_t err_code;

    // Pushed automatically by the CPU on interrupt entry. In long mode the
    // CPU always pushes SS:RSP, even without a privilege level change.
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) registers_t;

// Page fault error code bits (vector 14). CR2 holds the faulting address.
#define PF_ERR_PRESENT   (1 << 0)  // 0 = page not present, 1 = protection fault
#define PF_ERR_WRITE     (1 << 1)  // 0 = read, 1 = write
#define PF_ERR_USER      (1 << 2)  // 1 = fault happened in ring 3
#define PF_ERR_RESERVED  (1 << 3)  // reserved bit set in a page table entry
#define PF_ERR_FETCH     (1 << 4)  // instruction fetch (needs NX enabled)

// Read control registers. Useful in fault handlers: CR2 is the faulting
// linear address, CR3 the active page table root.
static inline uint64_t read_cr0(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static inline uint64_t read_cr2(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline uint64_t read_cr3(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline uint64_t read_cr4(void)
{
    uint64_t value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

#endif // KERNEL_ARCH_X86_64_REGS_H
