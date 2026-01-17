// syscall.c - System call implementation
// BlitzOS Syscall Handler

#include "syscall.h"
#include "../../drivers/vga.h"
#include "../../drivers/pit.h"
#include "../proc/process.h"

// ============================================================
// SYSCALL IMPLEMENTATION
// ============================================================

// Rainbow colors for fun syscalls
static const uint8_t rainbow_colors[] = {
    VGA_COLOR_LIGHT_RED, VGA_COLOR_BROWN, VGA_COLOR_LIGHT_GREEN,
    VGA_COLOR_LIGHT_CYAN, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_LIGHT_MAGENTA
};
#define RAINBOW_COUNT 6

// Simple random number (using timer ticks)
static uint32_t simple_rand(void) {
    static uint32_t seed = 12345;
    seed = seed * 1103515245 + 12345;
    return (seed / 65536) % 32768;
}

// ============================================================
// CORE SYSCALLS
// ============================================================

// SYS_EXIT - Terminate current process
static uint64_t sys_exit(uint64_t code) {
    process_t* current = get_current_process();
    if (current) {
        vga_print("\n[EXIT] Process ", VGA_COLOR_LIGHT_RED);
        vga_print(current->name, VGA_COLOR_LIGHT_RED);
        vga_print(" exited with code ", VGA_COLOR_LIGHT_RED);
        vga_print_int(code, VGA_COLOR_LIGHT_RED);
        vga_print("\n", VGA_COLOR_WHITE);
        // Mark as terminated (scheduler will clean up)
        current->state = PROCESS_TERMINATED;
    }
    // Yield to next process
    do_schedule();
    return 0;
}

// SYS_WRITE - Write to file descriptor (stdout only for now)
static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    if (fd != STDOUT && fd != STDERR) {
        return (uint64_t)-1;  // Invalid fd
    }
    
    const char* str = (const char*)buf;
    for (uint64_t i = 0; i < len && str[i]; i++) {
        char c[2] = {str[i], '\0'};
        vga_print(c, fd == STDERR ? VGA_COLOR_LIGHT_RED : VGA_COLOR_WHITE);
    }
    return len;
}

// SYS_SLEEP - Sleep for milliseconds
static uint64_t sys_sleep(uint64_t milliseconds) {
    uint32_t ticks = (milliseconds + 9) / 10;  // Convert to 10ms ticks
    pit_sleep(ticks);
    return 0;
}

// SYS_GETPID - Get current process ID
static uint64_t sys_getpid(void) {
    process_t* current = get_current_process();
    return current ? current->pid : 0;
}

// SYS_GETPPID - Get parent process ID
static uint64_t sys_getppid(void) {
    process_t* current = get_current_process();
    return current ? current->parent_pid : 0;
}

// ============================================================
// FUN SYSCALLS 🎉
// ============================================================

// SYS_PRINT_RAINBOW - Print text in rainbow colors
static uint64_t sys_print_rainbow(uint64_t buf) {
    const char* str = (const char*)buf;
    int color_idx = 0;
    
    while (*str) {
        char c[2] = {*str, '\0'};
        vga_print(c, rainbow_colors[color_idx % RAINBOW_COUNT]);
        color_idx++;
        str++;
    }
    return 0;
}

// SYS_PARTY_MODE - Random colors for duration
static uint64_t sys_party_mode(uint64_t duration_ms) {
    uint32_t end_tick = pit_get_ticks() + (duration_ms / 10);
    const char* party_chars = "*!@#$%&";
    
    while (pit_get_ticks() < end_tick) {
        uint32_t r = simple_rand();
        uint8_t color = rainbow_colors[r % RAINBOW_COUNT];
        char c[2] = {party_chars[r % 7], '\0'};
        vga_print(c, color);
        
        for(volatile int i = 0; i < 50000; i++);
    }
    return 0;
}

// SYS_PRINT_COOL - Print text with cool effect
static uint64_t sys_print_cool(uint64_t buf) {
    const char* str = (const char*)buf;
    
    vga_print(">> ", VGA_COLOR_LIGHT_CYAN);
    while (*str) {
        char c[2] = {*str, '\0'};
        vga_print(c, VGA_COLOR_LIGHT_GREEN);
        for(volatile int i = 0; i < 30000; i++);  // Typing effect
        str++;
    }
    vga_print(" <<", VGA_COLOR_LIGHT_CYAN);
    return 0;
}

// SYS_SCREEN_BLINK - Blink effect (changes colors)
static uint64_t sys_screen_blink(uint64_t count, uint64_t speed_ms) {
    for (uint64_t i = 0; i < count; i++) {
        vga_print("*", VGA_COLOR_WHITE);
        pit_sleep(speed_ms / 10);
        vga_print("\b ", VGA_COLOR_BLACK);  // Backspace effect
        pit_sleep(speed_ms / 10);
    }
    return 0;
}

// ============================================================
// SYSCALL DISPATCHER
// ============================================================

void syscall_init(void) {
    vga_print("[SYSCALL] System calls initialized (INT 0x80)\n", VGA_COLOR_LIGHT_GREEN);
}

uint64_t syscall_handler(uint64_t rax, uint64_t rbx, uint64_t rcx,
                         uint64_t rdx, uint64_t rsi, uint64_t rdi) {
    (void)rsi;
    (void)rdi;
    
    switch (rax) {
        // Core syscalls
        case SYS_EXIT:
            return sys_exit(rbx);
        case SYS_WRITE:
            return sys_write(rbx, rcx, rdx);
        case SYS_SLEEP:
            return sys_sleep(rbx);
        case SYS_GETPID:
            return sys_getpid();
        case SYS_GETPPID:
            return sys_getppid();
        
        // Fun syscalls 🎉
        case SYS_PRINT_RAINBOW:
            return sys_print_rainbow(rbx);
        case SYS_PARTY_MODE:
            return sys_party_mode(rbx);
        case SYS_PRINT_COOL:
            return sys_print_cool(rbx);
        case SYS_SCREEN_BLINK:
            return sys_screen_blink(rbx, rcx);
        
        default:
            vga_print("[SYSCALL] Unknown syscall: ", VGA_COLOR_LIGHT_RED);
            vga_print_int(rax, VGA_COLOR_LIGHT_RED);
            vga_print("\n", VGA_COLOR_WHITE);
            return (uint64_t)-1;
    }
}
