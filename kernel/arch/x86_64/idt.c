#include "idt.h"
#include <stdint.h>

// IDT entries and pointer
static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idtp;

// External function to load IDT (defined in idt_load.asm)
extern void idt_load(uint64_t);

// Set an IDT gate
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    
    idt[num].selector = selector;
    idt[num].ist = 0;  // No IST for now
    idt[num].type_attr = flags;
    idt[num].zero = 0;
}

// Initialize the IDT
void idt_init(void) {
    // Set up the IDT pointer
    idtp.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idtp.base = (uint64_t)&idt;
    
    // Clear the IDT
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].zero = 0;
    }
    
    // Load the IDT
    idt_load((uint64_t)&idtp);
}
