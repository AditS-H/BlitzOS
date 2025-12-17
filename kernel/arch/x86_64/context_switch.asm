; Save and restore CPU context for process switching
; This is critical for multitasking!

global context_switch_asm

; Offsets in process_t structure (must match process.h!)
; Process_t layout:
;   uint32_t pid;        // 0
;   uint32_t parent_pid; // 4
;   char name[32];       // 8
;   uint32_t state;      // 40
;   padding              // 44
;   registers struct     // 48 <- registers start here!

REGS_BASE    equ 48

OFFSET_RAX   equ REGS_BASE + 0
OFFSET_RBX   equ REGS_BASE + 8
OFFSET_RCX   equ REGS_BASE + 16
OFFSET_RDX   equ REGS_BASE + 24
OFFSET_RSI   equ REGS_BASE + 32
OFFSET_RDI   equ REGS_BASE + 40
OFFSET_RBP   equ REGS_BASE + 48
OFFSET_RSP   equ REGS_BASE + 56
OFFSET_R8    equ REGS_BASE + 64
OFFSET_R9    equ REGS_BASE + 72
OFFSET_R10   equ REGS_BASE + 80
OFFSET_R11   equ REGS_BASE + 88
OFFSET_R12   equ REGS_BASE + 96
OFFSET_R13   equ REGS_BASE + 104
OFFSET_R14   equ REGS_BASE + 112
OFFSET_R15   equ REGS_BASE + 120
OFFSET_RIP   equ REGS_BASE + 128
OFFSET_RFLAGS equ REGS_BASE + 136

section .text
bits 64

; void context_switch_asm(process_t* current, process_t* next)
; RDI = pointer to current process_t structure (to save into)
; RSI = pointer to next process_t structure (to load from)
context_switch_asm:
    ; Save current process state (if current != NULL)
    test rdi, rdi
    jz load_next  ; Skip saving if current is NULL
    
    ; Save all registers to current process
    mov [rdi + OFFSET_RAX], rax
    mov [rdi + OFFSET_RBX], rbx
    mov [rdi + OFFSET_RCX], rcx
    mov [rdi + OFFSET_RDX], rdx
    mov [rdi + OFFSET_RSI], rsi
    mov [rdi + OFFSET_RDI], rdi
    mov [rdi + OFFSET_RBP], rbp
    mov [rdi + OFFSET_RSP], rsp
    mov [rdi + OFFSET_R8],  r8
    mov [rdi + OFFSET_R9],  r9
    mov [rdi + OFFSET_R10], r10
    mov [rdi + OFFSET_R11], r11
    mov [rdi + OFFSET_R12], r12
    mov [rdi + OFFSET_R13], r13
    mov [rdi + OFFSET_R14], r14
    mov [rdi + OFFSET_R15], r15
    
    ; Save RIP (return address from this function)
    mov rax, [rsp]
    mov [rdi + OFFSET_RIP], rax
    
load_next:
    ; Load next process state from RSI
    mov rax, [rsi + OFFSET_RAX]
    mov rbx, [rsi + OFFSET_RBX]
    mov rcx, [rsi + OFFSET_RCX]
    mov rdx, [rsi + OFFSET_RDX]
    mov rbp, [rsi + OFFSET_RBP]
    mov rsp, [rsi + OFFSET_RSP]
    mov r8,  [rsi + OFFSET_R8]
    mov r9,  [rsi + OFFSET_R9]
    mov r10, [rsi + OFFSET_R10]
    mov r11, [rsi + OFFSET_R11]
    mov r12, [rsi + OFFSET_R12]
    mov r13, [rsi + OFFSET_R13]
    mov r14, [rsi + OFFSET_R14]
    mov r15, [rsi + OFFSET_R15]
    
    ; Load RDI and RSI last
    mov rdi, [rsi + OFFSET_RDI]
    push qword [rsi + OFFSET_RIP]  ; Push return address
    mov rsi, [rsi + OFFSET_RSI]
    
    ; Enable interrupts
    sti
    
    ; Jump to next process (RIP is on stack)
    ret

