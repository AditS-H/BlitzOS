#include "process.h"
#include "../../drivers/vga.h"
#include "../mm/kheap.h"

// String utilities
static inline void strncpy_safe(char* dest, const char* src, size_t n)
{
    for (size_t i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[n-1] = '\0';
}

static inline int snprintf_safe(char* buf, size_t size, const char* fmt, uint32_t val)
{
    // Simple version: just handles "proc_%u"
    const char prefix[] = "proc_";
    const char* p = prefix;
    size_t i = 0;
    
    while (i < size - 1 && *p) {
        buf[i++] = *p++;
    }
    
    // Convert number to string
    uint32_t digits[10];
    int digit_count = 0;
    uint32_t temp = val;
    
    if (temp == 0) {
        digits[digit_count++] = 0;
    } else {
        while (temp > 0) {
            digits[digit_count++] = temp % 10;
            temp /= 10;
        }
    }
    
    for (int j = digit_count - 1; j >= 0 && i < size - 1; j--) {
        buf[i++] = '0' + digits[j];
    }
    
    buf[i] = '\0';
    return i;
}

// Global scheduler state
static scheduler_t scheduler = {0};
static process_t process_table[MAX_PROCESSES] = {0};

// Flag to indicate reschedule is needed
volatile uint8_t need_reschedule = 0;

/**
 * Initialize the scheduler
 */
void scheduler_init(void)
{
    scheduler.ready_queue_head = NULL;
    scheduler.ready_queue_tail = NULL;
    scheduler.current_process = NULL;
    scheduler.next_pid = 1;  // PID 0 reserved for idle
    scheduler.process_count = 0;
    scheduler.total_ticks = 0;
    
    vga_print("[SCHED] Scheduler initialized", VGA_COLOR_LIGHT_GREEN);
    vga_print("\n", VGA_COLOR_WHITE);
}

/**
 * Enqueue a process to the ready queue
 */
static void queue_enqueue(process_t* proc)
{
    if (!proc) return;
    
    proc->next = NULL;
    
    if (scheduler.ready_queue_tail == NULL) {
        // Queue is empty
        scheduler.ready_queue_head = proc;
        scheduler.ready_queue_tail = proc;
        proc->prev = NULL;
    } else {
        // Add to end
        proc->prev = scheduler.ready_queue_tail;
        scheduler.ready_queue_tail->next = proc;
        scheduler.ready_queue_tail = proc;
    }
}

/**
 * Dequeue a process from the ready queue
 */
static process_t* queue_dequeue(void)
{
    if (scheduler.ready_queue_head == NULL) {
        return NULL;
    }
    
    process_t* proc = scheduler.ready_queue_head;
    scheduler.ready_queue_head = proc->next;
    
    if (scheduler.ready_queue_head == NULL) {
        scheduler.ready_queue_tail = NULL;
    } else {
        scheduler.ready_queue_head->prev = NULL;
    }
    
    proc->next = NULL;
    proc->prev = NULL;
    
    return proc;
}

/**
 * Create a new process
 * Returns NULL if failed (out of memory or max processes reached)
 */
process_t* process_create(const char* name, void (*entry)(void), uint32_t priority)
{
    if (scheduler.process_count >= MAX_PROCESSES) {
        vga_print("[ERR] Max processes reached", VGA_COLOR_LIGHT_RED);
        vga_print("\n", VGA_COLOR_WHITE);
        return NULL;
    }
    
    // Allocate process structure
    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if (!proc) {
        vga_print("[ERR] Failed to allocate process", VGA_COLOR_LIGHT_RED);
        vga_print("\n", VGA_COLOR_WHITE);
        return NULL;
    }
    
    // Initialize process fields
    proc->pid = scheduler.next_pid++;
    proc->parent_pid = scheduler.current_process ? scheduler.current_process->pid : 0;
    
    if (name) {
        strncpy_safe(proc->name, name, sizeof(proc->name));
    } else {
        snprintf_safe(proc->name, sizeof(proc->name), "proc_%u", proc->pid);
    }
    
    // State
    proc->state = PROCESS_READY;
    proc->priority = priority;
    proc->time_slice_remaining = TIME_SLICE_TICKS;
    proc->total_ticks = 0;
    proc->wake_time = 0;
    
    // Allocate stacks
    proc->kernel_stack = kmalloc(PROCESS_STACK_SIZE);
    if (!proc->kernel_stack) {
        kfree(proc);
        vga_print("[ERR] Failed to allocate kernel stack", VGA_COLOR_LIGHT_RED);
        vga_print("\n", VGA_COLOR_WHITE);
        return NULL;
    }
    
    proc->user_stack = kmalloc(PROCESS_STACK_SIZE);
    if (!proc->user_stack) {
        kfree(proc->kernel_stack);
        kfree(proc);
        vga_print("[ERR] Failed to allocate user stack", VGA_COLOR_LIGHT_RED);
        vga_print("\n", VGA_COLOR_WHITE);
        return NULL;
    }
    
    // Initialize stack pointers to top of stacks
    proc->kernel_stack_top = (void*)((uint64_t)proc->kernel_stack + PROCESS_STACK_SIZE);
    
    // Set up registers for first-time execution
    proc->registers.rsp = (uint64_t)proc->kernel_stack_top;
    proc->registers.rbp = (uint64_t)proc->kernel_stack_top;
    proc->registers.rip = (uint64_t)entry;
    proc->registers.rflags = 0x202;  // Enable interrupts (IF flag)
    
    // Page table (for now, use kernel's - no isolation yet)
    proc->page_table = NULL;  // NULL means use kernel page table
    
    // Add to ready queue
    queue_enqueue(proc);
    scheduler.process_count++;
    
    vga_print("[SCHED] Created process: ", VGA_COLOR_LIGHT_CYAN);
    vga_print(proc->name, VGA_COLOR_LIGHT_CYAN);
    vga_print(" (PID: ", VGA_COLOR_LIGHT_CYAN);
    vga_print_int(proc->pid, VGA_COLOR_LIGHT_CYAN);
    vga_print(")", VGA_COLOR_LIGHT_CYAN);
    vga_print("\n", VGA_COLOR_WHITE);
    
    return proc;
}

/**
 * Kill a process and free resources
 */
void process_kill(process_t* proc)
{
    if (!proc) return;
    
    proc->state = PROCESS_TERMINATED;
    
    // Remove from ready queue if there
    if (proc->prev) proc->prev->next = proc->next;
    if (proc->next) proc->next->prev = proc->prev;
    if (scheduler.ready_queue_head == proc) scheduler.ready_queue_head = proc->next;
    if (scheduler.ready_queue_tail == proc) scheduler.ready_queue_tail = proc->prev;
    
    // Free resources
    if (proc->kernel_stack) kfree(proc->kernel_stack);
    if (proc->user_stack) kfree(proc->user_stack);
    kfree(proc);
    
    scheduler.process_count--;
}

/**
 * Get the currently running process
 */
process_t* get_current_process(void)
{
    return scheduler.current_process;
}

/**
 * Pick the next process to run (round-robin)
 */
process_t* scheduler_pick_next(void)
{
    // If current process is still running and has time left
    if (scheduler.current_process != NULL &&
        scheduler.current_process->state == PROCESS_RUNNING &&
        scheduler.current_process->time_slice_remaining > 0) {
        return scheduler.current_process;  // Keep current process
    }
    
    // Current process needs to wait or is done
    if (scheduler.current_process != NULL) {
        scheduler.current_process->state = PROCESS_READY;
        queue_enqueue(scheduler.current_process);
    }
    
    // Get next ready process
    process_t* next = queue_dequeue();
    
    if (next == NULL) {
        // No processes ready - this shouldn't happen
        vga_print("[ERR] No processes ready!", VGA_COLOR_LIGHT_RED);
        vga_print("\n", VGA_COLOR_WHITE);
        return NULL;
    }
    
    next->state = PROCESS_RUNNING;
    next->time_slice_remaining = TIME_SLICE_TICKS;
    
    return next;
}

/**
 * Called every timer tick (10ms with PIT @ 100Hz)
 */
void scheduler_tick(void)
{
    scheduler.total_ticks++;
    
    if (scheduler.current_process) {
        scheduler.current_process->total_ticks++;
        scheduler.current_process->time_slice_remaining--;
        
        // Time quantum expired?
        if (scheduler.current_process->time_slice_remaining == 0) {
            need_reschedule = 1;
        }
    }
}

/**
 * Start the scheduler (called from kernel_main)
 */
void scheduler_start(void)
{
    // Pick first process
    process_t* first = scheduler_pick_next();
    
    if (!first) {
        vga_print("[ERR] No processes to run!", VGA_COLOR_LIGHT_RED);
        vga_print("\n", VGA_COLOR_WHITE);
        return;
    }
    
    vga_print("[*] Starting first process: ", VGA_COLOR_LIGHT_GREEN);
    vga_print(first->name, VGA_COLOR_LIGHT_GREEN);
    vga_print("\n\n", VGA_COLOR_WHITE);
    
    scheduler.current_process = first;
    
    // Switch to first process (no current process to save)
    context_switch_asm(NULL, first);
    
    // Should never return here
    vga_print("[ERR] Context switch returned!", VGA_COLOR_LIGHT_RED);
    vga_print("\n", VGA_COLOR_WHITE);
}

/**
 * Perform a context switch (cooperative - called by processes)
 */
void do_schedule(void)
{
    process_t* current = scheduler.current_process;
    
    // Pick next process to run
    process_t* next = scheduler_pick_next();
    
    if (!next) {
        return;  // No process to switch to
    }
    
    // Don't switch to ourselves
    if (next == current) {
        return;
    }
    
    // Update scheduler state
    scheduler.current_process = next;
    
    // Perform context switch: save current, load next
    context_switch_asm(current, next);
}

/**
 * Print scheduler statistics
 */
void scheduler_print_stats(void)
{
    vga_print("\n[SCHED] Scheduler Statistics:\n", VGA_COLOR_LIGHT_GREEN);
    vga_print("  Total Ticks: ", VGA_COLOR_LIGHT_GREEN);
    vga_print_int(scheduler.total_ticks, VGA_COLOR_LIGHT_GREEN);
    vga_print("\n  Processes: ", VGA_COLOR_LIGHT_GREEN);
    vga_print_int(scheduler.process_count, VGA_COLOR_LIGHT_GREEN);
    vga_print("\n  Current: ", VGA_COLOR_LIGHT_GREEN);
    if (scheduler.current_process) {
        vga_print(scheduler.current_process->name, VGA_COLOR_LIGHT_GREEN);
        vga_print(" (PID ", VGA_COLOR_LIGHT_GREEN);
        vga_print_int(scheduler.current_process->pid, VGA_COLOR_LIGHT_GREEN);
        vga_print(", CPU ticks: ", VGA_COLOR_LIGHT_GREEN);
        vga_print_int(scheduler.current_process->total_ticks, VGA_COLOR_LIGHT_GREEN);
        vga_print(")", VGA_COLOR_LIGHT_GREEN);
    } else {
        vga_print("None", VGA_COLOR_LIGHT_GREEN);
    }
    vga_print("\n", VGA_COLOR_WHITE);
}
