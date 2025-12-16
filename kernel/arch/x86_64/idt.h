#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// IDT Entry structure
typedef struct {
    uint16_t offset_low;    // Offset bits 0..15
    uint16_t selector;      // Code segment selector in GDT
    uint8_t  ist;          // Interrupt Stack Table offset (bits 0..2), rest reserved
    uint8_t  type_attr;    // Type and attributes
    uint16_t offset_mid;    // Offset bits 16..31
    uint32_t offset_high;   // Offset bits 32..63
    uint32_t zero;         // Reserved
} __attribute__((packed)) idt_entry_t;

// IDT Pointer structure
typedef struct {
    uint16_t limit;        // Size of IDT - 1
    uint64_t base;         // Base address of IDT
} __attribute__((packed)) idt_ptr_t;

// Number of IDT entries
#define IDT_ENTRIES 256

// Initialize the IDT
void idt_init(void);

// Set an IDT entry
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags);

#endif // IDT_H
