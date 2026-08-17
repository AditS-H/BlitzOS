; boot.asm - Multiboot2 header, long mode entry, and the GDT.
;
; GRUB hands us a 32-bit protected mode machine with paging off. This file
; builds identity-mapped page tables, switches to long mode, and calls
; kernel_main.

; Segment selectors. These must match GDT_* in gdt.h.
GDT_KERNEL_CODE equ 0x08
GDT_KERNEL_DATA equ 0x10

section .multiboot
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
    resb 16384  ; 16 KB boot stack (abandoned once the scheduler starts)
stack_top:

; Initial page tables.
;
; pd0..pd3 MUST stay adjacent: the mapping loop below walks all four as one
; flat array of 2048 entries.
align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd0:
    resb 4096   ; 0-1 GB
pd1:
    resb 4096   ; 1-2 GB
pd2:
    resb 4096   ; 2-3 GB
pd3:
    resb 4096   ; 3-4 GB  (the PCI MMIO hole lives here)
pt0:
    resb 4096   ; 4 KB granularity for the first 2 MB (see below)

; Multiboot info, stashed before we have a usable stack
multiboot_magic:
    resd 1
multiboot_addr:
    resq 1

section .text
bits 32
global _start
extern kernel_main

_start:
    ; GRUB leaves the magic in EAX and the info pointer in EBX.
    mov [multiboot_magic], eax
    mov [multiboot_addr], ebx

    mov esp, stack_top

    ; ---- Clear the page tables ----
    mov edi, pml4
    mov ecx, 7 * 4096 / 4   ; pml4, pdpt, pd0-pd3, pt0
    xor eax, eax
    rep stosd

    ; PML4[0] -> PDPT
    mov eax, pdpt
    or eax, 0x03            ; present | writable
    mov [pml4], eax

    ; PDPT[0..3] -> pd0..pd3, covering 0-4 GB
    mov eax, pd0
    or eax, 0x03
    mov [pdpt + 0], eax
    mov eax, pd1
    or eax, 0x03
    mov [pdpt + 8], eax
    mov eax, pd2
    or eax, 0x03
    mov [pdpt + 16], eax
    mov eax, pd3
    or eax, 0x03
    mov [pdpt + 24], eax

    ; ---- Identity map the first 4 GB with 2048 x 2 MB pages ----
    ;
    ; Originally this mapped a single 2 MB page. Two separate problems forced it
    ; wider:
    ;
    ;   1 GB: the heap hands out raw physical pages and uses them as virtual
    ;   addresses, so the first allocation past the mapped region page-faulted
    ;   on the kernel's own memory.
    ;
    ;   4 GB: PCI devices expose their memory through BARs up in the MMIO hole
    ;   just below 4 GB - QEMU puts the graphics adapter's framebuffer around
    ;   0xFD000000. Without a mapping there, touching the framebuffer faults, so
    ;   there is no way to draw anything.
    ;
    ; pd0..pd3 are contiguous in .bss, so one loop fills all four.
    ; Cost: 16 KB of page tables for the entire low 4 GB.
    mov edi, pd0
    mov eax, 0x83           ; present | writable | huge (2 MB)
    mov ecx, 2048           ; 4 tables x 512 entries
.map_2mb_pages:
    mov [edi], eax
    mov dword [edi + 4], 0  ; high half of the entry
    add eax, 0x200000       ; next 2 MB frame
    add edi, 8
    loop .map_2mb_pages

    ; ---- Re-map the first 2 MB at 4 KB granularity, leaving page 0 unmapped ----
    ;
    ; With a flat identity map, address 0 is a perfectly valid writable page, so
    ; a NULL pointer dereference silently scribbles over the BIOS data area and
    ; the bug surfaces somewhere else entirely. Leaving virtual 0x0-0xFFF with
    ; no mapping turns every NULL dereference into a page fault, which the
    ; panic handler reports with the faulting address and a "likely NULL
    ; pointer" hint.
    ;
    ; The rest of the low 2 MB still has to be mapped - VGA text memory lives
    ; at 0xB8000 - so only the very first page is sacrificed.
    mov edi, pt0
    add edi, 8              ; skip entry 0: virtual 0x0-0xFFF stays absent
    mov eax, 0x1000 | 0x03  ; second page onwards, present | writable
    mov ecx, 511
.map_low_4k:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x1000
    add edi, 8
    loop .map_low_4k

    ; Point PD0[0] at that table instead of the 2 MB huge page.
    mov eax, pt0
    or eax, 0x03
    mov [pd0], eax
    mov dword [pd0 + 4], 0

    ; ---- Enter long mode ----
    mov eax, pml4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5          ; CR4.PAE
    mov cr4, eax

    mov ecx, 0xC0000080     ; EFER
    rdmsr
    or eax, 1 << 8          ; EFER.LME
    wrmsr

    mov eax, cr0
    or eax, 1 << 31         ; CR0.PG
    mov cr0, eax

    lgdt [gdt64_pointer]
    jmp GDT_KERNEL_CODE:long_mode_start

bits 64
long_mode_start:
    mov ax, GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    xor rbp, rbp            ; terminates panic()'s stack walk

    mov edi, [multiboot_magic]
    mov rsi, [multiboot_addr]

    call kernel_main

    ; kernel_main never returns (scheduler_start is noreturn), but if it
    ; somehow does, stop rather than execute whatever follows.
    cli
.hang:
    hlt
    jmp .hang

; ---------------------------------------------------------------------------
; Global Descriptor Table
;
; Lives in .data, not .rodata, because gdt_load_tss() writes the TSS descriptor
; into it at runtime.
;
;   0x00  null
;   0x08  kernel code   (ring 0, 64-bit)
;   0x10  kernel data   (ring 0)
;   0x18  user code     (ring 3, 64-bit)
;   0x20  user data     (ring 3)
;   0x28  TSS           (16 bytes - system descriptors are double width in
;                        long mode - filled in by gdt_load_tss)
;
; The user and TSS entries did not exist before. gdt_load_tss() was a no-op
; stub with a "TODO" in it, so the TSS the kernel allocated was never actually
; loaded and ring 3 had no segments to run in.
; ---------------------------------------------------------------------------

section .data
align 16
global gdt64
gdt64:
    dq 0                                                    ; 0x00 null
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)                 ; 0x08 kernel code
    dq (1<<41) | (1<<44) | (1<<47)                           ; 0x10 kernel data
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) | (3<<45)       ; 0x18 user code
    dq (1<<41) | (1<<44) | (1<<47) | (3<<45)                 ; 0x20 user data

global gdt64_tss_descriptor
gdt64_tss_descriptor:
    dq 0                                                    ; 0x28 TSS low
    dq 0                                                    ;      TSS high

gdt64_end:

global gdt64_pointer
gdt64_pointer:
    dw gdt64_end - gdt64 - 1
    dq gdt64
