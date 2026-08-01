#ifndef KERNEL_ARCH_X86_64_GDT_H
#define KERNEL_ARCH_X86_64_GDT_H

#include <stdint.h>

// Segment selectors. These must match the table laid out in boot.asm.
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18   // OR with 3 for the RPL when entering ring 3
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

// Writes a 64-bit TSS descriptor into the GDT slot at 0x28 and loads it with
// LTR. Returns 1 on success.
//
// This was previously a static inline stub whose entire body was
// `(void)tss_addr;` next to a TODO. The kernel allocated a TSS, printed
// "User mode ready!", and had in fact loaded nothing.
int gdt_load_tss(uint64_t tss_address);

#endif // KERNEL_ARCH_X86_64_GDT_H
