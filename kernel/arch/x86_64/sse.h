// sse.h - Turning on SSE, and keeping it working across context switches.
//
// WHY THE KERNEL STARTS WITH SSE OFF
// ----------------------------------
// The Makefile builds with -mno-sse -mno-mmx -mno-sse2, and that is deliberate.
// Out of reset, the CPU has SSE disabled: CR0.EM (emulation) is set and
// CR4.OSFXSR is clear, so any SSE instruction raises #UD. More importantly,
// SSE adds 512 bytes of architectural state - sixteen 128-bit XMM registers
// plus MXCSR plus the x87 stack - that a context switch must preserve. A kernel
// that quietly lets the compiler emit SSE without saving that state corrupts
// registers across preemption in ways that are extremely hard to debug.
//
// So the rule is: no compiler-generated SSE anywhere, and SSE used explicitly
// only after this file has (a) checked the CPU supports it, (b) enabled it in
// the control registers, and (c) taught the context switcher to save it.
//
// ENABLING IT
// -----------
//   CR0.EM  (bit 2)  must be CLEAR - set means "emulate FPU", which traps
//   CR0.MP  (bit 1)  set - monitor coprocessor, pairs with TS for lazy switching
//   CR4.OSFXSR      (bit 9)  set - "the OS uses FXSAVE/FXRSTOR", enables SSE
//   CR4.OSXMMEXCPT  (bit 10) set - unmasked SSE exceptions raise #XM (19)
//                                  rather than the misleading #UD
//
// SAVING IT
// ---------
// FXSAVE writes a 512-byte image of the whole x87+SSE state; FXRSTOR reads it
// back. The buffer must be 16-byte aligned or the instruction faults, which is
// why process_t places fpu_state immediately after the 144-byte register block
// (144 is a multiple of 16) and process.c asserts the offset at compile time.
//
// The cost is 512 bytes of memory traffic per switch. At 100 Hz with a handful
// of processes that is a few hundred kilobytes per second - irrelevant next to
// what it buys, which is being able to use SSE at all.
//
// A production kernel would do this lazily: set CR0.TS on switch, let the first
// SSE instruction in the new process trap with #NM, and only then swap state.
// Processes that never touch SSE never pay. That is a good future optimisation
// and is noted in the performance docs; the eager version here is simpler and
// obviously correct.

#ifndef KERNEL_ARCH_X86_64_SSE_H
#define KERNEL_ARCH_X86_64_SSE_H

#include <stdint.h>
#include <stddef.h>

// Size and alignment of the FXSAVE image.
#define FPU_STATE_SIZE  512
#define FPU_STATE_ALIGN 16

// Probes CPUID and enables SSE if the CPU supports SSE2 and FXSAVE.
// Returns 1 if SSE is now usable.
int sse_init(void);

int sse_is_enabled(void);

// Written by sse_init(), read directly by context_switch.asm to decide whether
// to execute FXSAVE/FXRSTOR. Declared here so the dependency is visible.
extern volatile uint8_t sse_enabled;

// Prepares a 512-byte FXSAVE image for a brand-new process.
//
// This matters more than it looks: FXRSTOR from a zeroed buffer leaves MXCSR
// at 0, which unmasks every SSE exception, so the first inexact result raises
// #XM. Worse, FXRSTOR faults outright if reserved MXCSR bits are set. Writing
// sane defaults avoids both.
void sse_init_fpu_state(void* buffer);

// 16-byte-wide pixel copy. Falls back to the caller's responsibility to check
// sse_is_enabled() first.
void sse_copy_pixels(uint32_t* dst, const uint32_t* src, uint32_t count);

// Reports what the CPU said it supports, for the `cpuinfo` command.
typedef struct {
    char vendor[13];
    char brand[49];
    int  has_sse;
    int  has_sse2;
    int  has_sse3;
    int  has_ssse3;
    int  has_sse41;
    int  has_sse42;
    int  has_avx;
    int  has_fxsr;
    int  has_invariant_tsc;
    uint32_t family;
    uint32_t model;
} cpu_features_t;

const cpu_features_t* cpu_get_features(void);
void                  cpu_detect_features(void);

#endif // KERNEL_ARCH_X86_64_SSE_H
