// blitz.h - The BlitzOS user-space API.
//
// This is the entire "libc" available to a program running on BlitzOS. There is
// no standard library: programs are built -nostdlib -ffreestanding, so printf,
// malloc and friends do not exist. What does exist is the syscall interface,
// reached the same way it always has been - INT 0x80 with the number in RAX.
//
// USING IT
// --------
//   #include <blitz.h>
//
//   void _start(void) {
//       print("hello\n");
//       exit(0);
//   }
//
// The entry point must be called _start, because there is no C runtime to call
// main() for you. Whatever the linker records as the entry address is where the
// kernel jumps.
//
// BUILDING
// --------
// See userland/Makefile. The important flags:
//
//   -ffreestanding     no assumptions about a hosted environment
//   -nostdlib          do not link a C library or CRT startup files
//   -static-pie        produce a position-independent executable, so the loader
//                      can place it anywhere instead of one fixed address
//   -fno-exceptions    C++ only: exceptions need unwind tables and a runtime
//   -fno-rtti          C++ only: RTTI needs type_info objects from libstdc++
//
// The same header works from C and C++.

#ifndef BLITZ_H
#define BLITZ_H

#ifdef __cplusplus
extern "C" {
#endif

// Use the compiler's own definitions rather than hand-rolled ones.
//
// This matters more than it looks. Writing `typedef unsigned long long size_t`
// gives you a 64-bit type, but not *the* size_t the compiler uses - on LP64
// that is `unsigned long`. C++ then refuses to match the sized deallocation
// operator, and the link fails with:
//
//     undefined reference to `operator delete(void*, unsigned long)'
//
// even though a `operator delete(void*, size_t)` is right there. These builtin
// macros are always defined, freestanding or not, and always agree with the
// compiler.
typedef __UINT8_TYPE__  uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INT32_TYPE__  int32_t;
typedef __INT64_TYPE__  int64_t;
typedef __SIZE_TYPE__   size_t;

// ---------------------------------------------------------------------------
// Syscall numbers. Must match kernel/sys/syscall.h.
// ---------------------------------------------------------------------------

#define SYS_EXIT          0
#define SYS_WRITE         1
#define SYS_READ          2
#define SYS_SLEEP         3
#define SYS_GETPID        4
#define SYS_GETPPID       5
#define SYS_YIELD         8
#define SYS_GETTICKS      9
#define SYS_UPTIME_MS    11
#define SYS_PROC_COUNT   12

#define SYS_OPEN         20
#define SYS_CLOSE        21
#define SYS_SEEK         22
#define SYS_MKDIR        23
#define SYS_UNLINK       24

#define SYS_BEEP         100
#define SYS_PRINT_RAINBOW 101
#define SYS_PARTY_MODE   103
#define SYS_SET_COLOR    106
#define SYS_CLEAR        107

#define STDIN  0
#define STDOUT 1
#define STDERR 2

// Open flags
#define O_READ   0x01
#define O_WRITE  0x02
#define O_CREATE 0x04
#define O_APPEND 0x08
#define O_TRUNC  0x10

// ---------------------------------------------------------------------------
// Raw syscall entry
//
// The kernel expects the number in RAX and arguments in RBX, RCX, RDX. Note
// this is NOT the System V calling convention - it is the convention the
// BlitzOS syscall dispatcher defines, which is why these have to be written in
// assembly rather than as ordinary function calls.
// ---------------------------------------------------------------------------

static inline uint64_t syscall0(uint64_t number)
{
    uint64_t result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number) : "memory");
    return result;
}

static inline uint64_t syscall1(uint64_t number, uint64_t a)
{
    uint64_t result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "b"(a) : "memory");
    return result;
}

static inline uint64_t syscall2(uint64_t number, uint64_t a, uint64_t b)
{
    uint64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result) : "a"(number), "b"(a), "c"(b) : "memory");
    return result;
}

static inline uint64_t syscall3(uint64_t number, uint64_t a, uint64_t b,
                                uint64_t c)
{
    uint64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return result;
}

// ---------------------------------------------------------------------------
// Friendly wrappers
// ---------------------------------------------------------------------------

static inline size_t strlen(const char* s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline void exit(int code)
{
    syscall1(SYS_EXIT, (uint64_t)(int64_t)code);
    for (;;) { }   // exit never returns, but the compiler does not know that
}

static inline int64_t write(int fd, const void* buf, size_t len)
{
    return (int64_t)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, len);
}

static inline int64_t read(int fd, void* buf, size_t len)
{
    return (int64_t)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, len);
}

static inline void print(const char* s)
{
    write(STDOUT, s, strlen(s));
}

static inline uint32_t getpid(void)      { return (uint32_t)syscall0(SYS_GETPID); }
static inline uint32_t getppid(void)     { return (uint32_t)syscall0(SYS_GETPPID); }
static inline void     yield(void)       { syscall0(SYS_YIELD); }
static inline void     sleep_ms(uint32_t ms) { syscall1(SYS_SLEEP, ms); }
static inline uint64_t ticks(void)       { return syscall0(SYS_GETTICKS); }
static inline uint64_t uptime_ms(void)   { return syscall0(SYS_UPTIME_MS); }
static inline uint32_t proc_count(void)  { return (uint32_t)syscall0(SYS_PROC_COUNT); }

static inline int  open(const char* path, uint32_t flags)
{
    return (int)syscall2(SYS_OPEN, (uint64_t)path, flags);
}
static inline int  close(int fd)          { return (int)syscall1(SYS_CLOSE, (uint64_t)fd); }
static inline int  mkdir(const char* p)   { return (int)syscall1(SYS_MKDIR, (uint64_t)p); }
static inline int  unlink(const char* p)  { return (int)syscall1(SYS_UNLINK, (uint64_t)p); }

static inline void beep(uint32_t hz, uint32_t ms) { syscall2(SYS_BEEP, hz, ms); }
static inline void rainbow(const char* s)         { syscall1(SYS_PRINT_RAINBOW, (uint64_t)s); }
static inline void set_color(uint32_t c)          { syscall1(SYS_SET_COLOR, c); }
static inline void clear_screen(void)             { syscall0(SYS_CLEAR); }

// ---------------------------------------------------------------------------
// Minimal formatting
//
// No printf, so programs that want to show a number need this. Converts an
// unsigned 64-bit value into `buf` and returns it, for use with print().
// ---------------------------------------------------------------------------

static inline char* utoa(uint64_t value, char* buf, uint32_t base)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[65];
    int  len = 0;

    if (value == 0) {
        tmp[len++] = '0';
    }
    while (value) {
        tmp[len++] = digits[value % base];
        value /= base;
    }

    for (int i = 0; i < len; i++) {
        buf[i] = tmp[len - 1 - i];
    }
    buf[len] = '\0';
    return buf;
}

static inline void print_uint(uint64_t value)
{
    char buf[24];
    print(utoa(value, buf, 10));
}

#ifdef __cplusplus
}
#endif

#endif // BLITZ_H
