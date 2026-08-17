// tsc.h - Microsecond timing from the CPU's Time Stamp Counter.
//
// WHY THE PIT IS NOT ENOUGH
// -------------------------
// The scheduler runs on a 100 Hz timer, so the finest interval it can measure
// is 10 ms. A frame that takes 2 ms and a frame that takes 9 ms both look like
// "0 ticks". To say anything real about rendering performance we need a clock
// with sub-millisecond resolution.
//
// RDTSC gives us one. It returns a 64-bit count of CPU cycles, readable in a
// couple of nanoseconds with no I/O. The catch is that it counts *cycles*, and
// nothing tells us how many cycles a microsecond is - so we calibrate against
// the one clock whose rate we do know.
//
// CALIBRATION
// -----------
// Read the TSC, wait a known number of PIT ticks, read it again. The delta
// divided by the elapsed microseconds gives cycles per microsecond. Waiting
// 50 ticks (500 ms) keeps the error from the +/-1 tick quantisation under 2%.
//
// CAVEATS worth knowing about, and why they do not bite us here:
//
//   Frequency scaling. On old CPUs the TSC counted actual clock cycles, so it
//   sped up and slowed down with the core. Every CPU since roughly Nehalem has
//   an invariant TSC that ticks at a fixed rate regardless of P-state. We check
//   CPUID for that and warn if it is missing.
//
//   Multi-core skew. TSCs on different cores are not guaranteed to agree. This
//   kernel is single-core, so the question does not arise yet - but it is the
//   first thing to revisit when SMP lands.
//
//   Out-of-order execution. RDTSC is not a serialising instruction, so the CPU
//   may run it earlier or later than its position in the instruction stream
//   suggests. For measuring a whole frame that is noise; for timing a handful
//   of instructions you would need LFENCE first.

#ifndef KERNEL_ARCH_X86_64_TSC_H
#define KERNEL_ARCH_X86_64_TSC_H

#include <stdint.h>

// Reads the counter. Cheap enough to call inside a render loop.
static inline uint64_t rdtsc(void)
{
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

// Calibrates against the PIT. Must be called after pit_init() and with
// interrupts enabled, because it waits on timer ticks.
void tsc_init(void);

int      tsc_is_calibrated(void);
uint64_t tsc_cycles_per_us(void);

// Monotonic microseconds since calibration. Falls back to PIT resolution if
// calibration failed.
uint64_t tsc_us(void);

// Elapsed microseconds since a previous tsc_us() reading.
static inline uint64_t tsc_us_since(uint64_t start_us)
{
    uint64_t now = tsc_us();
    return now > start_us ? now - start_us : 0;
}

// Busy-wait for a short interval. For sub-tick delays where descheduling makes
// no sense (hardware register timing, for example).
void tsc_delay_us(uint32_t microseconds);

#endif // KERNEL_ARCH_X86_64_TSC_H
