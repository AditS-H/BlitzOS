#include "interrupts.h"
#include "idt.h"
#include "../../core/panic.h"
#include "../../lib/kprintf.h"
#include "../../proc/process.h"
#include "../../../drivers/vga.h"
#include "../../../drivers/pit.h"
#include "../../../drivers/keyboard.h"
#include "../../../drivers/serial.h"
#include "../../../drivers/mouse.h"

// Counts every hardware IRQ we have seen, for the shell's `irqstat` command.
static uint64_t irq_counts[16];

// Spurious interrupts arrive when a line goes inactive before the CPU
// acknowledges it. They must NOT be EOI'd or the PIC gets out of sync.
static uint64_t spurious_count;

// ---------------------------------------------------------------------------
// 8259A PIC
// ---------------------------------------------------------------------------

void pic_remap(uint8_t offset1, uint8_t offset2)
{
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // ICW1: begin initialisation, expect ICW4.
    outb(PIC1_COMMAND, 0x11); io_wait();
    outb(PIC2_COMMAND, 0x11); io_wait();

    // ICW2: vector offsets.
    outb(PIC1_DATA, offset1); io_wait();
    outb(PIC2_DATA, offset2); io_wait();

    // ICW3: wire the two chips together (slave on IRQ2).
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();

    // ICW4: 8086/88 mode.
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    // Restore whatever was masked before.
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_mask_irq(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (uint8_t)(irq < 8 ? irq : irq - 8);
    outb(port, (uint8_t)(inb(port) | (1 << bit)));
}

void pic_unmask_irq(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (uint8_t)(irq < 8 ? irq : irq - 8);
    outb(port, (uint8_t)(inb(port) & ~(1 << bit)));
}

// Read the In-Service Register to tell a real IRQ7/15 from a spurious one.
static uint16_t pic_read_isr(void)
{
    outb(PIC1_COMMAND, 0x0B);
    outb(PIC2_COMMAND, 0x0B);
    return (uint16_t)((inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND));
}

uint64_t interrupts_get_irq_count(uint8_t irq)
{
    return irq < 16 ? irq_counts[irq] : 0;
}

uint64_t interrupts_get_spurious_count(void)
{
    return spurious_count;
}

// ---------------------------------------------------------------------------
// CPU exceptions
// ---------------------------------------------------------------------------

void isr_handler(registers_t* regs)
{
    switch (regs->int_no) {
    case 3:
        // #BP breakpoint. Used deliberately (int3) as a debugger hook, so
        // print state and carry on instead of killing the machine.
        kprintf_color(VGA_COLOR_LIGHT_MAGENTA,
                      "\n[BREAKPOINT] at RIP=%016lx\n", regs->rip);
        dump_registers(regs);
        return;

    case 2:
        // NMI. Usually a hardware problem; report but keep running.
        kprintf_color(VGA_COLOR_LIGHT_RED,
                      "\n[NMI] Non-maskable interrupt received\n");
        return;

    default:
        break;
    }

    // Everything else is fatal. panic_with_regs() decodes the fault (CR2 for
    // page faults, selector index for GP faults), dumps every register and
    // walks the stack - all of it mirrored to the serial port.
    panic_with_regs(regs, "Unhandled CPU exception");
}

// ---------------------------------------------------------------------------
// Hardware IRQs
// ---------------------------------------------------------------------------

void irq_handler(registers_t* regs)
{
    uint8_t irq = (uint8_t)(regs->int_no - IRQ_BASE_VECTOR);

    // IRQ7 and IRQ15 can be spurious. If the ISR bit is clear the interrupt
    // was noise: do not run a handler, and do not EOI the master for IRQ7.
    if (irq == 7 && !(pic_read_isr() & (1 << 7))) {
        spurious_count++;
        return;
    }
    if (irq == 15 && !(pic_read_isr() & (1 << 15))) {
        spurious_count++;
        outb(PIC1_COMMAND, PIC_EOI);  // master still needs acknowledging
        return;
    }

    if (irq < 16) {
        irq_counts[irq]++;
    }

    switch (irq) {
    case 0:
        pit_handler();       // ticks the clock and the scheduler
        break;
    case 1:
        keyboard_handler();  // drains the PS/2 port, may wake the shell
        break;
    case 4:
        serial_handler();    // COM1 input, feeds the same console queue
        break;
    case 12:
        mouse_handler();     // PS/2 auxiliary device
        break;
    default:
        break;
    }

    // Acknowledge BEFORE any context switch. If we switched first, the EOI
    // would not run until this process is scheduled again - and since the PIC
    // blocks further interrupts of equal or lower priority until it is
    // acknowledged, the timer would never fire again and the system would
    // freeze on the very first preemption.
    pic_send_eoi(irq);

    // This is the one safe place to preempt. Our full register frame is
    // already on this process's kernel stack, so switching away and coming
    // back later resumes exactly here.
    if (need_reschedule) {
        schedule();
    }
}

// ---------------------------------------------------------------------------
// IDT setup
// ---------------------------------------------------------------------------

// Gate type/attribute bytes:
//   0x8E = present, DPL 0, 64-bit interrupt gate (clears IF on entry)
//   0xEE = present, DPL 3, 64-bit interrupt gate (callable from user mode)
#define GATE_KERNEL_INTERRUPT 0x8E
#define GATE_USER_INTERRUPT   0xEE
#define KERNEL_CODE_SELECTOR  0x08

void interrupts_init(void)
{
    idt_init();

    // Move the hardware IRQs off vectors 8-15, which collide with the CPU's
    // double fault and friends.
    pic_remap(IRQ_BASE_VECTOR, IRQ_BASE_VECTOR + 8);

    static void (*const exception_stubs[32])(void) = {
        isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
        isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };

    static void (*const irq_stubs[16])(void) = {
        irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
        irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15
    };

    for (uint8_t i = 0; i < 32; i++) {
        idt_set_gate(i, (uint64_t)exception_stubs[i],
                     KERNEL_CODE_SELECTOR, GATE_KERNEL_INTERRUPT);
    }

    for (uint8_t i = 0; i < 16; i++) {
        idt_set_gate((uint8_t)(IRQ_BASE_VECTOR + i), (uint64_t)irq_stubs[i],
                     KERNEL_CODE_SELECTOR, GATE_KERNEL_INTERRUPT);
    }

    // The system call gate. DPL 3 is the whole point: without it, `int 0x80`
    // from ring 3 raises a general protection fault instead of entering the
    // kernel. This gate was missing entirely, which is why every syscall in
    // syscall.c was dead code.
    idt_set_gate(SYSCALL_VECTOR, (uint64_t)syscall_stub,
                 KERNEL_CODE_SELECTOR, GATE_USER_INTERRUPT);

    // Mask every line, then unmask only what we actually service. Leaving
    // unhandled lines enabled invites interrupt storms from devices the
    // firmware left armed.
    for (uint8_t irq = 0; irq < 16; irq++) {
        pic_mask_irq(irq);
    }
    pic_unmask_irq(0);   // PIT
    pic_unmask_irq(1);   // keyboard
    pic_unmask_irq(4);   // COM1 receive - lets the shell be driven over serial
    pic_unmask_irq(2);   // cascade - required for any slave-PIC line to work
    // IRQ12 (mouse) is unmasked by mouse_init() once a device answers.

    // Deliberately NOT enabling interrupts here.
    //
    // The BIOS leaves the PIT running at ~18.2 Hz, so an STI at this point
    // delivers IRQ0 before scheduler_init() has run. scheduler_tick() would
    // then walk a zeroed process table and schedule() would resume an idle
    // process whose saved RSP is still 0. Interrupts come on at the first
    // context switch instead, via the RFLAGS that scheduler_start() restores.
}
