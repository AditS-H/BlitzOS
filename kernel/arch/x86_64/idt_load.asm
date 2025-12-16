; IDT loading routine
section .text
global idt_load

idt_load:
    lidt [rdi]  ; Load IDT pointer (first argument in rdi)
    ret
