// Test kernel entry point - demonstrates syscall usage
// This shows how syscalls would be called from user space

#include "../kernel/proc/process.h"
#include "../drivers/vga.h"

// Inline syscall invocation macro
// Usage: SYSCALL(syscall_num, arg1, arg2, arg3)
#define SYSCALL(num, a, b, c) \
    __asm__ volatile( \
        "mov %0, %%rax\n\t" \
        "mov %1, %%rbx\n\t" \
        "mov %2, %%rcx\n\t" \
        "mov %3, %%rdx\n\t" \
        "int $0x80\n\t" \
        : \
        : "i"(num), "g"(a), "g"(b), "g"(c) \
        : "rax", "rbx", "rcx", "rdx" \
    )

// Test process 1 - Uses fun syscalls
void test_syscall_fun(void) {
    vga_print("\n[TEST-FUN] Starting fun syscall tests!\n", VGA_COLOR_LIGHT_GREEN);
    
    // Test: Print rainbow text
    // Syscall: SYS_PRINT_RAINBOW = 101
    vga_print("Testing PRINT_RAINBOW...\n", VGA_COLOR_WHITE);
    SYSCALL(101, (uint64_t)"RAINBOW!", 0, 0);
    
    // Small delay
    for (volatile uint64_t i = 0; i < 100000000; i++);
    
    // Test: Print cool text
    vga_print("Testing PRINT_COOL...\n", VGA_COLOR_WHITE);
    SYSCALL(104, (uint64_t)"COOL TEXT", 0, 0);
    
    vga_print("[TEST-FUN] Complete!\n", VGA_COLOR_LIGHT_GREEN);
    __asm__ volatile("hlt");
}

// Test process 2 - Uses core syscalls
void test_syscall_core(void) {
    vga_print("\n[TEST-CORE] Testing core syscalls!\n", VGA_COLOR_LIGHT_CYAN);
    
    // Test: Get PID
    // Syscall: SYS_GETPID = 4
    vga_print("Getting PID via syscall...\n", VGA_COLOR_WHITE);
    uint64_t pid;
    __asm__ volatile(
        "mov $4, %%rax\n\t"
        "int $0x80\n\t"
        "mov %%rax, %0\n\t"
        : "=r"(pid)
        :
        : "rax"
    );
    
    vga_print("PID from syscall: ", VGA_COLOR_LIGHT_CYAN);
    vga_print_int(pid, VGA_COLOR_LIGHT_CYAN);
    vga_print("\n", VGA_COLOR_WHITE);
    
    // Test: Get PPID
    vga_print("Getting PPID via syscall...\n", VGA_COLOR_WHITE);
    uint64_t ppid;
    __asm__ volatile(
        "mov $5, %%rax\n\t"
        "int $0x80\n\t"
        "mov %%rax, %0\n\t"
        : "=r"(ppid)
        :
        : "rax"
    );
    
    vga_print("PPID from syscall: ", VGA_COLOR_LIGHT_CYAN);
    vga_print_int(ppid, VGA_COLOR_LIGHT_CYAN);
    vga_print("\n", VGA_COLOR_WHITE);
    
    vga_print("[TEST-CORE] Complete!\n", VGA_COLOR_LIGHT_GREEN);
    __asm__ volatile("hlt");
}

// Test process 3 - Uses party mode!
void test_syscall_party(void) {
    vga_print("\n[TEST-PARTY] Let's party with syscalls!\n", VGA_COLOR_LIGHT_MAGENTA);
    
    // Syscall: SYS_PARTY_MODE = 103
    // Duration = 50 ticks (about 500ms at 100Hz)
    vga_print("Invoking PARTY_MODE for 50 ticks...\n", VGA_COLOR_WHITE);
    __asm__ volatile(
        "mov $103, %%rax\n\t"
        "mov $50, %%rbx\n\t"
        "int $0x80\n\t"
        :
        :
        : "rax", "rbx"
    );
    
    vga_print("[TEST-PARTY] Woo! Syscalls work!\n", VGA_COLOR_LIGHT_GREEN);
    __asm__ volatile("hlt");
}
