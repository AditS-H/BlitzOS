#ifndef KERNEL_ARCH_X86_64_GDT_H
#define KERNEL_ARCH_X86_64_GDT_H

#include <stdint.h>

// Global Descriptor Table (GDT) definitions
// For x86-64, GDT is mostly used for privilege level separation

// Segment selectors
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

// GDT functions (stubs - GDT already set up in boot.asm)
static inline void gdt_init(void) {
    // GDT is already initialized in boot.asm
    // This function exists for future enhancements
}

static inline void gdt_reload(void) {
    // Reload GDT if needed
    // Currently handled in assembly
}

// Load TSS into GDT (stub - not fully implemented yet)
static inline void gdt_load_tss(uint64_t tss_addr) {
    // TODO: Properly load TSS descriptor into GDT
    // For now, just acknowledge the call
    (void)tss_addr;
}

#endif // KERNEL_ARCH_X86_64_GDT_H
