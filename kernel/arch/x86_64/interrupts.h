#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>
#include "regs.h"

// 8259A PIC ports
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20  // End Of Interrupt

// Where the PICs are remapped to. Vectors 0-31 belong to CPU exceptions, so
// the hardware IRQs have to move out of the way.
#define IRQ_BASE_VECTOR 0x20  // IRQ0 -> vector 32

// The system call vector. Installed with DPL=3 so ring 3 can reach it.
#define SYSCALL_VECTOR 0x80

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void interrupts_init(void);
void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_send_eoi(uint8_t irq);

// Individually mask/unmask a hardware IRQ line.
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);

// Per-line interrupt counters, surfaced by the shell's `irqstat` command.
uint64_t interrupts_get_irq_count(uint8_t irq);
uint64_t interrupts_get_spurious_count(void);

// ---------------------------------------------------------------------------
// Interrupt flag control
// ---------------------------------------------------------------------------

static inline void enable_interrupts(void)
{
    __asm__ volatile("sti" ::: "memory");
}

static inline void disable_interrupts(void)
{
    __asm__ volatile("cli" ::: "memory");
}

// Save the interrupt flag and disable interrupts. Pair with irq_restore().
//
// Use these instead of a bare cli/sti pair: a plain sti at the end of a
// critical section wrongly enables interrupts even when the caller had them
// disabled, which silently breaks nesting.
static inline uint64_t irq_save(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq\n\t"
                     "pop %0\n\t"
                     "cli"
                     : "=r"(flags)
                     :
                     : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags)
{
    __asm__ volatile("push %0\n\t"
                     "popfq"
                     :
                     : "rm"(flags)
                     : "memory", "cc");
}

static inline int interrupts_enabled(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    return (flags & (1 << 9)) != 0;  // IF is bit 9
}

// ---------------------------------------------------------------------------
// Port I/O
// ---------------------------------------------------------------------------

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// 32-bit port I/O. Required for PCI configuration space, which is only
// addressable a dword at a time through ports 0xCF8/0xCFC.
static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Short delay by writing to an unused port. Some older PIC/PIT hardware needs
// a moment between consecutive command writes.
static inline void io_wait(void)
{
    outb(0x80, 0);
}

// ---------------------------------------------------------------------------
// Assembly entry points (isr.asm)
// ---------------------------------------------------------------------------

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

// INT 0x80 entry point.
extern void syscall_stub(void);

// ---------------------------------------------------------------------------
// C handlers, called from the assembly stubs with a pointer to the saved frame
// ---------------------------------------------------------------------------

void isr_handler(registers_t* regs);
void irq_handler(registers_t* regs);
void syscall_dispatch(registers_t* regs);

#endif // INTERRUPTS_H
