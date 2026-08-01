#ifndef KERNEL_PROC_PROCESS_H
#define KERNEL_PROC_PROCESS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES       64
#define PROCESS_STACK_SIZE  16384   // 16 KB kernel stack per process
#define PROCESS_NAME_MAX    32

// Priorities run 0 (background) .. 255 (critical). Higher wins.
#define PRIORITY_IDLE       0
#define PRIORITY_LOW        64
#define DEFAULT_PRIORITY    128
#define PRIORITY_HIGH       192
#define PRIORITY_CRITICAL   240

// Base scheduling quantum in timer ticks (10 ms each at 100 Hz).
// The effective quantum scales with priority - see process_quantum_for().
#define TIME_SLICE_TICKS    5

// A process that has been READY without running for this many ticks gets a
// temporary priority boost, so low-priority work cannot starve forever.
#define PRIORITY_AGING_INTERVAL 50
#define PRIORITY_AGING_STEP     8

// Wait channels. A blocked process records what it is waiting for; whoever
// makes that resource ready calls process_wake_all() with the same value.
#define WAIT_CHANNEL_NONE      0
#define WAIT_CHANNEL_KEYBOARD  1
#define WAIT_CHANNEL_SERIAL    2

// Process states
typedef enum {
    PROCESS_READY      = 0,  // Runnable, sitting in the ready queue
    PROCESS_RUNNING    = 1,  // Currently on the CPU
    PROCESS_WAITING    = 2,  // Blocked on a wait channel (I/O)
    PROCESS_SLEEPING   = 3,  // Sleeping until wake_time
    PROCESS_TERMINATED = 4   // Zombie: exited, resources not yet reclaimed
} process_state_t;

// Saved CPU context.
//
// IMPORTANT: this struct is the FIRST member of process_t and
// context_switch.asm indexes it from offset 0. If you reorder these fields you
// must update the OFFSET_* constants in that file. A _Static_assert at the
// bottom of process.c enforces the layout at compile time.
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi;
    uint64_t rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;      // Where to resume execution
    uint64_t rflags;   // Saved flags, including the interrupt enable bit
} cpu_context_t;

// Task Control Block
typedef struct process_t {
    // ---- Must stay first: the assembly context switcher assumes offset 0 ----
    cpu_context_t registers;

    // ---- Identity ----
    uint32_t pid;
    uint32_t parent_pid;
    char     name[PROCESS_NAME_MAX];

    // ---- State machine ----
    process_state_t state;
    int32_t         exit_code;

    // ---- Scheduling ----
    uint32_t base_priority;          // What the process was created with
    uint32_t priority;               // Effective priority, raised by aging
    uint32_t time_slice_remaining;   // Ticks left in the current quantum
    uint64_t total_ticks;            // Lifetime CPU time, in ticks
    uint64_t ready_since;            // Tick when it last entered the queue
    uint64_t wake_time;              // Tick to wake at, when SLEEPING
    uint64_t wait_channel;           // What it is blocked on, when WAITING

    // ---- Memory ----
    uint64_t* page_table;       // CR3 value; NULL means the kernel's tables
    void*     kernel_stack;     // Base of the allocation (pass this to kfree)
    void*     kernel_stack_top; // Highest usable address
    void*     user_stack;       // Ring 3 stack (unused until user mode lands)

    // ---- Ready queue links ----
    struct process_t* next;
    struct process_t* prev;
} process_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void       scheduler_init(void);
process_t* process_create(const char* name, void (*entry)(void), uint32_t priority);

// Terminate the calling process. Does not return.
void process_exit(int32_t code) __attribute__((noreturn));

// Terminate another process by PID. Returns 1 on success, 0 if not found or
// the target is un-killable (the idle process).
int process_kill_pid(uint32_t pid);

// ---------------------------------------------------------------------------
// Yielding and blocking
// ---------------------------------------------------------------------------

// Give up the rest of the current quantum.
void process_yield(void);

// Sleep for a number of timer ticks / milliseconds. The process is taken off
// the run queue entirely, so it burns no CPU while it waits.
void process_sleep(uint64_t ticks);
void process_sleep_ms(uint64_t milliseconds);

// Block on a wait channel until someone calls process_wake_all() for it.
void process_block(uint64_t channel);

// Move every process blocked on `channel` back to the ready queue.
// Safe to call from an interrupt handler.
uint32_t process_wake_all(uint64_t channel);

// ---------------------------------------------------------------------------
// Scheduler internals
// ---------------------------------------------------------------------------

process_t* scheduler_pick_next(void);
void       scheduler_tick(void);       // Called from the timer IRQ
void       scheduler_start(void) __attribute__((noreturn));

// Perform a context switch now. Must be called with interrupts disabled.
void schedule(void);

// Reclaim memory from exited processes. Called from the idle loop, because a
// process cannot free the stack it is currently running on.
uint32_t scheduler_reap(void);

// ---------------------------------------------------------------------------
// Introspection
// ---------------------------------------------------------------------------

process_t*  get_current_process(void);
uint64_t    scheduler_get_ticks(void);
uint32_t    scheduler_get_process_count(void);
void        scheduler_print_stats(void);
const char* process_state_name(process_state_t state);

// Iterate every live process. Returns NULL once `index` runs past the end.
// Used by the shell's `ps` command.
process_t* process_at_index(uint32_t index);

// ---------------------------------------------------------------------------
// Assembly interface
// ---------------------------------------------------------------------------

// Saves the CPU context into `current` (skipped if NULL) and resumes `next`.
extern void context_switch_asm(process_t* current, process_t* next);

// Set by the timer when the running process has used up its quantum. Checked
// at the end of the IRQ handler, which is the only safe place to switch.
extern volatile uint8_t need_reschedule;

#endif // KERNEL_PROC_PROCESS_H
