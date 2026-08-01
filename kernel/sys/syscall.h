#ifndef KERNEL_SYS_SYSCALL_H
#define KERNEL_SYS_SYSCALL_H

#include <stdint.h>
#include "../arch/x86_64/regs.h"

// ============================================================================
// BlitzOS system call interface
//
// Invoked with INT 0x80:
//   RAX = syscall number
//   RBX, RCX, RDX, RSI, RDI = arguments (5 maximum)
//   RAX = return value
//
// The IDT gate for vector 0x80 is installed by interrupts_init() with DPL 3,
// so this entry point already works from ring 3 once user mode lands.
// ============================================================================

// -------- Process control --------
#define SYS_EXIT          0    // void   exit(int code)
#define SYS_WRITE         1    // int    write(int fd, const void* buf, int len)
#define SYS_READ          2    // int    read(int fd, void* buf, int len)
#define SYS_SLEEP         3    // void   sleep(uint32_t milliseconds)
#define SYS_GETPID        4    // uint32 getpid(void)
#define SYS_GETPPID       5    // uint32 getppid(void)
#define SYS_FORK          6    // reserved: needs per-process address spaces
#define SYS_EXEC          7    // reserved: needs an ELF loader
#define SYS_YIELD         8    // void   yield(void)
#define SYS_GETTICKS      9    // uint64 ticks(void)
#define SYS_KILL         10    // int    kill(uint32 pid)
#define SYS_UPTIME_MS    11    // uint64 uptime_ms(void)
#define SYS_PROC_COUNT   12    // uint32 process_count(void)

// -------- Filesystem --------
#define SYS_OPEN         20    // int open(const char* path, uint32 flags)
#define SYS_CLOSE        21    // int close(int fd)
#define SYS_SEEK         22    // int seek(int fd, int64 offset, int whence)
#define SYS_MKDIR        23    // int mkdir(const char* path)
#define SYS_UNLINK       24    // int unlink(const char* path)
#define SYS_STAT         25    // int stat(const char* path, stat_t* out)

// -------- Console / fun --------
#define SYS_BEEP         100   // void beep(uint32 frequency_hz, uint32 duration_ms)
#define SYS_PRINT_RAINBOW 101  // void print_rainbow(const char* text)
#define SYS_SCREEN_BLINK 102   // void screen_blink(uint32 count, uint32 speed_ms)
#define SYS_PARTY_MODE   103   // void party_mode(uint32 duration_ms)
#define SYS_PRINT_COOL   104   // void print_cool(const char* text)
#define SYS_CURSOR_DANCE 105   // void cursor_dance(uint32 duration_ms)
#define SYS_SET_COLOR    106   // void set_color(uint32 vga_color)
#define SYS_CLEAR        107   // void clear_screen(void)

// Standard file descriptors
#define STDIN  0
#define STDOUT 1
#define STDERR 2

// Returned by SYS_STAT.
typedef struct {
    uint32_t type;      // 1 = file, 2 = directory
    uint32_t size;      // bytes
    uint64_t created;   // tick when created
    uint64_t modified;  // tick of last write
} stat_t;

// Every syscall returns this on failure.
#define SYSCALL_ERROR ((uint64_t)-1)

void syscall_init(void);

// Called from syscall_stub in isr.asm. Reads the arguments out of the saved
// register frame and writes the result back into the frame's RAX slot.
void syscall_dispatch(registers_t* regs);

// ---------------------------------------------------------------------------
// Caller-side wrappers
//
// These are what make a syscall an actual syscall: they trap through INT 0x80
// rather than calling the kernel function directly. Kernel threads use them
// today; ring 3 code will use the identical sequence tomorrow.
// ---------------------------------------------------------------------------

static inline uint64_t syscall0(uint64_t number)
{
    uint64_t result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number) : "memory");
    return result;
}

static inline uint64_t syscall1(uint64_t number, uint64_t arg1)
{
    uint64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(arg1)
                     : "memory");
    return result;
}

static inline uint64_t syscall2(uint64_t number, uint64_t arg1, uint64_t arg2)
{
    uint64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(arg1), "c"(arg2)
                     : "memory");
    return result;
}

static inline uint64_t syscall3(uint64_t number, uint64_t arg1, uint64_t arg2,
                                uint64_t arg3)
{
    uint64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3)
                     : "memory");
    return result;
}

// Convenience helpers built on the raw traps.
static inline void     sys_exit(int32_t code)      { syscall1(SYS_EXIT, (uint64_t)(int64_t)code); }
static inline uint32_t sys_getpid(void)            { return (uint32_t)syscall0(SYS_GETPID); }
static inline uint32_t sys_getppid(void)           { return (uint32_t)syscall0(SYS_GETPPID); }
static inline void     sys_yield(void)             { syscall0(SYS_YIELD); }
static inline void     sys_sleep_ms(uint32_t ms)   { syscall1(SYS_SLEEP, ms); }
static inline uint64_t sys_ticks(void)             { return syscall0(SYS_GETTICKS); }

static inline int64_t sys_write(int32_t fd, const void* buf, uint32_t len)
{
    return (int64_t)syscall3(SYS_WRITE, (uint64_t)(int64_t)fd, (uint64_t)buf, len);
}

static inline int64_t sys_read(int32_t fd, void* buf, uint32_t len)
{
    return (int64_t)syscall3(SYS_READ, (uint64_t)(int64_t)fd, (uint64_t)buf, len);
}

#endif // KERNEL_SYS_SYSCALL_H
