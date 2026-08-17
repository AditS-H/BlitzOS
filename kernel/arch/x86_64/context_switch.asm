; context_switch.asm - Save and restore CPU context for process switching.
;
; void context_switch_asm(process_t* current, process_t* next)
;   RDI = process to save into (may be NULL: "nothing to save")
;   RSI = process to resume
;
; The saved context lives at offset 0 of process_t (cpu_context_t). process.c
; carries _Static_asserts that pin these offsets, so reordering the C struct
; breaks the build instead of silently corrupting RSP at runtime.

global context_switch_asm

; Set by sse_init() once SSE is actually usable. FXSAVE/FXRSTOR raise #UD if
; CR4.OSFXSR is clear, so on a CPU without SSE2 we must skip them entirely.
extern sse_enabled

; FXSAVE image, immediately after the 144-byte register block. process.c
; asserts this offset and its 16-byte alignment at compile time - FXSAVE faults
; on a misaligned destination.
OFFSET_FPU    equ 144

OFFSET_RAX    equ 0
OFFSET_RBX    equ 8
OFFSET_RCX    equ 16
OFFSET_RDX    equ 24
OFFSET_RSI    equ 32
OFFSET_RDI    equ 40
OFFSET_RBP    equ 48
OFFSET_RSP    equ 56
OFFSET_R8     equ 64
OFFSET_R9     equ 72
OFFSET_R10    equ 80
OFFSET_R11    equ 88
OFFSET_R12    equ 96
OFFSET_R13    equ 104
OFFSET_R14    equ 112
OFFSET_R15    equ 120
OFFSET_RIP    equ 128
OFFSET_RFLAGS equ 136

section .text
bits 64

context_switch_asm:
    ; Capture RFLAGS before anything else. The TEST below overwrites ZF, and we
    ; would end up saving the comparison result instead of the real flags.
    pushfq                              ; RSP = entry_rsp - 8

    test rdi, rdi
    jz .no_save

    ; ---- Save the outgoing process ----
    mov [rdi + OFFSET_RAX], rax
    mov [rdi + OFFSET_RBX], rbx
    mov [rdi + OFFSET_RCX], rcx
    mov [rdi + OFFSET_RDX], rdx
    mov [rdi + OFFSET_RSI], rsi
    mov [rdi + OFFSET_RDI], rdi         ; caller-saved in SysV; stored for completeness
    mov [rdi + OFFSET_RBP], rbp
    mov [rdi + OFFSET_R8],  r8
    mov [rdi + OFFSET_R9],  r9
    mov [rdi + OFFSET_R10], r10
    mov [rdi + OFFSET_R11], r11
    mov [rdi + OFFSET_R12], r12
    mov [rdi + OFFSET_R13], r13
    mov [rdi + OFFSET_R14], r14
    mov [rdi + OFFSET_R15], r15

    ; RAX is already saved, so it is free to use as scratch from here.
    pop rax                             ; RAX = caller's RFLAGS, RSP = entry_rsp
    mov [rdi + OFFSET_RFLAGS], rax

    ; Resume point = our return address.
    mov rax, [rsp]
    mov [rdi + OFFSET_RIP], rax

    ; Save the stack pointer as it will be *after* returning, i.e. entry_rsp+8.
    ;
    ; The previous version saved entry_rsp instead, which left the stale return
    ; address on the stack after every resume. That leaked 8 bytes per context
    ; switch - roughly 2000 switches (20 seconds at 100 Hz) before a kernel
    ; stack silently overflowed into whatever was allocated below it.
    lea rax, [rsp + 8]
    mov [rdi + OFFSET_RSP], rax

    ; ---- Save x87 + SSE state ----
    ;
    ; 512 bytes of XMM registers, the x87 stack and MXCSR. Without this, any
    ; process that touches SSE would have its vector registers silently
    ; clobbered by the next process to run - a corruption that shows up as
    ; wrong pixels or wrong arithmetic far from the actual switch.
    ;
    ; RAX is already saved above, so it is free as scratch.
    mov al, [sse_enabled]
    test al, al
    jz .load

    fxsave [rdi + OFFSET_FPU]
    jmp .load

.no_save:
    add rsp, 8                          ; discard the RFLAGS we pushed

.load:
    ; ---- Restore x87 + SSE state ----
    ; Before the general purpose registers, because this needs a scratch
    ; register and RSI (the pointer to `next`) is still live.
    mov al, [sse_enabled]
    test al, al
    jz .load_gprs

    fxrstor [rsi + OFFSET_FPU]

.load_gprs:
    ; ---- Restore the incoming process ----
    ; RSI still points at `next`, so it must be loaded last.
    mov rsp, [rsi + OFFSET_RSP]

    ; Stage [RFLAGS][RIP] on the new stack. Net stack effect of
    ; push/push/popfq/ret is zero, so we land with RSP exactly as saved.
    push qword [rsi + OFFSET_RIP]
    push qword [rsi + OFFSET_RFLAGS]

    mov rax, [rsi + OFFSET_RAX]
    mov rbx, [rsi + OFFSET_RBX]
    mov rcx, [rsi + OFFSET_RCX]
    mov rdx, [rsi + OFFSET_RDX]
    mov rbp, [rsi + OFFSET_RBP]
    mov r8,  [rsi + OFFSET_R8]
    mov r9,  [rsi + OFFSET_R9]
    mov r10, [rsi + OFFSET_R10]
    mov r11, [rsi + OFFSET_R11]
    mov r12, [rsi + OFFSET_R12]
    mov r13, [rsi + OFFSET_R13]
    mov r14, [rsi + OFFSET_R14]
    mov r15, [rsi + OFFSET_R15]

    mov rdi, [rsi + OFFSET_RDI]
    mov rsi, [rsi + OFFSET_RSI]         ; RSI dies here - must be the last load

    ; POPFQ restores the interrupt flag exactly as the target process left it.
    ; The old code did an unconditional STI, which re-enabled interrupts even
    ; when switching into a critical section.
    ;
    ; If an interrupt fires in the one-instruction window between POPFQ and RET,
    ; the stack is already well formed (return address on top), so the handler
    ; nests and returns harmlessly.
    popfq
    ret
