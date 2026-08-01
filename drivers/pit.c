#include "pit.h"
#include "../kernel/arch/x86_64/interrupts.h"
#include "../kernel/proc/process.h"

// Monotonic tick counter. 64-bit, so it will not wrap in any realistic uptime
// (at 100 Hz that is about 5.8 billion years).
static volatile uint64_t timer_ticks = 0;

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------

void pit_init(void)
{
    uint32_t divisor = PIT_FREQUENCY / TIMER_FREQUENCY;

    // Command byte 0x36:
    //   bits 7-6 = 00  channel 0
    //   bits 5-4 = 11  access mode: low byte then high byte
    //   bits 3-1 = 011 mode 3, square wave generator
    //   bit  0   = 0   16-bit binary counter
    outb(PIT_COMMAND, 0x36);

    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_handler(void)
{
    timer_ticks++;

    // Age priorities, wake sleepers, and expire the running quantum. This only
    // *marks* that a switch is needed (need_reschedule); the switch itself
    // happens at the end of irq_handler(), once the PIC has been acknowledged.
    scheduler_tick();
}

uint64_t pit_get_ticks(void)
{
    return timer_ticks;
}

uint64_t pit_get_uptime_ms(void)
{
    return timer_ticks * MS_PER_TICK;
}

void pit_busy_wait(uint64_t ticks)
{
    uint64_t deadline = timer_ticks + ticks;
    while (timer_ticks < deadline) {
        __asm__ volatile("hlt");
    }
}

void pit_sleep(uint64_t ticks)
{
    // Once the scheduler owns the CPU, sleeping should take the process off
    // the run queue entirely rather than burning its quantum in a HLT loop.
    if (get_current_process()) {
        process_sleep(ticks);
    } else {
        pit_busy_wait(ticks);
    }
}

// ---------------------------------------------------------------------------
// PC speaker
//
// Channel 2 of the same PIT drives the speaker. Program it to a square wave at
// the requested frequency, then set bits 0 and 1 of port 0x61 to connect the
// output to the speaker.
//
// Note: QEMU only makes noise with an audio backend configured, so this may be
// silent under a plain `make run` even though the ports are driven correctly.
// ---------------------------------------------------------------------------

void speaker_on(uint32_t frequency_hz)
{
    if (frequency_hz < 20 || frequency_hz > 20000) {
        return;  // outside human hearing, and the divisor would be nonsense
    }

    uint32_t divisor = PIT_FREQUENCY / frequency_hz;

    // 0xB6: channel 2, low/high byte access, mode 3 square wave, binary.
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (uint8_t)((divisor >> 8) & 0xFF));

    // Connect the timer output to the speaker (bits 0 and 1).
    uint8_t gate = inb(SPEAKER_PORT);
    if ((gate & 0x03) != 0x03) {
        outb(SPEAKER_PORT, (uint8_t)(gate | 0x03));
    }
}

void speaker_off(void)
{
    outb(SPEAKER_PORT, (uint8_t)(inb(SPEAKER_PORT) & 0xFC));
}

void speaker_beep(uint32_t frequency_hz, uint32_t duration_ms)
{
    speaker_on(frequency_hz);
    pit_sleep((duration_ms + MS_PER_TICK - 1) / MS_PER_TICK);
    speaker_off();
}
