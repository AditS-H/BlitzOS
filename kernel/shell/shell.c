#include "shell.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mm/kheap.h"
#include "../mm/pmm.h"
#include "../proc/process.h"
#include "../fs/vfs/vfs.h"
#include "../sys/syscall.h"
#include "../core/panic.h"
#include "../arch/x86_64/interrupts.h"
#include "../../drivers/vga.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/pit.h"
#include "../../drivers/serial.h"
#include "../../drivers/mouse.h"
#include "../../drivers/pci/pci.h"
#include "../../drivers/video/framebuffer.h"
#include "../../drivers/video/bga.h"
#include "../arch/x86_64/sse.h"
#include "../arch/x86_64/tsc.h"
#include "../gui/gui.h"
#include "../gui/apps.h"
#include "../exec/elf.h"

// ---------------------------------------------------------------------------
// Shell state
// ---------------------------------------------------------------------------

static char    line[SHELL_LINE_MAX];
static char    history[SHELL_HISTORY_SIZE][SHELL_LINE_MAX];
static int     history_count = 0;
static int     history_pos   = 0;     // browse cursor, == history_count when at the live line
static int32_t cwd = VFS_ROOT_NODE;

// Where read_line() gets its keys. Defaults to the raw keyboard; the GUI
// terminal swaps in its own queue via shell_set_input_source().
static int (*input_getkey)(void) = NULL;

void shell_set_input_source(int (*getkey)(void))
{
    input_getkey = getkey;
}

static int shell_read_key(void)
{
    return input_getkey ? input_getkey() : keyboard_getkey();
}

typedef struct {
    const char* name;
    const char* usage;
    const char* help;
    void (*handler)(int argc, char** argv);
} command_t;

static const command_t commands[];   // forward declaration

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void print_size(uint64_t bytes)
{
    if (bytes >= 1024 * 1024) {
        kprintf("%lu.%lu MB", bytes / (1024 * 1024),
                ((bytes % (1024 * 1024)) * 10) / (1024 * 1024));
    } else if (bytes >= 1024) {
        kprintf("%lu.%lu KB", bytes / 1024, ((bytes % 1024) * 10) / 1024);
    } else {
        kprintf("%lu B", bytes);
    }
}

static void print_duration(uint64_t ticks)
{
    uint64_t total_seconds = ticks / 100;
    kprintf("%lud %luh %lum %lus",
            total_seconds / 86400,
            (total_seconds % 86400) / 3600,
            (total_seconds % 3600) / 60,
            total_seconds % 60);
}

// Joins argv[from..argc) with single spaces. Returns bytes written.
static uint32_t join_args(char* out, uint32_t size, int argc, char** argv, int from)
{
    uint32_t pos = 0;

    for (int i = from; i < argc; i++) {
        if (i > from && pos + 1 < size) {
            out[pos++] = ' ';
        }
        for (const char* p = argv[i]; *p && pos + 1 < size; p++) {
            out[pos++] = *p;
        }
    }

    out[pos] = '\0';
    return pos;
}

// ---------------------------------------------------------------------------
// Commands: system information
// ---------------------------------------------------------------------------

static void cmd_help(int argc, char** argv)
{
    if (argc > 1) {
        for (int i = 0; commands[i].name; i++) {
            if (strcasecmp_ascii(commands[i].name, argv[1]) == 0) {
                kprintf_color(VGA_COLOR_LIGHT_CYAN, "%s\n", commands[i].usage);
                kprintf("  %s\n", commands[i].help);
                return;
            }
        }
        kprintf_color(VGA_COLOR_LIGHT_RED, "No such command: %s\n", argv[1]);
        return;
    }

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\nBlitzOS shell commands\n");
    kprintf_color(VGA_COLOR_DARK_GREY,
                  "(type `help <command>` for details)\n\n");

    for (int i = 0; commands[i].name; i++) {
        kprintf_color(VGA_COLOR_LIGHT_CYAN, "  %-10s", commands[i].name);
        kprintf("%s\n", commands[i].help);
    }
    kprintf("\n");
}

static void cmd_ver(int argc, char** argv)
{
    (void)argc; (void)argv;

    const fb_stats_t* gfx = fb_get_stats();
    const cpu_features_t* cpu = cpu_get_features();

    kprintf_color(VGA_COLOR_LIGHT_CYAN, "\n  BlitzOS v0.6 \"Desktop\"\n");
    kprintf("  x86-64 long mode, monolithic kernel\n\n");
    kprintf("  Scheduler  : preemptive, priority-based with aging\n");
    kprintf("  Syscalls   : INT 0x80, 26 entry points\n");
    kprintf("  Filesystem : in-memory ramfs\n");
    kprintf("  Graphics   : %s\n",
            fb_is_active() ? "active" : "text mode (run `desktop`)");
    kprintf("  Blitter    : %s\n",
            cpu->has_sse2 && sse_is_enabled() ? "SSE2, state saved per switch"
                                              : "scalar");
    kprintf("  Page flip  : %s\n",
            gfx->hardware_flip ? "hardware" : "software / inactive");
    kprintf("  Programs   : ELF64 loader, ET_EXEC and ET_DYN\n");
    kprintf("  Serial     : %s\n\n",
            serial_is_available() ? "COM1 @ 38400 8N1" : "not detected");
}

static void cmd_uptime(int argc, char** argv)
{
    (void)argc; (void)argv;

    uint64_t ticks = scheduler_get_ticks();
    kprintf("Up ");
    print_duration(ticks);
    kprintf("  (%lu ticks, %lu ms)\n", ticks, pit_get_uptime_ms());
}

static void cmd_ps(int argc, char** argv)
{
    (void)argc; (void)argv;

    kprintf_color(VGA_COLOR_LIGHT_GREEN,
                  "  PID  PPID  PRI  STATE     CPU(ms)  NAME\n");

    process_t* current = get_current_process();

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = process_at_index(i);
        if (!p) {
            continue;
        }

        vga_color_t color = (p == current) ? VGA_COLOR_LIGHT_CYAN
                                           : VGA_COLOR_LIGHT_GREY;

        kprintf_color(color, "  %-4u %-5u %-4u %-9s %-8lu %s%s\n",
                      p->pid, p->parent_pid, p->base_priority,
                      process_state_name(p->state),
                      p->total_ticks * 10,
                      p->name,
                      (p == current) ? " *" : "");
    }

    scheduler_print_stats();
}

static void cmd_mem(int argc, char** argv)
{
    (void)argc; (void)argv;

    uint64_t total = pmm_get_total_memory();
    uint64_t used  = pmm_get_used_memory();
    uint64_t free  = pmm_get_free_memory();

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\nPhysical memory\n");
    kprintf("  Total : ");  print_size(total); kprintf("\n");
    kprintf("  Used  : ");  print_size(used);
    if (total) {
        kprintf("  (%lu%%)", (used * 100) / total);
    }
    kprintf("\n");
    kprintf("  Free  : ");  print_size(free);  kprintf("\n");

    size_t heap_total, heap_used, free_blocks, used_blocks;
    kheap_get_stats(&heap_total, &heap_used, &free_blocks, &used_blocks);

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\nKernel heap\n");
    kprintf("  Total : ");  print_size(heap_total); kprintf("\n");
    kprintf("  Used  : ");  print_size(heap_used);  kprintf("\n");
    kprintf("  Blocks: %lu in use, %lu free\n",
            (uint64_t)used_blocks, (uint64_t)free_blocks);

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\nramfs\n");
    kprintf("  Nodes : %u / %u\n", vfs_used_nodes(), (uint32_t)VFS_MAX_NODES);
    kprintf("  Data  : ");  print_size(vfs_total_bytes()); kprintf("\n\n");
}

static void cmd_irqstat(int argc, char** argv)
{
    (void)argc; (void)argv;

    static const char* const names[16] = {
        "timer", "keyboard", "cascade", "COM2", "COM1", "LPT2", "floppy",
        "LPT1", "RTC", "free", "free", "free", "PS/2 mouse", "FPU",
        "ATA primary", "ATA secondary"
    };

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\n IRQ  COUNT       SOURCE\n");
    for (uint8_t i = 0; i < 16; i++) {
        uint64_t count = interrupts_get_irq_count(i);
        if (count == 0) {
            continue;   // hide lines that never fire
        }
        kprintf("  %-3u  %-11lu %s\n", i, count, names[i]);
    }
    kprintf("  Spurious: %lu\n\n", interrupts_get_spurious_count());
}

// ---------------------------------------------------------------------------
// Commands: filesystem
// ---------------------------------------------------------------------------

static void cmd_ls(int argc, char** argv)
{
    int32_t dir = (argc > 1) ? vfs_resolve(argv[1], cwd) : cwd;

    if (dir < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "ls: %s: %s\n",
                      argc > 1 ? argv[1] : ".", vfs_error_string(dir));
        return;
    }

    vfs_node_t* node = vfs_get_node(dir);
    if (!node) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "ls: invalid node\n");
        return;
    }

    // Listing a file just prints that file's entry.
    if (node->type == VFS_NODE_FILE) {
        kprintf("  %-20s %lu bytes\n", node->name, (uint64_t)node->size);
        return;
    }

    uint32_t cursor = 0;
    uint32_t count  = 0;
    int32_t  child;

    while ((child = vfs_read_dir(dir, &cursor)) != VFS_INVALID) {
        vfs_node_t* entry = vfs_get_node(child);
        if (!entry) {
            continue;
        }

        if (entry->type == VFS_NODE_DIR) {
            kprintf_color(VGA_COLOR_LIGHT_BLUE, "  %-20s <DIR>\n", entry->name);
        } else {
            kprintf("  %-20s %lu bytes\n", entry->name, (uint64_t)entry->size);
        }
        count++;
    }

    if (count == 0) {
        kprintf_color(VGA_COLOR_DARK_GREY, "  (empty)\n");
    }
}

static void cmd_cd(int argc, char** argv)
{
    if (argc < 2) {
        cwd = VFS_ROOT_NODE;
        return;
    }

    int32_t target = vfs_resolve(argv[1], cwd);
    if (target < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "cd: %s: %s\n", argv[1],
                      vfs_error_string(target));
        return;
    }

    vfs_node_t* node = vfs_get_node(target);
    if (!node || node->type != VFS_NODE_DIR) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "cd: %s: not a directory\n", argv[1]);
        return;
    }

    cwd = target;
}

static void cmd_pwd(int argc, char** argv)
{
    (void)argc; (void)argv;

    char path[VFS_PATH_MAX];
    kprintf("%s\n", vfs_get_path(cwd, path, sizeof(path)));
}

static void cmd_cat(int argc, char** argv)
{
    if (argc < 2) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "usage: cat <file>\n");
        return;
    }

    int32_t node = vfs_resolve(argv[1], cwd);
    if (node < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "cat: %s: %s\n", argv[1],
                      vfs_error_string(node));
        return;
    }

    vfs_node_t* file = vfs_get_node(node);
    if (!file || file->type != VFS_NODE_FILE) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "cat: %s: is a directory\n", argv[1]);
        return;
    }

    // Stream through a small buffer rather than assuming the whole file fits
    // somewhere convenient.
    char     chunk[128];
    uint32_t offset = 0;
    int32_t  read;

    while ((read = vfs_read_node(node, offset, chunk, sizeof(chunk) - 1)) > 0) {
        chunk[read] = '\0';
        kprintf("%s", chunk);
        offset += (uint32_t)read;
    }

    if (file->size > 0 && file->data && file->data[file->size - 1] != '\n') {
        kprintf("\n");
    }
}

static void write_file(int argc, char** argv, int append)
{
    const char* verb = append ? "append" : "write";

    if (argc < 2) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "usage: %s <file> [text...]\n", verb);
        return;
    }

    int32_t node = vfs_resolve(argv[1], cwd);
    if (node < 0) {
        node = vfs_create(argv[1], cwd, VFS_NODE_FILE);
        if (node < 0) {
            kprintf_color(VGA_COLOR_LIGHT_RED, "%s: %s: %s\n", verb, argv[1],
                          vfs_error_string(node));
            return;
        }
    }

    vfs_node_t* file = vfs_get_node(node);
    if (!file || file->type != VFS_NODE_FILE) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "%s: %s: is a directory\n", verb,
                      argv[1]);
        return;
    }

    char     text[SHELL_LINE_MAX];
    uint32_t length = join_args(text, sizeof(text) - 1, argc, argv, 2);
    text[length++] = '\n';

    uint32_t offset = append ? file->size : 0;
    if (!append) {
        vfs_truncate(node);
        offset = 0;
    }

    int32_t written = vfs_write_node(node, offset, text, length);
    if (written < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "%s: %s\n", verb,
                      vfs_error_string(written));
        return;
    }

    kprintf_color(VGA_COLOR_DARK_GREY, "%d bytes written to %s\n", written,
                  argv[1]);
}

static void cmd_write(int argc, char** argv)  { write_file(argc, argv, 0); }
static void cmd_append(int argc, char** argv) { write_file(argc, argv, 1); }

static void cmd_touch(int argc, char** argv)
{
    if (argc < 2) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "usage: touch <file>\n");
        return;
    }

    if (vfs_resolve(argv[1], cwd) >= 0) {
        return;   // already exists, nothing to do
    }

    int32_t node = vfs_create(argv[1], cwd, VFS_NODE_FILE);
    if (node < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "touch: %s: %s\n", argv[1],
                      vfs_error_string(node));
    }
}

static void cmd_mkdir(int argc, char** argv)
{
    if (argc < 2) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "usage: mkdir <dir>\n");
        return;
    }

    int32_t node = vfs_create(argv[1], cwd, VFS_NODE_DIR);
    if (node < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "mkdir: %s: %s\n", argv[1],
                      vfs_error_string(node));
    }
}

static void cmd_rm(int argc, char** argv)
{
    if (argc < 2) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "usage: rm <path>\n");
        return;
    }

    int32_t target = vfs_resolve(argv[1], cwd);
    if (target == cwd) {
        kprintf_color(VGA_COLOR_LIGHT_RED,
                      "rm: cannot remove the current directory\n");
        return;
    }

    int32_t result = vfs_unlink(argv[1], cwd);
    if (result < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "rm: %s: %s\n", argv[1],
                      vfs_error_string(result));
    }
}

static void cmd_stat(int argc, char** argv)
{
    if (argc < 2) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "usage: stat <path>\n");
        return;
    }

    int32_t index = vfs_resolve(argv[1], cwd);
    if (index < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "stat: %s: %s\n", argv[1],
                      vfs_error_string(index));
        return;
    }

    vfs_node_t* node = vfs_get_node(index);
    char        path[VFS_PATH_MAX];

    kprintf("  Path     : %s\n", vfs_get_path(index, path, sizeof(path)));
    kprintf("  Type     : %s\n",
            node->type == VFS_NODE_DIR ? "directory" : "file");
    kprintf("  Size     : %lu bytes (%lu allocated)\n",
            (uint64_t)node->size, (uint64_t)node->capacity);
    kprintf("  Created  : tick %lu\n", node->created_tick);
    kprintf("  Modified : tick %lu\n", node->modified_tick);
}

// ---------------------------------------------------------------------------
// Commands: processes
// ---------------------------------------------------------------------------

// A demo worker. Prints a dot every 500 ms, twenty times, then exits - which
// exercises sleeping, preemption and reaping all at once.
static void demo_worker(void)
{
    uint32_t pid = sys_getpid();   // real INT 0x80 trap

    for (int i = 0; i < 20; i++) {
        kprintf_color(VGA_COLOR_LIGHT_MAGENTA, "[worker %u] tick %d\n", pid, i);
        sys_sleep_ms(500);
    }

    sys_exit(0);
}

static void cmd_spawn(int argc, char** argv)
{
    uint32_t priority = (argc > 1) ? (uint32_t)str_to_int(argv[1])
                                   : PRIORITY_LOW;

    process_t* worker = process_create("worker", demo_worker, priority);
    if (!worker) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "spawn: failed\n");
        return;
    }

    kprintf("Spawned worker as pid %u (priority %u). Watch it interleave with "
            "the shell.\n", worker->pid, priority);
}

static void cmd_kill(int argc, char** argv)
{
    if (argc < 2) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "usage: kill <pid>\n");
        return;
    }

    uint32_t pid = (uint32_t)str_to_int(argv[1]);
    if (process_kill_pid(pid)) {
        kprintf("Killed pid %u\n", pid);
    } else {
        kprintf_color(VGA_COLOR_LIGHT_RED,
                      "kill: no such process, or pid %u is protected\n", pid);
    }
}

static void cmd_sleep(int argc, char** argv)
{
    uint64_t ms = (argc > 1) ? (uint64_t)str_to_int(argv[1]) : 1000;

    kprintf("Sleeping %lu ms (the shell is off the run queue - try `ps` from a "
            "worker)...\n", ms);
    sys_sleep_ms((uint32_t)ms);
    kprintf("Awake.\n");
}

// ---------------------------------------------------------------------------
// Commands: console and diagnostics
// ---------------------------------------------------------------------------

static void cmd_clear(int argc, char** argv)
{
    (void)argc; (void)argv;
    vga_clear();
}

static void cmd_echo(int argc, char** argv)
{
    char text[SHELL_LINE_MAX];
    join_args(text, sizeof(text), argc, argv, 1);

    // Go through the write syscall rather than calling kprintf directly, so
    // `echo` is a genuine end-to-end test of INT 0x80.
    sys_write(STDOUT, text, (uint32_t)strlen(text));
    sys_write(STDOUT, "\n", 1);
}

static void cmd_color(int argc, char** argv)
{
    if (argc < 2) {
        for (int i = 0; i < 16; i++) {
            kprintf_color((vga_color_t)i, "  %2d: sample text\n", i);
        }
        return;
    }

    int value = (int)str_to_int(argv[1]);
    if (value < 0 || value > 15) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "color: pick 0-15\n");
        return;
    }

    syscall1(SYS_SET_COLOR, (uint64_t)value);
    kprintf("Default text colour is now %d.\n", value);
}

static void cmd_beep(int argc, char** argv)
{
    uint32_t frequency = (argc > 1) ? (uint32_t)str_to_int(argv[1]) : 880;
    uint32_t duration  = (argc > 2) ? (uint32_t)str_to_int(argv[2]) : 200;

    syscall2(SYS_BEEP, frequency, duration);
    kprintf("Beeped at %u Hz for %u ms (QEMU needs an audio backend to be "
            "audible).\n", frequency, duration);
}

static void cmd_rainbow(int argc, char** argv)
{
    char text[SHELL_LINE_MAX];
    join_args(text, sizeof(text), argc, argv, 1);

    if (text[0] == '\0') {
        strlcpy(text, "BlitzOS", sizeof(text));
    }

    syscall1(SYS_PRINT_RAINBOW, (uint64_t)text);
    kprintf("\n");
}

static void cmd_party(int argc, char** argv)
{
    uint32_t duration = (argc > 1) ? (uint32_t)str_to_int(argv[1]) : 2000;
    syscall1(SYS_PARTY_MODE, duration);
    kprintf("\n");
}

static void cmd_syscalls(int argc, char** argv)
{
    (void)argc; (void)argv;

    kprintf_color(VGA_COLOR_LIGHT_GREEN,
                  "\nExercising the INT 0x80 path\n");

    uint32_t pid  = sys_getpid();
    uint32_t ppid = sys_getppid();
    kprintf("  getpid()    -> %u\n", pid);
    kprintf("  getppid()   -> %u\n", ppid);
    kprintf("  ticks()     -> %lu\n", sys_ticks());
    kprintf("  proc_count()-> %lu\n", syscall0(SYS_PROC_COUNT));

    const char* message = "  write()     -> this line came back through fd 1\n";
    sys_write(STDOUT, message, (uint32_t)strlen(message));

    // Round-trip a file through open/write/seek/read/close.
    int64_t fd = (int64_t)syscall2(SYS_OPEN, (uint64_t)"/tmp/syscall_test",
                                   VFS_O_READ | VFS_O_WRITE | VFS_O_CREATE |
                                   VFS_O_TRUNC);
    if (fd < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "  open() failed\n");
        return;
    }

    const char* payload = "written via syscall";
    sys_write((int32_t)fd, payload, (uint32_t)strlen(payload));
    syscall3(SYS_SEEK, (uint64_t)fd, 0, 0);   // rewind

    char buffer[64];
    int64_t read = sys_read((int32_t)fd, buffer, sizeof(buffer) - 1);
    if (read > 0) {
        buffer[read] = '\0';
        kprintf("  open/write/seek/read -> \"%s\"\n", buffer);
    }

    syscall1(SYS_CLOSE, (uint64_t)fd);
    kprintf_color(VGA_COLOR_LIGHT_GREEN,
                  "  All syscalls returned. INT 0x80 is live.\n\n");
}

// These live at file scope and are volatile so the optimiser cannot fold them.
// With a local `volatile int zero = 0`, GCC at -O2 still proves the division
// and the NULL store are undefined and replaces both with a UD2 instruction -
// so `crash null` reported "Invalid Opcode" instead of the page fault it was
// supposed to demonstrate.
static volatile int       crash_zero;
static volatile uintptr_t crash_null_address;

static void cmd_crash(int argc, char** argv)
{
    const char* kind = (argc > 1) ? argv[1] : "help";

    if (strcmp(kind, "div0") == 0) {
        kprintf("Triggering a divide-by-zero (expect vector 0, #DE)...\n");
        volatile int boom = 1 / crash_zero;
        (void)boom;
    } else if (strcmp(kind, "null") == 0) {
        kprintf("Dereferencing NULL (expect vector 14, #PF at CR2=0)...\n");
        volatile uint64_t* bad = (volatile uint64_t*)crash_null_address;
        *bad = 1;
    } else if (strcmp(kind, "ud") == 0) {
        kprintf("Executing an invalid opcode...\n");
        __asm__ volatile("ud2");
    } else if (strcmp(kind, "assert") == 0) {
        KASSERT(1 == 2);
    } else if (strcmp(kind, "bp") == 0) {
        kprintf("Breakpoint (recoverable - the kernel keeps running):\n");
        __asm__ volatile("int3");
    } else {
        kprintf("usage: crash <div0|null|ud|assert|bp>\n");
        kprintf("  Deliberately faults so you can see the panic screen and\n");
        kprintf("  register dump. `bp` is the only recoverable one.\n");
    }
}

static void cmd_history(int argc, char** argv)
{
    (void)argc; (void)argv;

    for (int i = 0; i < history_count; i++) {
        kprintf("  %2d  %s\n", i + 1, history[i]);
    }
}

static void cmd_reboot(int argc, char** argv)
{
    (void)argc; (void)argv;

    kprintf_color(VGA_COLOR_LIGHT_BROWN, "Rebooting...\n");
    process_sleep_ms(300);

    // Pulse the 8042 keyboard controller's reset line. This is the classic
    // way to reboot a PC without ACPI.
    disable_interrupts();
    for (uint32_t i = 0; i < 100000; i++) {
        if (!(inb(KB_STATUS_PORT) & KB_STATUS_INPUT_FULL)) {
            break;
        }
    }
    outb(KB_COMMAND_PORT, 0xFE);

    // If that did not take, fall back to a triple fault by loading a null IDT.
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; }
        null_idt = { 0, 0 };
    __asm__ volatile("lidt %0; int3" :: "m"(null_idt));

    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void cmd_halt(int argc, char** argv)
{
    (void)argc; (void)argv;

    kprintf_color(VGA_COLOR_LIGHT_BROWN, "System halted. Close QEMU to exit.\n");
    disable_interrupts();
    for (;;) {
        __asm__ volatile("hlt");
    }
}


// ---------------------------------------------------------------------------
// Commands: graphics and hardware
// ---------------------------------------------------------------------------

static void cmd_desktop(int argc, char** argv)
{
    uint32_t width  = (argc > 1) ? (uint32_t)str_to_int(argv[1]) : 1024;
    uint32_t height = (argc > 2) ? (uint32_t)str_to_int(argv[2]) : 768;

    if (gui_is_running()) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "desktop: already running\n");
        return;
    }

    kprintf("Starting desktop at %ux%u. ESC inside the desktop returns here.\n",
            width, height);
    process_sleep_ms(300);

    if (!gui_start(width, height)) {
        kprintf_color(VGA_COLOR_LIGHT_RED,
                      "desktop: could not initialise graphics; still in text mode\n");
        return;
    }

    // From here the compositor owns the screen and the keyboard. Read our keys
    // from the terminal window instead, so both can coexist.
    shell_set_input_source(terminal_getkey);
}

static void cmd_lspci(int argc, char** argv)
{
    (void)argc; (void)argv;

    kprintf_color(VGA_COLOR_LIGHT_GREEN,
                  "\n BUS:DEV.FN  VENDOR:DEVICE  CLASS\n");

    for (uint32_t i = 0; i < pci_device_count(); i++) {
        const pci_device_t* dev = pci_device_at(i);
        if (!dev) continue;

        kprintf("  %02x:%02x.%u    %04x:%04x      %s (%s)\n",
                dev->bus, dev->device, dev->function,
                dev->vendor_id, dev->device_id,
                pci_class_name(dev->class_code, dev->subclass),
                pci_vendor_name(dev->vendor_id));

        for (int b = 0; b < 6; b++) {
            if (dev->bar[b] == 0) continue;
            kprintf_color(VGA_COLOR_DARK_GREY, "               BAR%d %s 0x%lx\n",
                          b, dev->bar_is_io[b] ? "io " : "mem", dev->bar[b]);
        }
    }
    kprintf("\n");
}

static void cmd_cpuinfo(int argc, char** argv)
{
    (void)argc; (void)argv;

    const cpu_features_t* cpu = cpu_get_features();

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\nCPU\n");
    kprintf("  Vendor    : %s\n", cpu->vendor);
    if (cpu->brand[0]) {
        kprintf("  Model     : %s\n", cpu->brand);
    }
    kprintf("  Features  : %s%s%s%s%s%s%s\n",
            cpu->has_sse   ? "sse "   : "",
            cpu->has_sse2  ? "sse2 "  : "",
            cpu->has_sse3  ? "sse3 "  : "",
            cpu->has_ssse3 ? "ssse3 " : "",
            cpu->has_sse41 ? "sse4.1 ": "",
            cpu->has_sse42 ? "sse4.2 ": "",
            cpu->has_avx   ? "avx"    : "");
    kprintf("  FXSAVE    : %s\n", cpu->has_fxsr ? "yes" : "no");
    kprintf("  SSE state : %s\n",
            sse_is_enabled() ? "enabled, saved on every context switch"
                             : "disabled");
    kprintf("  TSC       : %lu MHz%s\n", tsc_cycles_per_us(),
            cpu->has_invariant_tsc ? ", invariant" : "");
    kprintf("  Timer     : %u Hz\n\n", pit_get_frequency());
}

static void cmd_gfxstat(int argc, char** argv)
{
    (void)argc; (void)argv;

    const bga_info_t* gpu = bga_get_info();

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\nDisplay adapter\n");
    if (!gpu->present) {
        kprintf("  No BGA-compatible adapter detected.\n\n");
        return;
    }

    kprintf("  BGA version : 0x%x\n", gpu->version);
    kprintf("  Framebuffer : 0x%lx\n", gpu->framebuffer_phys);
    kprintf("  VRAM        : %lu MB\n", gpu->vram_bytes / (1024 * 1024));
    kprintf("  Mode        : %ux%u at %u bpp\n", gpu->width, gpu->height, gpu->bpp);
    kprintf("  Virtual     : %u rows (%s)\n", gpu->virtual_height,
            gpu->virtual_height >= gpu->height * 2 ? "double buffered"
                                                   : "single buffered");

    if (!fb_is_active()) {
        kprintf("\n  Framebuffer inactive - run `desktop` first.\n\n");
        return;
    }

    const fb_stats_t* stats = fb_get_stats();

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\nFrame pipeline\n");
    kprintf("  Frames      : %lu\n", stats->frames_presented);
    kprintf("  Present     : %u us average, %u min, %u max\n",
            stats->average_frame_us, stats->min_frame_us, stats->max_frame_us);
    kprintf("  Page flip   : %s\n",
            stats->hardware_flip ? "hardware (one register write)"
                                 : "software copy");
    kprintf("  Blitter     : %s\n",
            stats->sse_blitter ? "SSE2, 16 bytes per instruction"
                               : "scalar, 8 bytes per instruction");

    if (stats->total_pixels_possible) {
        uint64_t saved = 100 - (stats->total_pixels_copied * 100 /
                                stats->total_pixels_possible);
        kprintf("  Dirty rects : %lu%% of full-screen blits avoided\n", saved);
        kprintf("  Pixels      : %lu copied of %lu possible\n",
                stats->total_pixels_copied, stats->total_pixels_possible);
    }
    kprintf("\n");
}

static void cmd_gamemode(int argc, char** argv)
{
    if (!gui_is_running()) {
        kprintf_color(VGA_COLOR_LIGHT_RED,
                      "gamemode: only meaningful with the desktop running\n");
        return;
    }

    int enable = (argc > 1) ? (strcmp(argv[1], "off") != 0) : !gui_get_game_mode();
    gui_set_game_mode(enable);
}

// ---------------------------------------------------------------------------
// Commands: running compiled programs
// ---------------------------------------------------------------------------

static void cmd_run(int argc, char** argv)
{
    if (argc < 2) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "usage: run <path> [priority]\n");
        kprintf_color(VGA_COLOR_DARK_GREY,
                      "  Programs shipped in the ISO appear under /bin.\n");
        return;
    }

    uint32_t priority = (argc > 2) ? (uint32_t)str_to_int(argv[2])
                                   : DEFAULT_PRIORITY;

    int32_t result = elf_exec(argv[1], priority);

    if (result < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "run: %s: %s\n", argv[1],
                      elf_error_string(result));
        return;
    }

    kprintf_color(VGA_COLOR_DARK_GREY,
                  "Started %s as pid %d.\n", argv[1], result);
}

static void cmd_readelf(int argc, char** argv)
{
    if (argc < 2) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "usage: readelf <path>\n");
        return;
    }

    int32_t node = vfs_resolve(argv[1], cwd);
    if (node < 0) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "readelf: %s: %s\n", argv[1],
                      vfs_error_string(node));
        return;
    }

    vfs_node_t* file = vfs_get_node(node);
    if (!file || file->type != VFS_NODE_FILE) {
        kprintf_color(VGA_COLOR_LIGHT_RED, "readelf: %s is not a file\n", argv[1]);
        return;
    }

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\n%s (%lu bytes)\n",
                  argv[1], (uint64_t)file->size);
    elf_describe(file->data, file->size);
    kprintf("\n");
}

// ---------------------------------------------------------------------------
// Command table
// ---------------------------------------------------------------------------

static const command_t commands[] = {
    { "help",    "help [command]",              "list commands, or explain one",      cmd_help },
    { "ver",     "ver",                         "kernel version and features",        cmd_ver },
    { "clear",   "clear",                       "clear the screen",                   cmd_clear },
    { "echo",    "echo <text...>",              "print text (via the write syscall)", cmd_echo },
    { "ps",      "ps",                          "list processes and scheduler stats", cmd_ps },
    { "mem",     "mem",                         "physical, heap and ramfs usage",     cmd_mem },
    { "uptime",  "uptime",                      "how long the kernel has been up",    cmd_uptime },
    { "irqstat", "irqstat",                     "per-line interrupt counters",        cmd_irqstat },
    { "ls",      "ls [path]",                   "list a directory",                   cmd_ls },
    { "cd",      "cd [path]",                   "change the working directory",       cmd_cd },
    { "pwd",     "pwd",                         "print the working directory",        cmd_pwd },
    { "cat",     "cat <file>",                  "print a file",                       cmd_cat },
    { "write",   "write <file> <text...>",      "overwrite a file with text",         cmd_write },
    { "append",  "append <file> <text...>",     "append a line to a file",            cmd_append },
    { "touch",   "touch <file>",                "create an empty file",               cmd_touch },
    { "mkdir",   "mkdir <dir>",                 "create a directory",                 cmd_mkdir },
    { "rm",      "rm <path>",                   "delete a file or empty directory",   cmd_rm },
    { "stat",    "stat <path>",                 "show file metadata",                 cmd_stat },
    { "spawn",   "spawn [priority]",            "start a demo worker process",        cmd_spawn },
    { "kill",    "kill <pid>",                  "terminate a process",                cmd_kill },
    { "sleep",   "sleep <ms>",                  "block the shell for a while",        cmd_sleep },
    { "syscalls","syscalls",                    "exercise every INT 0x80 entry point",cmd_syscalls },
    { "color",   "color [0-15]",                "set or preview text colours",        cmd_color },
    { "beep",    "beep [hz] [ms]",              "PC speaker tone",                    cmd_beep },
    { "rainbow", "rainbow <text...>",           "print text in rainbow colours",      cmd_rainbow },
    { "party",   "party [ms]",                  "confetti, via a syscall",            cmd_party },
    { "run",     "run <path> [priority]",       "load and run a compiled program",    cmd_run },
    { "readelf", "readelf <path>",              "inspect an ELF file without running", cmd_readelf },
    { "desktop", "desktop [w] [h]",             "start the graphical desktop",        cmd_desktop },
    { "gamemode","gamemode [on|off]",           "low-latency mode for the desktop",   cmd_gamemode },
    { "gfxstat", "gfxstat",                     "GPU and frame pipeline statistics",  cmd_gfxstat },
    { "lspci",   "lspci",                       "list PCI devices and their BARs",    cmd_lspci },
    { "cpuinfo", "cpuinfo",                     "CPU features, SSE and timer state",  cmd_cpuinfo },
    { "crash",   "crash <div0|null|ud|assert|bp>","fault on purpose to see the panic",cmd_crash },
    { "history", "history",                     "recent commands",                    cmd_history },
    { "reboot",  "reboot",                      "restart the machine",                cmd_reboot },
    { "halt",    "halt",                        "stop the CPU",                       cmd_halt },
    { NULL, NULL, NULL, NULL }
};

// ---------------------------------------------------------------------------
// Line editing
// ---------------------------------------------------------------------------

static void shell_prompt(void)
{
    char path[VFS_PATH_MAX];
    vfs_get_path(cwd, path, sizeof(path));

    kprintf_color(VGA_COLOR_LIGHT_GREEN, "blitz");
    kprintf_color(VGA_COLOR_DARK_GREY, ":");
    kprintf_color(VGA_COLOR_LIGHT_BLUE, "%s", path);
    kprintf_color(VGA_COLOR_LIGHT_GREEN, "$ ");
}

// Erase the current input from the screen and replace it with `replacement`.
static void replace_line(uint32_t* length, const char* replacement)
{
    while (*length > 0) {
        kprintf("\b");
        (*length)--;
    }

    strlcpy(line, replacement, SHELL_LINE_MAX);
    *length = (uint32_t)strlen(line);
    kprintf("%s", line);
}

static void history_add(const char* text)
{
    if (text[0] == '\0') {
        return;
    }
    // Skip consecutive duplicates - they just push useful entries out.
    if (history_count > 0 && strcmp(history[history_count - 1], text) == 0) {
        return;
    }

    if (history_count == SHELL_HISTORY_SIZE) {
        for (int i = 1; i < SHELL_HISTORY_SIZE; i++) {
            strlcpy(history[i - 1], history[i], SHELL_LINE_MAX);
        }
        history_count--;
    }

    strlcpy(history[history_count++], text, SHELL_LINE_MAX);
}

// Reads one line. Blocks on the keyboard wait channel, so the shell uses no
// CPU at all while it waits.
static void read_line(void)
{
    uint32_t length = 0;
    line[0] = '\0';
    history_pos = history_count;

    for (;;) {
        int key = shell_read_key();

        if (key == '\n') {
            kprintf("\n");
            line[length] = '\0';
            return;
        }

        if (key == '\b') {
            if (length > 0) {
                length--;
                line[length] = '\0';
                kprintf("\b");
            }
            continue;
        }

        if (key == KEY_ARROW_UP) {
            if (history_pos > 0) {
                history_pos--;
                replace_line(&length, history[history_pos]);
            }
            continue;
        }

        if (key == KEY_ARROW_DOWN) {
            if (history_pos < history_count - 1) {
                history_pos++;
                replace_line(&length, history[history_pos]);
            } else {
                history_pos = history_count;
                replace_line(&length, "");
            }
            continue;
        }

        if (key == 3) {          // Ctrl+C: abandon the line
            kprintf("^C\n");
            line[0] = '\0';
            return;
        }

        if (key == 12) {         // Ctrl+L: clear and redraw
            vga_clear();
            shell_prompt();
            kprintf("%s", line);
            continue;
        }

        // Ignore anything that is not a printable character.
        if (key < 32 || key > 126) {
            continue;
        }

        if (length + 1 >= SHELL_LINE_MAX) {
            continue;   // line full
        }

        line[length++] = (char)key;
        line[length]   = '\0';
        kprintf("%c", (char)key);
    }
}

// Splits `line` in place into argv. Returns argc.
static int tokenize(char** argv)
{
    int   argc = 0;
    char* p    = line;

    while (*p && argc < SHELL_MAX_ARGS) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }

        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
    }

    return argc;
}

static void execute(int argc, char** argv)
{
    if (argc == 0) {
        return;
    }

    for (int i = 0; commands[i].name; i++) {
        if (strcasecmp_ascii(commands[i].name, argv[0]) == 0) {
            commands[i].handler(argc, argv);
            return;
        }
    }

    kprintf_color(VGA_COLOR_LIGHT_RED, "Unknown command: %s\n", argv[0]);
    kprintf_color(VGA_COLOR_DARK_GREY, "Type `help` for the command list.\n");
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void shell_print_banner(void)
{
    kprintf_color(VGA_COLOR_LIGHT_CYAN,
        "\n"
        "  ####  #    # ###  ##### #####  ####   ####\n"
        "  #  #  #    #  #     #     #   #    # #    #\n"
        "  ####  #    #  #     #     #   #    #  ####\n"
        "  #  #  #    #  #     #     #   #    #      #\n"
        "  ####  ######  #     #   #####  ####   ####\n");
    kprintf_color(VGA_COLOR_DARK_GREY,
        "     preemptive  .  x86-64  .  desktop  .  v0.6\n\n");
    kprintf("Type ");
    kprintf_color(VGA_COLOR_LIGHT_CYAN, "help");
    kprintf(" for commands. Highlights:\n");
    kprintf_color(VGA_COLOR_LIGHT_CYAN, "  desktop");
    kprintf("   graphical session       ");
    kprintf_color(VGA_COLOR_LIGHT_CYAN, "run /bin/demo");
    kprintf("  a real C++ program\n");
    kprintf_color(VGA_COLOR_LIGHT_CYAN, "  syscalls");
    kprintf("  exercise INT 0x80       ");
    kprintf_color(VGA_COLOR_LIGHT_CYAN, "gfxstat");
    kprintf("       GPU and frame stats\n\n");
}

void shell_main(void)
{
    char* argv[SHELL_MAX_ARGS];

    shell_print_banner();

    for (;;) {
        shell_prompt();
        read_line();

        if (line[0] == '\0') {
            continue;
        }

        history_add(line);

        // tokenize() rewrites `line` in place, so history_add() must come
        // first or the stored entry would be truncated at the first space.
        int argc = tokenize(argv);
        execute(argc, argv);
    }
}
