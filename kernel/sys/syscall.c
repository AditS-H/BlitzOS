#include "syscall.h"
#include "../../drivers/vga.h"
#include "../../drivers/pit.h"
#include "../proc/process.h"
#include "../arch/x86_64/idt.h"

// Helper: Convert integer to string
static void itoa(uint32_t value, char* buffer) {
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    
    int i = 0;
    while (value > 0) {
        int remainder = value % 10;
        buffer[i++] = '0' + remainder;
        value /= 10;
    }
    buffer[i] = '\0';
    
    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = temp;
        start++;
        end--;
    }
}

// I/O port functions
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Syscall entry point (assembly stub)
extern void syscall_asm(void);

// Initialize the syscall handler
void syscall_init(void) {
    // INT 0x80 is the traditional Linux syscall vector
    // 0xEE = trap gate (allows user mode to call it)
    idt_set_gate(0x80, (uint64_t)syscall_asm, 0x08, 0xEE);
    vga_print("[SYSCALL] System call interface initialized (INT 0x80)\n", VGA_COLOR_LIGHT_GREEN);
}

// ============================================================
// FUN SYSCALL IMPLEMENTATIONS
// ============================================================

// Beep: Generate a tone via PIT (Intel 8253)
static void syscall_beep(uint32_t frequency, uint32_t duration_ticks) {
    if (frequency == 0 || duration_ticks == 0) return;
    
    // PIT port for speaker control
    #define PIT_SPEAKER_PORT 0x61
    #define PIT_COUNTER_2    0x42
    #define PIT_CONTROL      0x43
    
    // Calculate divisor for desired frequency
    // PIT runs at 1.193182 MHz
    uint16_t divisor = 1193182 / frequency;
    
    // Program counter 2 of PIT (speaker timer)
    outb(PIT_CONTROL, 0xB6);  // Counter 2, both bytes, square wave
    outb(PIT_COUNTER_2, divisor & 0xFF);
    outb(PIT_COUNTER_2, (divisor >> 8) & 0xFF);
    
    // Enable speaker
    uint8_t status = inb(PIT_SPEAKER_PORT);
    outb(PIT_SPEAKER_PORT, status | 0x03);
    
    // Sleep for duration
    pit_sleep(duration_ticks);
    
    // Disable speaker
    status = inb(PIT_SPEAKER_PORT);
    outb(PIT_SPEAKER_PORT, status & ~0x03);
}

// Print in rainbow colors
static void syscall_print_rainbow(const char* text) {
    if (!text) return;
    
    static const vga_color_t colors[] = {
        VGA_COLOR_RED,
        VGA_COLOR_LIGHT_RED,
        VGA_COLOR_LIGHT_BROWN,
        VGA_COLOR_LIGHT_GREEN,
        VGA_COLOR_CYAN,
        VGA_COLOR_LIGHT_BLUE,
        VGA_COLOR_LIGHT_MAGENTA
    };
    static const int num_colors = 7;
    
    for (int i = 0; text[i]; i++) {
        vga_putchar(text[i], colors[i % num_colors]);
    }
    vga_putchar('\n', VGA_COLOR_WHITE);
}

// Print cool ASCII style
static void syscall_print_cool(const char* text) {
    if (!text) return;
    
    vga_print("  ===== COOL TEXT =====  \n", VGA_COLOR_LIGHT_CYAN);
    vga_print("  ", VGA_COLOR_LIGHT_CYAN);
    vga_print(text, VGA_COLOR_LIGHT_BROWN);
    vga_print(" \n", VGA_COLOR_LIGHT_CYAN);
    vga_print("  =====================  \n", VGA_COLOR_LIGHT_CYAN);
}

// Screen blink effect
static void syscall_screen_blink(uint32_t count, uint32_t speed_ticks) {
    for (uint32_t i = 0; i < count; i++) {
        vga_putchar('*', VGA_COLOR_WHITE);
        pit_sleep(speed_ticks);
    }
    vga_putchar('\n', VGA_COLOR_WHITE);
}

// Party mode: random colors!
static void syscall_party_mode(uint32_t duration_ticks) {
    static const vga_color_t party_colors[] = {
        VGA_COLOR_RED,
        VGA_COLOR_LIGHT_RED,
        VGA_COLOR_GREEN,
        VGA_COLOR_LIGHT_GREEN,
        VGA_COLOR_BLUE,
        VGA_COLOR_LIGHT_BLUE,
        VGA_COLOR_LIGHT_BROWN,
        VGA_COLOR_LIGHT_MAGENTA,
        VGA_COLOR_LIGHT_CYAN
    };
    
    vga_print("PARTY TIME!  \n", VGA_COLOR_LIGHT_BROWN);
    
    uint32_t end_time = pit_get_ticks() + duration_ticks;
    
    while (pit_get_ticks() < end_time) {
        static uint32_t seed = 12345;
        seed = seed * 1103515245 + 12345;  // Simple LCG
        
        vga_color_t color = party_colors[(seed >> 16) % 9];
        vga_putchar('*', color);
        
        pit_sleep(1);  // Sleep 1 tick at a time
    }
    
    vga_print("\nParty over!\n", VGA_COLOR_WHITE);
}

// Cursor dance effect
static void syscall_cursor_dance(uint32_t duration_ticks) {
    uint32_t end_time = pit_get_ticks() + duration_ticks;
    
    const char dance_chars[] = "|/-\\";
    int idx = 0;
    
    vga_print("Dancing: ", VGA_COLOR_LIGHT_GREEN);
    
    while (pit_get_ticks() < end_time) {
        char c = dance_chars[idx % 4];
        vga_putchar(c, VGA_COLOR_LIGHT_CYAN);
        idx++;
        pit_sleep(1);
    }
    
    vga_print("\nDance complete!\n", VGA_COLOR_LIGHT_GREEN);
}

// ============================================================
// CORE SYSCALL IMPLEMENTATIONS
// ============================================================

// Exit the current process
static void syscall_exit(int code) {
    (void)code;  // Suppress unused warning
    process_t* current = get_current_process();
    if (current) {
        current->state = PROCESS_TERMINATED;
    }
    __asm__ volatile("hlt");
}

// Write to output (stdout/stderr)
static int syscall_write(int fd, const char* buf, int len) {
    if (!buf || len <= 0) return -1;
    
    if (fd == STDOUT || fd == STDERR) {
        for (int i = 0; i < len && buf[i]; i++) {
            vga_putchar(buf[i], VGA_COLOR_WHITE);
        }
        return len;
    }
    
    return -1;  // Invalid file descriptor
}

// Read from input (not yet implemented)
static int syscall_read(int fd, char* buf, int len) {
    (void)fd;   // Suppress unused warning
    (void)buf;  // Suppress unused warning
    (void)len;  // Suppress unused warning
    return -1;
}

// Sleep for ticks (not milliseconds!)
static void syscall_sleep(uint32_t ticks) {
    pit_sleep(ticks);
}

// Get current process ID
static uint32_t syscall_getpid(void) {
    process_t* current = get_current_process();
    return current ? current->pid : 0;
}

// Get parent process ID
static uint32_t syscall_getppid(void) {
    process_t* current = get_current_process();
    return current ? current->parent_pid : 0;
}

// ============================================================
// MAIN SYSCALL DISPATCHER
// ============================================================

uint64_t syscall_handler(uint64_t rax, uint64_t rbx, uint64_t rcx, 
                         uint64_t rdx, uint64_t rsi, uint64_t rdi) {
    (void)rsi;  // Suppress unused warning
    (void)rdi;  // Suppress unused warning
    uint32_t syscall_num = rax & 0xFFFFFFFF;
    
    switch (syscall_num) {
        // Core syscalls
        case SYS_EXIT:
            syscall_exit((int)rbx);
            return 0;
            
        case SYS_WRITE:
            return syscall_write((int)rbx, (const char*)rcx, (int)rdx);
            
        case SYS_READ:
            return syscall_read((int)rbx, (char*)rcx, (int)rdx);
            
        case SYS_SLEEP:
            syscall_sleep((uint32_t)rbx);
            return 0;
            
        case SYS_GETPID:
            return syscall_getpid();
            
        case SYS_GETPPID:
            return syscall_getppid();
            
        // Fun syscalls!
        case SYS_BEEP:
            syscall_beep((uint32_t)rbx, (uint32_t)rcx);
            return 0;
            
        case SYS_PRINT_RAINBOW:
            syscall_print_rainbow((const char*)rbx);
            return 0;
            
        case SYS_SCREEN_BLINK:
            syscall_screen_blink((uint32_t)rbx, (uint32_t)rcx);
            return 0;
            
        case SYS_PARTY_MODE:
            syscall_party_mode((uint32_t)rbx);
            return 0;
            
        case SYS_PRINT_COOL:
            syscall_print_cool((const char*)rbx);
            return 0;
            
        case SYS_CURSOR_DANCE:
            syscall_cursor_dance((uint32_t)rbx);
            return 0;
            
        default:
            vga_print("[SYSCALL] Unknown: ", VGA_COLOR_LIGHT_RED);
            char buf[10];
            itoa(syscall_num, buf);
            vga_print(buf, VGA_COLOR_LIGHT_RED);
            vga_print("\n", VGA_COLOR_WHITE);
            return -1;
    }
}
