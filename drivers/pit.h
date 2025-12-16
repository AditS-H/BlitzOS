#ifndef PIT_H
#define PIT_H

#include <stdint.h>

// PIT I/O ports
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

// PIT frequency (Hz)
#define PIT_FREQUENCY 1193182

// Target timer frequency (Hz) - 100 Hz = 10ms per tick
#define TIMER_FREQUENCY 100

// Initialize the PIT timer
void pit_init(void);

// PIT interrupt handler (called from IRQ0)
void pit_handler(void);

// Get the current tick count
uint64_t pit_get_ticks(void);

// Sleep for a specified number of ticks
void pit_sleep(uint64_t ticks);

#endif // PIT_H
