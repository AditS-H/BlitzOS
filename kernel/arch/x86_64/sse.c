#include "sse.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

// Read by context_switch.asm. Volatile because the assembly reads it without
// the compiler knowing.
volatile uint8_t sse_enabled = 0;

static cpu_features_t features;

// ---------------------------------------------------------------------------
// CPUID
// ---------------------------------------------------------------------------

static inline void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx,
                         uint32_t* ecx, uint32_t* edx)
{
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

void cpu_detect_features(void)
{
    memset(&features, 0, sizeof(features));

    uint32_t eax, ebx, ecx, edx;

    // Leaf 0: vendor string, spread across EBX, EDX, ECX in that odd order.
    cpuid(0, &eax, &ebx, &ecx, &edx);
    memcpy(features.vendor + 0, &ebx, 4);
    memcpy(features.vendor + 4, &edx, 4);
    memcpy(features.vendor + 8, &ecx, 4);
    features.vendor[12] = '\0';

    // Leaf 1: family/model and the main feature bits.
    cpuid(1, &eax, &ebx, &ecx, &edx);
    features.family = (eax >> 8) & 0xF;
    features.model  = (eax >> 4) & 0xF;

    features.has_fxsr  = (edx >> 24) & 1;
    features.has_sse   = (edx >> 25) & 1;
    features.has_sse2  = (edx >> 26) & 1;
    features.has_sse3  = (ecx >> 0)  & 1;
    features.has_ssse3 = (ecx >> 9)  & 1;
    features.has_sse41 = (ecx >> 19) & 1;
    features.has_sse42 = (ecx >> 20) & 1;
    features.has_avx   = (ecx >> 28) & 1;

    // Extended leaves: brand string and invariant TSC.
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    uint32_t max_extended = eax;

    if (max_extended >= 0x80000004) {
        // The brand string arrives in three 16-byte chunks.
        uint32_t* brand = (uint32_t*)features.brand;
        for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            cpuid(leaf, &eax, &ebx, &ecx, &edx);
            *brand++ = eax;
            *brand++ = ebx;
            *brand++ = ecx;
            *brand++ = edx;
        }
        features.brand[48] = '\0';
    }

    if (max_extended >= 0x80000007) {
        cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
        features.has_invariant_tsc = (edx >> 8) & 1;
    }
}

const cpu_features_t* cpu_get_features(void)
{
    return &features;
}

// ---------------------------------------------------------------------------
// Enabling SSE
// ---------------------------------------------------------------------------

int sse_init(void)
{
    cpu_detect_features();

    if (!features.has_sse2 || !features.has_fxsr) {
        kwarn("SSE: CPU lacks %s - staying on the scalar blitter\n",
              !features.has_sse2 ? "SSE2" : "FXSAVE");
        sse_enabled = 0;
        return 0;
    }

    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

    cr0 &= ~(1ULL << 2);   // clear EM: stop emulating the FPU, allow real SSE
    cr0 |=  (1ULL << 1);   // set MP: monitor coprocessor

    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    cr4 |= (1ULL << 9);    // OSFXSR: the OS saves state with FXSAVE, enable SSE
    cr4 |= (1ULL << 10);   // OSXMMEXCPT: report SSE faults as #XM, not #UD

    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    // Give the current (kernel init) context a sane MXCSR. Everything else
    // gets one from sse_init_fpu_state() at process creation.
    uint32_t mxcsr = 0x1F80;
    __asm__ volatile("ldmxcsr %0" :: "m"(mxcsr));

    sse_enabled = 1;

    kok("SSE2 enabled on %s%s%s\n",
        features.brand[0] ? features.brand : features.vendor,
        features.has_avx ? " (AVX present, unused)" : "",
        features.has_sse42 ? "" : "");

    return 1;
}

int sse_is_enabled(void)
{
    return sse_enabled != 0;
}

void sse_init_fpu_state(void* buffer)
{
    uint8_t* image = (uint8_t*)buffer;
    memset(image, 0, FPU_STATE_SIZE);

    // FXSAVE image layout, the fields that must not be left at zero:
    //   offset 0  FCW    x87 control word
    //   offset 24 MXCSR  SSE control/status
    //
    // 0x037F masks all x87 exceptions and selects extended precision.
    // 0x1F80 masks all six SSE exceptions, which is what every OS uses; a
    // zeroed MXCSR would unmask them and the first inexact result would trap.
    *(uint16_t*)(image + 0)  = 0x037F;
    *(uint32_t*)(image + 24) = 0x1F80;
}

// ---------------------------------------------------------------------------
// The SSE pixel blitter
//
// Written as inline assembly rather than intrinsics on purpose: the kernel is
// compiled with -mno-sse, so <emmintrin.h> is unavailable and the compiler
// would refuse to emit SSE anyway. The assembler has no such objection.
// ---------------------------------------------------------------------------

// __attribute__((target("sse2"))) is required, not decorative. The whole
// kernel is built with -mno-sse, which makes GCC treat the XMM registers as
// nonexistent - it rejects an asm block that names them in its clobber list
// with "register xmm0 cannot be clobbered for the current target". The
// attribute re-enables them for this one function without letting the compiler
// emit SSE anywhere else.
__attribute__((target("sse2")))
void sse_copy_pixels(uint32_t* dst, const uint32_t* src, uint32_t count)
{
    if (!sse_enabled) {
        for (uint32_t i = 0; i < count; i++) {
            dst[i] = src[i];
        }
        return;
    }

    uint32_t i = 0;

    // Prologue: scalar copies until the destination is 16-byte aligned. The
    // non-temporal store below faults on a misaligned address, so this is not
    // optional.
    while (i < count && (((uintptr_t)(dst + i)) & 15)) {
        dst[i] = src[i];
        i++;
    }

    // Main loop: 16 pixels (64 bytes) per iteration.
    //
    // MOVDQU loads because the source alignment is not guaranteed.
    // MOVNTDQ stores because the destination is video memory we will never
    // read back - a non-temporal store writes straight out without pulling the
    // cache line in first, which for a framebuffer avoids evicting genuinely
    // useful data and skips a read-for-ownership per line.
    while (i + 16 <= count) {
        __asm__ volatile(
            "movdqu   (%0), %%xmm0\n\t"
            "movdqu 16(%0), %%xmm1\n\t"
            "movdqu 32(%0), %%xmm2\n\t"
            "movdqu 48(%0), %%xmm3\n\t"
            "movntdq %%xmm0,   (%1)\n\t"
            "movntdq %%xmm1, 16(%1)\n\t"
            "movntdq %%xmm2, 32(%1)\n\t"
            "movntdq %%xmm3, 48(%1)"
            :
            : "r"(src + i), "r"(dst + i)
            : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
        i += 16;
    }

    // Four pixels at a time for the tail.
    while (i + 4 <= count) {
        __asm__ volatile(
            "movdqu (%0), %%xmm0\n\t"
            "movntdq %%xmm0, (%1)"
            :
            : "r"(src + i), "r"(dst + i)
            : "xmm0", "memory");
        i += 4;
    }

    while (i < count) {
        dst[i] = src[i];
        i++;
    }

    // Non-temporal stores are weakly ordered: without a fence, a later read of
    // this memory (or the GPU's scanout) could see stale data.
    __asm__ volatile("sfence" ::: "memory");
}
