; boot.asm - Multiboot header and kernel entry point
; This is the very first code that runs when the OS boots

section .multiboot
    ; Multiboot2 header for GRUB
    MAGIC       equ 0xE85250D6              ; Multiboot2 magic number
    ARCH        equ 0                        ; i386 protected mode
    LENGTH      equ multiboot_end - multiboot_start
    CHECKSUM    equ -(MAGIC + ARCH + LENGTH)

multiboot_start:
    dd MAGIC
    dd ARCH
    dd LENGTH
    dd CHECKSUM

    ; End tag
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
multiboot_end:

section .bss
align 16
stack_bottom:
    resb 16384  ; 16 KB stack
stack_top:

; Page tables for initial identity mapping
align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096

; Storage for multiboot info
multiboot_magic:
    resd 1
multiboot_addr:
    resq 1

section .text
bits 32
global _start
extern kernel_main

_start:
    ; GRUB loads us in 32-bit protected mode
    ; Save multiboot info to memory (not stack)
    mov [multiboot_magic], eax
    mov [multiboot_addr], ebx
    
    ; Set up stack
    mov esp, stack_top

    ; Set up page tables for long mode
    ; Clear page tables
    mov edi, pml4
    mov ecx, 3 * 4096 / 4  ; 3 pages
    xor eax, eax
    rep stosd
    
    ; Set up identity mapping for first 2MB
    ; PML4[0] -> PDPT
    mov eax, pdpt
    or eax, 0x03    ; Present + Writable
    mov [pml4], eax
    
    ; PDPT[0] -> PD
    mov eax, pd
    or eax, 0x03
    mov [pdpt], eax
    
    ; PD[0] = 2MB page at 0x0
    mov eax, 0x83   ; Present + Writable + Huge (2MB page)
    mov [pd], eax

    ; Load page table
    mov eax, pml4
    mov cr3, eax

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Enable long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Load 64-bit GDT
    lgdt [gdt64.pointer]
    
    ; Jump to 64-bit code
    jmp gdt64.code:long_mode_start

bits 64
long_mode_start:
    ; Set up segment registers
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Set up 64-bit stack
    mov rsp, stack_top
    
    ; Load multiboot info from memory
    mov edi, [multiboot_magic]
    mov rsi, [multiboot_addr]
    
    ; Call kernel
    call kernel_main

    ; Hang if kernel returns
    cli
.hang:
    hlt
    jmp .hang

section .rodata
gdt64:
    dq 0                        ; Null descriptor
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)  ; Code segment
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)  ; Data segment
.pointer:
    dw $ - gdt64 - 1
    dq gdt64
