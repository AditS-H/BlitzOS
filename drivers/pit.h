#ifndef PIT_H
#define PIT_H

#include <stdint.h>

// 8253/8254 Programmable Interval Timer
#define PIT_CHANNEL0 0x40   // System timer -> IRQ0
#define PIT_CHANNEL1 0x41   // Historically DRAM refresh; unused
#define PIT_CHANNEL2 0x42   // Wired to the PC speaker
#define PIT_COMMAND  0x43

// The PIT's fixed input frequency in Hz.
#define PIT_FREQUENCY 1193182

// Our tick rate. 100 Hz = one tick every 10 ms.
#define TIMER_FREQUENCY 100
#define MS_PER_TICK     (1000 / TIMER_FREQUENCY)

// PC speaker gate register
#define SPEAKER_PORT 0x61

void     pit_init(void);

// Reprogram the timer at runtime.
//
// Game mode uses this. At 100 Hz the shortest sleep the scheduler can express
// is 10 ms, so frame pacing quantises to 10/20/30 ms - asking for a 16 ms
// budget is impossible. At 1000 Hz the granularity is 1 ms, frame pacing
// becomes accurate, and a woken process starts running sooner, which is
// exactly the input latency a game cares about.
//
// The cost is 10x the interrupt rate: 1000 context-switch checks per second
// instead of 100. Measurably more overhead, which is why it is opt-in.
void     pit_set_frequency(uint32_t hz);
uint32_t pit_get_frequency(void);
void     pit_handler(void);       // called from IRQ0

uint64_t pit_get_ticks(void);
uint64_t pit_get_uptime_ms(void);

// Busy-wait for `ticks` timer ticks. Prefer process_sleep(), which gives the
// CPU up instead of spinning; this is only for very early boot, before the
// scheduler is running.
void pit_busy_wait(uint64_t ticks);

// Sleep for `ticks`. Descheduled if the scheduler is up, busy-wait if not.
void pit_sleep(uint64_t ticks);

// -------- PC speaker --------
// Square-wave tone generation via PIT channel 2. This is what makes SYS_BEEP
// actually audible instead of being a stub.
void speaker_on(uint32_t frequency_hz);
void speaker_off(void);
void speaker_beep(uint32_t frequency_hz, uint32_t duration_ms);

#endif // PIT_H
