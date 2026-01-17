#ifndef KERNEL_SYS_SYSCALL_H
#define KERNEL_SYS_SYSCALL_H

#include <stdint.h>

// ============================================================
// SYSCALL NUMBER DEFINITIONS
// ============================================================
// Syscalls are invoked via INT 0x80
// RAX = syscall number
// RBX, RCX, RDX, RSI, RDI = arguments (5 max)
// RAX = return value
// ============================================================

// Core syscalls
#define SYS_EXIT          0    // void exit(int code)
#define SYS_WRITE         1    // int write(int fd, const char* buf, int len)
#define SYS_READ          2    // int read(int fd, char* buf, int len)
#define SYS_SLEEP         3    // void sleep(uint32_t milliseconds)
#define SYS_GETPID        4    // uint32_t getpid(void)
#define SYS_GETPPID       5    // uint32_t getppid(void)
#define SYS_FORK          6    // int fork(void)  [not yet implemented]
#define SYS_EXEC          7    // int exec(const char* path, char** argv) [future]

// Fun syscalls 🎉
#define SYS_BEEP          100  // void beep(uint32_t frequency, uint32_t duration_ms)
#define SYS_PRINT_RAINBOW 101  // void print_rainbow(const char* text)
#define SYS_SCREEN_BLINK  102  // void screen_blink(uint32_t count, uint32_t speed_ms)
#define SYS_PARTY_MODE    103  // void party_mode(uint32_t duration_ms) - random colors!
#define SYS_PRINT_COOL    104  // void print_cool(const char* text) - ASCII art style
#define SYS_CURSOR_DANCE  105  // void cursor_dance(uint32_t duration_ms) - moves around

// File descriptor constants
#define STDOUT 1  // Standard output
#define STDERR 2  // Standard error
#define STDIN  0  // Standard input

// Initialize the syscall handler
void syscall_init(void);

// Main syscall dispatcher (called from INT 0x80 handler)
// Returns the value to be placed in RAX
uint64_t syscall_handler(uint64_t rax, uint64_t rbx, uint64_t rcx, 
                         uint64_t rdx, uint64_t rsi, uint64_t rdi);

#endif // KERNEL_SYS_SYSCALL_H
