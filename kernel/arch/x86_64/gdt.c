// gdt.c - TSS descriptor installation.
//
// The GDT itself is built statically in boot.asm; the only entry that cannot
// be filled in at assembly time is the TSS descriptor, because it embeds the
// runtime address of the TSS structure.

#include "gdt.h"
#include "tss.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

// Provided by boot.asm. `gdt64_tss_descriptor` is the 16-byte hole reserved at
// selector 0x28.
extern uint8_t gdt64[];
extern uint8_t gdt64_tss_descriptor[];

// The one and only kernel TSS.
static tss_t kernel_tss __attribute__((aligned(16)));

// A 64-bit system-segment descriptor. Unlike code and data descriptors, these
// are 16 bytes: the base address is 64 bits wide and spills into a second
// slot.
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;         // 0x89 = present, type 9 (available 64-bit TSS)
    uint8_t  limit_high_flags;
    uint8_t  base_mid2;
    uint32_t base_high;
    uint32_t reserved;
} tss_descriptor_t;

_Static_assert(sizeof(tss_descriptor_t) == 16,
               "A 64-bit TSS descriptor must be exactly 16 bytes");

void tss_init(uint64_t kernel_stack_top)
{
    memset(&kernel_tss, 0, sizeof(kernel_tss));

    kernel_tss.rsp0 = kernel_stack_top;

    // Setting the I/O permission bitmap offset to the size of the TSS means
    // "there is no bitmap", so every port access from ring 3 faults. That is
    // what we want: user code should go through syscalls, not IN/OUT.
    kernel_tss.iopb_offset = sizeof(tss_t);
}

void tss_set_kernel_stack(uint64_t stack_top)
{
    kernel_tss.rsp0 = stack_top;
}

tss_t* tss_get(void)
{
    return &kernel_tss;
}

int gdt_load_tss(uint64_t tss_address)
{
    if (tss_address == 0) {
        kerror("gdt_load_tss: NULL TSS address\n");
        return 0;
    }

    tss_descriptor_t* descriptor = (tss_descriptor_t*)gdt64_tss_descriptor;
    uint32_t          limit      = sizeof(tss_t) - 1;

    descriptor->limit_low        = (uint16_t)(limit & 0xFFFF);
    descriptor->base_low         = (uint16_t)(tss_address & 0xFFFF);
    descriptor->base_mid         = (uint8_t)((tss_address >> 16) & 0xFF);
    descriptor->access           = 0x89;
    descriptor->limit_high_flags = (uint8_t)((limit >> 16) & 0x0F);
    descriptor->base_mid2        = (uint8_t)((tss_address >> 24) & 0xFF);
    descriptor->base_high        = (uint32_t)(tss_address >> 32);
    descriptor->reserved         = 0;

    // LTR marks the descriptor busy and points the CPU's task register at it.
    __asm__ volatile("ltr %%ax" :: "a"((uint16_t)GDT_TSS) : "memory");

    return 1;
}
