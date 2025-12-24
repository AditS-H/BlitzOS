#include "pit.h"
#include "../kernel/arch/x86_64/interrupts.h"

// Global tick counter
static volatile uint64_t timer_ticks = 0;

// PIT interrupt handler (called from IRQ0)
void pit_handler(void) {
    timer_ticks++;
    // Note: Preemption now handled in preempt_handler() called from assembly
}

// Initialize the PIT
void pit_init(void) {
    // Calculate divisor for desired frequency
    uint32_t divisor = PIT_FREQUENCY / TIMER_FREQUENCY;
    
    // Send command byte: Channel 0, Mode 3 (square wave), binary mode
    // Command format: 00 (Channel 0) 11 (lobyte/hibyte) 011 (square wave) 0 (binary)
    outb(PIT_COMMAND, 0x36);
    
    // Send frequency divisor (low byte, then high byte)
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

// Get current tick count
uint64_t pit_get_ticks(void) {
    return timer_ticks;
}

// Sleep for specified number of ticks
void pit_sleep(uint64_t ticks) {
    uint64_t end_tick = timer_ticks + ticks;
    while (timer_ticks < end_tick) {
        __asm__ volatile("hlt");  // Halt CPU until next interrupt
    }
}
