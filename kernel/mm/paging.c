#include "paging.h"
#include "pmm.h"
#include "../../drivers/vga.h"
#include <stddef.h>

// Current PML4 (Page Map Level 4) table
static page_table_t* kernel_pml4 = NULL;

// Helper function to get page table index at each level
static inline uint64_t pml4_index(uint64_t virt) { return (virt >> 39) & 0x1FF; }
static inline uint64_t pdpt_index(uint64_t virt) { return (virt >> 30) & 0x1FF; }
static inline uint64_t pd_index(uint64_t virt)   { return (virt >> 21) & 0x1FF; }
static inline uint64_t pt_index(uint64_t virt)   { return (virt >> 12) & 0x1FF; }

// Helper to convert number to string
static void uint64_to_str(uint64_t num, char* buf) {
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char temp[32];
    int i = 0;
    while (num > 0) {
        temp[i++] = '0' + (num % 10);
        num /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

// Get or create page table
static page_table_t* get_or_create_table(pte_t* entry, uint64_t flags) {
    if (*entry & PAGE_PRESENT) {
        // Table exists, return its address
        return (page_table_t*)(*entry & ~0xFFF);
    }
    
    // Allocate new table
    page_table_t* table = (page_table_t*)pmm_alloc_page();
    if (!table) {
        return NULL; // Out of memory
    }
    
    // Clear the table
    for (int i = 0; i < 512; i++) {
        table->entries[i] = 0;
    }
    
    // Set entry to point to new table
    *entry = (uint64_t)table | flags | PAGE_PRESENT | PAGE_WRITABLE;
    
    return table;
}

// Initialize paging - use existing page tables from boot.asm
void paging_init(void) {
    vga_print("[*] Initializing paging...\n", VGA_COLOR_BROWN);
    
    // Get current PML4 from CR3 (set up by boot.asm)
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    kernel_pml4 = (page_table_t*)cr3;
    
    vga_print("    Using boot page tables at 0x", VGA_COLOR_WHITE);
    char hex[17];
    for (int j = 15; j >= 0; j--) {
        uint8_t nibble = (cr3 >> (j * 4)) & 0xF;
        hex[15 - j] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
    }
    hex[16] = '\0';
    vga_print(hex, VGA_COLOR_LIGHT_CYAN);
    vga_print("\n", VGA_COLOR_WHITE);
    
    vga_print("    Paging already enabled by bootloader\n", VGA_COLOR_WHITE);
    vga_print("    First 2 MB identity mapped\n", VGA_COLOR_WHITE);
    
    vga_print("[OK] Paging initialized!\n", VGA_COLOR_LIGHT_GREEN);
}

// Map virtual address to physical address
void paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    // Get indices for all levels
    uint64_t pml4_idx = pml4_index(virt);
    uint64_t pdpt_idx = pdpt_index(virt);
    uint64_t pd_idx = pd_index(virt);
    uint64_t pt_idx = pt_index(virt);
    
    // Get or create PDPT
    page_table_t* pdpt = get_or_create_table(&kernel_pml4->entries[pml4_idx], PAGE_USER);
    if (!pdpt) return;
    
    // Get or create PD
    page_table_t* pd = get_or_create_table(&pdpt->entries[pdpt_idx], PAGE_USER);
    if (!pd) return;
    
    // Get or create PT
    page_table_t* pt = get_or_create_table(&pd->entries[pd_idx], PAGE_USER);
    if (!pt) return;
    
    // Map page
    pt->entries[pt_idx] = (phys & ~0xFFF) | flags;
}

// Unmap a virtual address
void paging_unmap_page(uint64_t virt) {
    uint64_t pml4_idx = pml4_index(virt);
    uint64_t pdpt_idx = pdpt_index(virt);
    uint64_t pd_idx = pd_index(virt);
    uint64_t pt_idx = pt_index(virt);
    
    // Check if PML4 entry exists
    if (!(kernel_pml4->entries[pml4_idx] & PAGE_PRESENT)) return;
    page_table_t* pdpt = (page_table_t*)(kernel_pml4->entries[pml4_idx] & ~0xFFF);
    
    // Check if PDPT entry exists
    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT)) return;
    page_table_t* pd = (page_table_t*)(pdpt->entries[pdpt_idx] & ~0xFFF);
    
    // Check if PD entry exists
    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) return;
    page_table_t* pt = (page_table_t*)(pd->entries[pd_idx] & ~0xFFF);
    
    // Unmap page
    pt->entries[pt_idx] = 0;
    
    // Invalidate TLB entry
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

// Get physical address for virtual address
uint64_t paging_get_physical(uint64_t virt) {
    uint64_t pml4_idx = pml4_index(virt);
    uint64_t pdpt_idx = pdpt_index(virt);
    uint64_t pd_idx = pd_index(virt);
    uint64_t pt_idx = pt_index(virt);
    
    if (!(kernel_pml4->entries[pml4_idx] & PAGE_PRESENT)) return 0;
    page_table_t* pdpt = (page_table_t*)(kernel_pml4->entries[pml4_idx] & ~0xFFF);
    
    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT)) return 0;
    page_table_t* pd = (page_table_t*)(pdpt->entries[pdpt_idx] & ~0xFFF);
    
    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) return 0;
    page_table_t* pt = (page_table_t*)(pd->entries[pd_idx] & ~0xFFF);
    
    if (!(pt->entries[pt_idx] & PAGE_PRESENT)) return 0;
    
    return (pt->entries[pt_idx] & ~0xFFF) | (virt & 0xFFF);
}

// Switch to different page directory
void paging_switch_directory(page_table_t* pml4) {
    kernel_pml4 = pml4;
    __asm__ volatile("mov %0, %%cr3" :: "r"(pml4) : "memory");
}

// Get current page directory
page_table_t* paging_get_current_directory(void) {
    return kernel_pml4;
}

// Create new address space
page_table_t* paging_create_address_space(void) {
    page_table_t* pml4 = (page_table_t*)pmm_alloc_page();
    if (!pml4) return NULL;
    
    // Clear PML4
    for (int i = 0; i < 512; i++) {
        pml4->entries[i] = 0;
    }
    
    // Copy kernel mappings (top half)
    for (int i = 256; i < 512; i++) {
        pml4->entries[i] = kernel_pml4->entries[i];
    }
    
    return pml4;
}
