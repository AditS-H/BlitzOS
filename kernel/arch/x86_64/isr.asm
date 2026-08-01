; isr.asm - Interrupt entry stubs.
;
; Every stub builds an identical `registers_t` frame on the stack (see regs.h)
; and hands the C handler a pointer to it. That uniform frame is what lets the
; panic screen print real register values, and what lets the timer IRQ switch
; processes: the half-finished frame just waits on the outgoing process's
; kernel stack until it gets scheduled again.
;
; Stack when the C handler runs, lowest address first:
;   r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax   <- pushed here
;   int_no err_code                                             <- pushed here
;   rip cs rflags rsp ss                                        <- pushed by CPU

section .text
bits 64

extern isr_handler
extern irq_handler
extern syscall_dispatch

; ---------------------------------------------------------------------------
; Frame construction
;
; NOTE: no CLI here. All gates are installed as interrupt gates (type 0xE),
; which clear IF automatically on entry. The old explicit CLI was redundant.
; ---------------------------------------------------------------------------

%macro PUSH_ALL 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_ALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

; Exceptions that do NOT push an error code: push a dummy so every frame is
; the same shape and regs.h can use fixed offsets.
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common_stub
%endmacro

; Exceptions where the CPU already pushed an error code.
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push qword %1
    jmp isr_common_stub
%endmacro

; Hardware IRQs, remapped to vectors 32..47.
%macro IRQ 2
global irq%1
irq%1:
    push qword 0
    push qword %2
    jmp irq_common_stub
%endmacro

; ---------------------------------------------------------------------------
; CPU exceptions (vectors 0-31)
; ---------------------------------------------------------------------------

ISR_NOERRCODE 0     ; #DE Divide by zero
ISR_NOERRCODE 1     ; #DB Debug
ISR_NOERRCODE 2     ;     Non-maskable interrupt
ISR_NOERRCODE 3     ; #BP Breakpoint
ISR_NOERRCODE 4     ; #OF Overflow
ISR_NOERRCODE 5     ; #BR Bound range exceeded
ISR_NOERRCODE 6     ; #UD Invalid opcode
ISR_NOERRCODE 7     ; #NM Device not available
ISR_ERRCODE   8     ; #DF Double fault
ISR_NOERRCODE 9     ;     Coprocessor segment overrun
ISR_ERRCODE   10    ; #TS Invalid TSS
ISR_ERRCODE   11    ; #NP Segment not present
ISR_ERRCODE   12    ; #SS Stack-segment fault
ISR_ERRCODE   13    ; #GP General protection fault
ISR_ERRCODE   14    ; #PF Page fault
ISR_NOERRCODE 15    ;     Reserved
ISR_NOERRCODE 16    ; #MF x87 floating point
ISR_ERRCODE   17    ; #AC Alignment check
ISR_NOERRCODE 18    ; #MC Machine check
ISR_NOERRCODE 19    ; #XM SIMD floating point
ISR_NOERRCODE 20    ; #VE Virtualization
ISR_ERRCODE   21    ; #CP Control protection
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30    ; #SX Security exception
ISR_NOERRCODE 31    ;     Reserved

; ---------------------------------------------------------------------------
; Hardware IRQs (PIC remapped to 32-47)
; ---------------------------------------------------------------------------

IRQ 0, 32   ; PIT timer
IRQ 1, 33   ; Keyboard
IRQ 2, 34   ; Cascade from the slave PIC
IRQ 3, 35   ; COM2
IRQ 4, 36   ; COM1
IRQ 5, 37   ; LPT2
IRQ 6, 38   ; Floppy
IRQ 7, 39   ; LPT1 (also spurious IRQ7)
IRQ 8, 40   ; RTC
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44  ; PS/2 mouse
IRQ 13, 45  ; FPU
IRQ 14, 46  ; Primary ATA
IRQ 15, 47  ; Secondary ATA

; ---------------------------------------------------------------------------
; Common stubs
; ---------------------------------------------------------------------------

isr_common_stub:
    PUSH_ALL
    cld                     ; SysV requires DF=0 on entry to C code
    mov rdi, rsp            ; registers_t*
    call isr_handler
    POP_ALL
    add rsp, 16             ; drop int_no and err_code
    iretq

irq_common_stub:
    PUSH_ALL
    cld
    mov rdi, rsp            ; registers_t*
    call irq_handler        ; sends EOI and may switch processes
    POP_ALL
    add rsp, 16
    iretq

; ---------------------------------------------------------------------------
; System call entry (INT 0x80)
;
; This is the piece that was missing: syscall_init() printed a banner but no
; IDT gate ever pointed at anything, so `int 0x80` hit a zeroed descriptor and
; triple-faulted the machine.
;
; Calling convention (unchanged from syscall.h):
;   RAX = syscall number
;   RBX, RCX, RDX, RSI, RDI = arguments
;   RAX = return value
;
; The dispatcher writes the result into the saved RAX slot of the frame, and
; POP_ALL loads it back into the register before IRETQ.
; ---------------------------------------------------------------------------

global syscall_stub
syscall_stub:
    push qword 0            ; dummy error code, keeps the frame uniform
    push qword 0x80         ; vector number
    PUSH_ALL
    cld
    mov rdi, rsp            ; registers_t*
    call syscall_dispatch
    POP_ALL
    add rsp, 16
    iretq
