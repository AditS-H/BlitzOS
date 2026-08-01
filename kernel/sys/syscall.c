// syscall.c - System call implementation.
//
// Reached via INT 0x80 -> syscall_stub (isr.asm) -> syscall_dispatch().
//
// Until this commit none of this ran: syscall_init() printed a banner but no
// IDT gate was ever installed for vector 0x80, so an `int $0x80` hit a zeroed
// descriptor and triple-faulted. The gate is now installed in
// interrupts_init(), and every entry point below is live.

#include "syscall.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../proc/process.h"
#include "../fs/vfs/vfs.h"
#include "../core/panic.h"
#include "../arch/x86_64/interrupts.h"
#include "../../drivers/vga.h"
#include "../../drivers/pit.h"
#include "../../drivers/keyboard.h"

// Longest buffer a single read/write syscall may touch. Keeps a bogus length
// from walking off the end of memory and taking the machine down.
#define SYSCALL_MAX_IO 65536

static uint64_t syscall_count = 0;

// ---------------------------------------------------------------------------
// Argument validation
//
// Everything currently runs in ring 0 sharing one address space, so we cannot
// do a real user/kernel split yet. What we CAN do is reject the mistakes that
// actually happen: NULL, near-NULL, and absurd lengths. When per-process page
// tables land, this is the single function that grows a proper page-table walk.
// ---------------------------------------------------------------------------

static int validate_buffer(uint64_t address, uint64_t length)
{
    if (address == 0 || address < 0x1000) {
        return 0;   // NULL or the guard page
    }
    if (length > SYSCALL_MAX_IO) {
        return 0;
    }
    if (address + length < address) {
        return 0;   // 64-bit overflow
    }
    return 1;
}

// Validates a NUL-terminated string argument and returns its length, or -1.
static int64_t validate_string(uint64_t address, uint64_t max_length)
{
    if (address == 0 || address < 0x1000) {
        return -1;
    }

    const char* str = (const char*)address;
    for (uint64_t i = 0; i < max_length; i++) {
        if (str[i] == '\0') {
            return (int64_t)i;
        }
    }
    return -1;  // no terminator within the limit
}

// ---------------------------------------------------------------------------
// Process control
// ---------------------------------------------------------------------------

static uint64_t sys_do_exit(uint64_t code)
{
    process_exit((int32_t)code);   // noreturn
}

static uint64_t sys_do_write(uint64_t fd, uint64_t buf, uint64_t len)
{
    if (!validate_buffer(buf, len)) {
        return SYSCALL_ERROR;
    }

    const char* data = (const char*)buf;

    // Console descriptors go to the screen (and the serial log via kprintf).
    if (fd == STDOUT || fd == STDERR) {
        vga_color_t color = (fd == STDERR) ? VGA_COLOR_LIGHT_RED
                                           : vga_get_default_color();
        for (uint64_t i = 0; i < len; i++) {
            kprintf_color(color, "%c", data[i]);
        }
        return len;
    }

    if (fd == STDIN) {
        return SYSCALL_ERROR;
    }

    int32_t written = vfs_write((int32_t)fd, data, (uint32_t)len);
    return written < 0 ? SYSCALL_ERROR : (uint64_t)written;
}

static uint64_t sys_do_read(uint64_t fd, uint64_t buf, uint64_t len)
{
    if (!validate_buffer(buf, len) || len == 0) {
        return SYSCALL_ERROR;
    }

    // Reading stdin blocks on the keyboard until Enter, which is what a shell
    // or any line-oriented program expects.
    if (fd == STDIN) {
        char*    out   = (char*)buf;
        uint64_t count = 0;

        while (count < len - 1) {
            char c = keyboard_getchar();
            if (c == '\n') {
                break;
            }
            if (c == '\b') {
                if (count > 0) count--;
                continue;
            }
            out[count++] = c;
        }
        out[count] = '\0';
        return count;
    }

    if (fd == STDOUT || fd == STDERR) {
        return SYSCALL_ERROR;
    }

    int32_t read = vfs_read((int32_t)fd, (void*)buf, (uint32_t)len);
    return read < 0 ? SYSCALL_ERROR : (uint64_t)read;
}

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------

static uint64_t sys_do_open(uint64_t path, uint64_t flags)
{
    if (validate_string(path, VFS_PATH_MAX) < 0) {
        return SYSCALL_ERROR;
    }

    int32_t fd = vfs_open((const char*)path, VFS_ROOT_NODE, (uint32_t)flags);
    return fd < 0 ? SYSCALL_ERROR : (uint64_t)fd;
}

static uint64_t sys_do_stat(uint64_t path, uint64_t out)
{
    if (validate_string(path, VFS_PATH_MAX) < 0) {
        return SYSCALL_ERROR;
    }
    if (!validate_buffer(out, sizeof(stat_t))) {
        return SYSCALL_ERROR;
    }

    int32_t index = vfs_resolve((const char*)path, VFS_ROOT_NODE);
    if (index < 0) {
        return SYSCALL_ERROR;
    }

    vfs_node_t* node = vfs_get_node(index);
    if (!node) {
        return SYSCALL_ERROR;
    }

    stat_t* result   = (stat_t*)out;
    result->type     = (uint32_t)node->type;
    result->size     = node->size;
    result->created  = node->created_tick;
    result->modified = node->modified_tick;

    return 0;
}

// ---------------------------------------------------------------------------
// Console effects
// ---------------------------------------------------------------------------

static const vga_color_t RAINBOW[] = {
    VGA_COLOR_LIGHT_RED, VGA_COLOR_LIGHT_BROWN, VGA_COLOR_LIGHT_GREEN,
    VGA_COLOR_LIGHT_CYAN, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_LIGHT_MAGENTA
};
#define RAINBOW_COUNT 6

// Linear congruential generator, seeded from the timer so successive runs of
// party mode do not produce an identical pattern.
static uint32_t simple_rand(void)
{
    static uint32_t seed = 0;
    if (seed == 0) {
        seed = (uint32_t)pit_get_ticks() | 1;
    }
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7FFF;
}

static uint64_t sys_do_print_rainbow(uint64_t buf)
{
    if (validate_string(buf, 4096) < 0) {
        return SYSCALL_ERROR;
    }

    const char* str = (const char*)buf;
    for (int i = 0; str[i]; i++) {
        kprintf_color(RAINBOW[i % RAINBOW_COUNT], "%c", str[i]);
    }
    return 0;
}

static uint64_t sys_do_print_cool(uint64_t buf)
{
    if (validate_string(buf, 4096) < 0) {
        return SYSCALL_ERROR;
    }

    const char* str = (const char*)buf;
    kprintf_color(VGA_COLOR_LIGHT_CYAN, ">> ");

    for (int i = 0; str[i]; i++) {
        kprintf_color(VGA_COLOR_LIGHT_GREEN, "%c", str[i]);
        // A typewriter effect that sleeps instead of spinning, so the rest of
        // the system keeps running while it types.
        process_sleep_ms(20);
    }

    kprintf_color(VGA_COLOR_LIGHT_CYAN, " <<\n");
    return 0;
}

static uint64_t sys_do_party_mode(uint64_t duration_ms)
{
    if (duration_ms > 10000) {
        duration_ms = 10000;   // no runaway parties
    }

    uint64_t deadline = pit_get_ticks() + (duration_ms / MS_PER_TICK);
    const char* glyphs = "*!@#$%&";

    while (pit_get_ticks() < deadline) {
        uint32_t r = simple_rand();
        kprintf_color(RAINBOW[r % RAINBOW_COUNT], "%c", glyphs[r % 7]);
        process_sleep_ms(30);
    }
    return 0;
}

static uint64_t sys_do_screen_blink(uint64_t count, uint64_t speed_ms)
{
    if (count > 100)      count = 100;
    if (speed_ms < 10)    speed_ms = 10;
    if (speed_ms > 2000)  speed_ms = 2000;

    for (uint64_t i = 0; i < count; i++) {
        kprintf_color(VGA_COLOR_WHITE, "*");
        process_sleep_ms(speed_ms);
        kprintf("\b");
        process_sleep_ms(speed_ms);
    }
    return 0;
}

static uint64_t sys_do_cursor_dance(uint64_t duration_ms)
{
    if (duration_ms > 10000) {
        duration_ms = 10000;
    }

    uint8_t saved_x, saved_y;
    vga_get_cursor(&saved_x, &saved_y);

    uint64_t deadline = pit_get_ticks() + (duration_ms / MS_PER_TICK);
    while (pit_get_ticks() < deadline) {
        uint8_t x = (uint8_t)(simple_rand() % VGA_WIDTH);
        uint8_t y = (uint8_t)(simple_rand() % VGA_HEIGHT);
        vga_set_cursor(x, y);
        process_sleep_ms(60);
    }

    vga_set_cursor(saved_x, saved_y);
    return 0;
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

void syscall_init(void)
{
    syscall_count = 0;
    kok("System calls live on INT 0x80 (%u implemented)\n", 26u);
}

uint64_t syscall_get_count(void)
{
    return syscall_count;
}

void syscall_dispatch(registers_t* regs)
{
    syscall_count++;

    uint64_t number = regs->rax;
    uint64_t a1     = regs->rbx;
    uint64_t a2     = regs->rcx;
    uint64_t a3     = regs->rdx;

    // Interrupt gates arrive with IF clear. Most syscalls want to sleep or
    // print, both of which need a running timer, so re-enable interrupts for
    // the duration of the call.
    enable_interrupts();

    uint64_t result;

    switch (number) {
    // ---- Process control ----
    case SYS_EXIT:        result = sys_do_exit(a1); break;
    case SYS_WRITE:       result = sys_do_write(a1, a2, a3); break;
    case SYS_READ:        result = sys_do_read(a1, a2, a3); break;

    case SYS_SLEEP:
        process_sleep_ms(a1);
        result = 0;
        break;

    case SYS_GETPID: {
        process_t* current = get_current_process();
        result = current ? current->pid : 0;
        break;
    }

    case SYS_GETPPID: {
        process_t* current = get_current_process();
        result = current ? current->parent_pid : 0;
        break;
    }

    case SYS_YIELD:
        process_yield();
        result = 0;
        break;

    case SYS_GETTICKS:    result = scheduler_get_ticks(); break;
    case SYS_UPTIME_MS:   result = pit_get_uptime_ms(); break;
    case SYS_PROC_COUNT:  result = scheduler_get_process_count(); break;

    case SYS_KILL:
        result = process_kill_pid((uint32_t)a1) ? 0 : SYSCALL_ERROR;
        break;

    case SYS_FORK:
    case SYS_EXEC:
        // Both need per-process address spaces (fork) and an ELF loader
        // (exec). Reported as unsupported rather than silently returning 0,
        // which would look like "you are the child".
        kwarn("syscall %lu (fork/exec) is not implemented yet\n", number);
        result = SYSCALL_ERROR;
        break;

    // ---- Filesystem ----
    case SYS_OPEN:  result = sys_do_open(a1, a2); break;

    case SYS_CLOSE:
        result = vfs_close((int32_t)a1) < 0 ? SYSCALL_ERROR : 0;
        break;

    case SYS_SEEK: {
        int32_t position = vfs_seek((int32_t)a1, (int64_t)a2, (int)a3);
        result = position < 0 ? SYSCALL_ERROR : (uint64_t)position;
        break;
    }

    case SYS_MKDIR:
        if (validate_string(a1, VFS_PATH_MAX) < 0) {
            result = SYSCALL_ERROR;
        } else {
            int32_t node = vfs_create((const char*)a1, VFS_ROOT_NODE, VFS_NODE_DIR);
            result = node < 0 ? SYSCALL_ERROR : 0;
        }
        break;

    case SYS_UNLINK:
        if (validate_string(a1, VFS_PATH_MAX) < 0) {
            result = SYSCALL_ERROR;
        } else {
            result = vfs_unlink((const char*)a1, VFS_ROOT_NODE) < 0
                         ? SYSCALL_ERROR : 0;
        }
        break;

    case SYS_STAT: result = sys_do_stat(a1, a2); break;

    // ---- Console ----
    case SYS_BEEP:
        speaker_beep((uint32_t)a1, (uint32_t)a2);
        result = 0;
        break;

    case SYS_PRINT_RAINBOW: result = sys_do_print_rainbow(a1); break;
    case SYS_PRINT_COOL:    result = sys_do_print_cool(a1); break;
    case SYS_PARTY_MODE:    result = sys_do_party_mode(a1); break;
    case SYS_SCREEN_BLINK:  result = sys_do_screen_blink(a1, a2); break;
    case SYS_CURSOR_DANCE:  result = sys_do_cursor_dance(a1); break;

    case SYS_SET_COLOR:
        vga_set_default_color((vga_color_t)(a1 & 0x0F));
        result = 0;
        break;

    case SYS_CLEAR:
        vga_clear();
        result = 0;
        break;

    default:
        kerror("Unknown syscall %lu from pid %u\n", number,
               get_current_process() ? get_current_process()->pid : 0);
        result = SYSCALL_ERROR;
        break;
    }

    // Hand the result back through the saved frame; POP_ALL in the stub loads
    // it into RAX just before IRETQ.
    regs->rax = result;
}
