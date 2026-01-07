; syscall_asm.asm - INT 0x80 handler for syscalls
; Saves registers, calls syscall_handler, restores registers

[BITS 64]

extern syscall_handler

global syscall_asm

; INT 0x80 handler
; Stack at entry:
;   RSP+0: RIP (return address)
;   RSP+8: CS
;   RSP+16: RFLAGS
;   RSP+24: RSP (user)
;   RSP+32: SS
syscall_asm:
    ; Save all registers that syscall_handler doesn't preserve
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
    
    ; Call syscall_handler(rax, rbx, rcx, rdx, rsi, rdi)
    ; The registers are already in place from the syscall
    ; rax = syscall number
    ; rbx, rcx, rdx, rsi, rdi = arguments
    
    ; Restore rax, rbx, rcx, rdx, rsi, rdi for the call
    mov rax, [rsp + 120]  ; Saved rax
    mov rbx, [rsp + 112]  ; Saved rbx
    mov rcx, [rsp + 104]  ; Saved rcx
    mov rdx, [rsp + 96]   ; Saved rdx
    mov rsi, [rsp + 88]   ; Saved rsi
    mov rdi, [rsp + 80]   ; Saved rdi
    
    ; Call syscall_handler(rax, rbx, rcx, rdx, rsi, rdi)
    ; Arguments are already in the right registers per x86_64 ABI
    call syscall_handler
    
    ; RAX now contains the return value
    ; Save it for restoration
    mov [rsp + 120], rax
    
    ; Restore all registers
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
    
    ; Return from interrupt
    iretq
