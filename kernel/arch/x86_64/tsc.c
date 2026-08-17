#include "tsc.h"
#include "../../lib/kprintf.h"
#include "../../../drivers/pit.h"

static uint64_t cycles_per_us = 0;
static uint64_t tsc_origin    = 0;
static int      calibrated    = 0;

// How many PIT ticks to measure over. 50 ticks at 100 Hz is 500 ms: long
// enough that the +/-1 tick quantisation error stays under 2%, short enough
// that nobody notices it during boot.
#define CALIBRATION_TICKS 50

// Checks CPUID leaf 0x80000007 bit 8 for an invariant TSC - one that ticks at
// a constant rate regardless of CPU frequency scaling or idle states.
static int tsc_is_invariant(void)
{
    uint32_t eax, ebx, ecx, edx;

    // First check the highest extended leaf the CPU supports.
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0x80000000));
    if (eax < 0x80000007) {
        return 0;
    }

    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0x80000007));
    return (edx & (1 << 8)) != 0;
}

void tsc_init(void)
{
    uint64_t start_tick = pit_get_ticks();

    // Wait for a tick boundary first. Starting mid-tick would add up to 10 ms
    // of error to a 500 ms measurement.
    while (pit_get_ticks() == start_tick) {
        __asm__ volatile("pause");
    }

    uint64_t tsc_start  = rdtsc();
    uint64_t tick_start = pit_get_ticks();

    while (pit_get_ticks() - tick_start < CALIBRATION_TICKS) {
        __asm__ volatile("pause");
    }

    uint64_t tsc_end      = rdtsc();
    uint64_t ticks_waited = pit_get_ticks() - tick_start;

    uint64_t elapsed_us = ticks_waited * (uint64_t)MS_PER_TICK * 1000;
    uint64_t cycles     = tsc_end - tsc_start;

    if (elapsed_us == 0 || cycles == 0) {
        kwarn("TSC: calibration failed, falling back to 10 ms timer resolution\n");
        calibrated = 0;
        return;
    }

    cycles_per_us = cycles / elapsed_us;
    tsc_origin    = tsc_end;
    calibrated    = cycles_per_us > 0;

    if (!calibrated) {
        kwarn("TSC: calibrated to 0 cycles/us, which cannot be right\n");
        return;
    }

    kok("TSC: %lu MHz (%lu cycles/us)%s\n",
        cycles_per_us, cycles_per_us,
        tsc_is_invariant() ? ", invariant" : ", NOT invariant - timings may drift");
}

int tsc_is_calibrated(void)
{
    return calibrated;
}

uint64_t tsc_cycles_per_us(void)
{
    return cycles_per_us;
}

uint64_t tsc_us(void)
{
    if (!calibrated) {
        // Better than nothing: PIT resolution, in microseconds.
        return pit_get_ticks() * (uint64_t)MS_PER_TICK * 1000;
    }

    uint64_t now = rdtsc();
    if (now < tsc_origin) {
        return 0;   // counter was reset under us somehow
    }
    return (now - tsc_origin) / cycles_per_us;
}

void tsc_delay_us(uint32_t microseconds)
{
    if (!calibrated) {
        // Round up to whole ticks. Coarse, but correct.
        pit_busy_wait((microseconds + (MS_PER_TICK * 1000) - 1) /
                      (MS_PER_TICK * 1000));
        return;
    }

    uint64_t deadline = rdtsc() + (uint64_t)microseconds * cycles_per_us;
    while (rdtsc() < deadline) {
        // PAUSE hints to the CPU that this is a spin loop, which saves power
        // and avoids a memory-order violation penalty on exit.
        __asm__ volatile("pause");
    }
}
