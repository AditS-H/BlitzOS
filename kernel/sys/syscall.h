#ifndef KERNEL_SYS_SYSCALL_H
#define KERNEL_SYS_SYSCALL_H

#include <stdint.h>

// System call numbers (for future implementation)
#define SYS_EXIT     0
#define SYS_WRITE    1
#define SYS_READ     2
#define SYS_YIELD    3
#define SYS_SLEEP    4
#define SYS_GETPID   5

// System call handler type
typedef uint64_t (*syscall_handler_t)(uint64_t arg1, uint64_t arg2, uint64_t arg3);

// System call functions (stubs for now)
static inline void syscall_init(void) {
    // TODO: Initialize system call interface
    // Will set up interrupt 0x80 or SYSCALL/SYSRET
}

static inline uint64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    // TODO: Dispatch system calls to appropriate handlers
    (void)syscall_num;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    return 0;
}

#endif // KERNEL_SYS_SYSCALL_H
