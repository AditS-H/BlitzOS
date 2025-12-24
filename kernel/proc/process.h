#ifndef KERNEL_PROC_PROCESS_H
#define KERNEL_PROC_PROCESS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES 256
#define PROCESS_STACK_SIZE 8192
#define DEFAULT_PRIORITY 128
#define TIME_SLICE_TICKS 20
// Set to 1 to enable periodic scheduler summary prints
#define DEBUG_SCHED_SUMMARY 1

// Process states
typedef enum {
    PROCESS_READY = 0,      // Ready to run
    PROCESS_RUNNING = 1,    // Currently running
    PROCESS_WAITING = 2,    // Waiting for I/O
    PROCESS_SLEEPING = 3,   // Sleeping (wake at time)
    PROCESS_TERMINATED = 4  // Dead (cleanup needed)
} process_state_t;

// Task Control Block (TCB)
typedef struct process_t {
    // Identity
    uint32_t pid;              // Process ID
    uint32_t parent_pid;       // Parent process ID
    char name[32];             // Process name
    
    // State machine
    process_state_t state;     // Current state
    
    // CPU context (saved for context switches)
    struct {
        uint64_t rax, rbx, rcx, rdx;
        uint64_t rsi, rdi;
        uint64_t rbp, rsp;
        uint64_t r8, r9, r10, r11;
        uint64_t r12, r13, r14, r15;
        uint64_t rip;          // Return address
        uint64_t rflags;       // Flags register
    } registers;
    
    // Memory management
    uint64_t* page_table;      // Page table base (CR3 value)
    void* kernel_stack;        // Kernel mode stack
    void* kernel_stack_top;    // Top of kernel stack (for interrupts)
    void* user_stack;          // User mode stack (future)
    
    // Scheduling
    uint32_t priority;         // 0 (low) - 255 (high)
    uint32_t time_slice_remaining;  // Ticks left in current quantum
    uint32_t total_ticks;      // Total CPU time (ticks)
    uint32_t wake_time;        // When to wake from sleep (in ticks)
    
    // Linked list pointers
    struct process_t* next;
    struct process_t* prev;
    
} process_t;

// Scheduler state
typedef struct {
    process_t* ready_queue_head;  // First ready process
    process_t* ready_queue_tail;  // Last ready process
    process_t* current_process;   // Currently running
    uint32_t next_pid;            // Next available PID
    uint32_t process_count;       // Total processes
    uint32_t total_ticks;         // Total elapsed ticks
} scheduler_t;

// Function declarations
void scheduler_init(void);
process_t* process_create(const char* name, void (*entry)(void), uint32_t priority);
void process_kill(process_t* proc);
void process_sleep(uint32_t ticks);
process_t* scheduler_pick_next(void);
void scheduler_tick(void);
process_t* get_current_process(void);
void scheduler_print_stats(void);

// Assembly function for context switching
extern void context_switch_asm(process_t* current, process_t* next);

// Preemptive context switch from interrupt (returns new stack pointer)
uint64_t preempt_handler(uint64_t stack_ptr);

// Reschedule flag (set by timer)
extern volatile uint8_t need_reschedule;

// Trigger a context switch
void do_schedule(void);

// Start first process (called by kernel_main)
void scheduler_start(void);

#endif // KERNEL_PROC_PROCESS_H
