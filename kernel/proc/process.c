// process.c - Process table, ready queue, and the preemptive scheduler.
//
// How preemption works here:
//
//   1. The PIT fires IRQ0 at 100 Hz. The assembly stub saves every register
//      onto the *current process's* kernel stack and calls irq_handler().
//   2. irq_handler() calls scheduler_tick(), which ages priorities, wakes
//      sleepers, and decrements the running process's quantum. When the
//      quantum hits zero it raises need_reschedule.
//   3. irq_handler() acknowledges the PIC (EOI) and then, still with
//      interrupts disabled, calls schedule().
//   4. schedule() calls context_switch_asm(), which swaps RSP to the next
//      process's stack and returns into *its* copy of irq_handler.
//   5. That process's irq_handler returns into its own interrupt stub, which
//      pops its registers and IRETQs back to whatever it was doing.
//
// The half-finished interrupt frame simply waits on each process's stack
// until that process is scheduled again. EOI is sent *before* the switch,
// otherwise the PIC would stay masked and the timer would never fire again.

#include "process.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"
#include "../mm/kheap.h"
#include "../core/panic.h"
#include "../arch/x86_64/interrupts.h"
#include "../../drivers/vga.h"

// ---------------------------------------------------------------------------
// Scheduler state
// ---------------------------------------------------------------------------

typedef struct {
    process_t* ready_head;
    process_t* ready_tail;
    process_t* current;
    uint32_t   next_pid;
    uint32_t   process_count;
    uint64_t   total_ticks;
    uint64_t   context_switches;
    uint64_t   last_aging_tick;
} scheduler_t;

static scheduler_t sched;

// Every live process, for `ps`, sleep scanning and priority aging. Slot i is
// NULL when free. This replaces the unused process_table[] that used to sit
// here doing nothing.
static process_t* all_processes[MAX_PROCESSES];

// The idle process lives in .bss rather than the heap: it must exist before
// anything else can run, and it must never fail to allocate.
static process_t idle_process __attribute__((aligned(FPU_STATE_ALIGN)));
static uint8_t   idle_stack[PROCESS_STACK_SIZE] __attribute__((aligned(16)));

volatile uint8_t need_reschedule = 0;

// ---------------------------------------------------------------------------
// Layout guards
//
// context_switch.asm reads the saved registers at fixed byte offsets. If
// anyone reorders process_t or cpu_context_t, the kernel would not crash - it
// would silently restore garbage into RSP, which is far worse. These asserts
// turn that into a build failure.
// ---------------------------------------------------------------------------

_Static_assert(offsetof(process_t, registers) == 0,
               "context_switch.asm requires registers at offset 0 of process_t");
_Static_assert(offsetof(cpu_context_t, rsp) == 56,
               "context_switch.asm OFFSET_RSP is 56");
_Static_assert(offsetof(cpu_context_t, rip) == 128,
               "context_switch.asm OFFSET_RIP is 128");
_Static_assert(offsetof(cpu_context_t, rflags) == 136,
               "context_switch.asm OFFSET_RFLAGS is 136");
_Static_assert(sizeof(cpu_context_t) == 144, "cpu_context_t must be 18 qwords");
_Static_assert(offsetof(process_t, fpu_state) == 144,
               "context_switch.asm OFFSET_FPU is 144");
_Static_assert(offsetof(process_t, fpu_state) % FPU_STATE_ALIGN == 0,
               "FXSAVE requires the state image to be 16-byte aligned");

// ---------------------------------------------------------------------------
// Ready queue (intrusive doubly-linked list)
// ---------------------------------------------------------------------------

static void queue_push_back(process_t* proc)
{
    proc->next = NULL;
    proc->prev = sched.ready_tail;

    if (sched.ready_tail) {
        sched.ready_tail->next = proc;
    } else {
        sched.ready_head = proc;
    }
    sched.ready_tail = proc;
}

static void queue_remove(process_t* proc)
{
    if (proc->prev) {
        proc->prev->next = proc->next;
    } else if (sched.ready_head == proc) {
        sched.ready_head = proc->next;
    }

    if (proc->next) {
        proc->next->prev = proc->prev;
    } else if (sched.ready_tail == proc) {
        sched.ready_tail = proc->prev;
    }

    proc->next = NULL;
    proc->prev = NULL;
}

// True if `proc` is currently linked into the ready queue.
static int queue_contains(process_t* proc)
{
    return proc->prev != NULL || proc->next != NULL || sched.ready_head == proc;
}

// Highest effective priority wins. Ties go to whoever is earliest in the
// list, which preserves round-robin fairness inside a priority level.
static process_t* pick_highest_ready(void)
{
    process_t* best = NULL;

    for (process_t* p = sched.ready_head; p; p = p->next) {
        if (!best || p->priority > best->priority) {
            best = p;
        }
    }
    return best;
}

// Higher priority processes get a slightly longer quantum: 5 ticks (50 ms) at
// the bottom, 8 ticks (80 ms) at the top.
static uint32_t process_quantum_for(const process_t* proc)
{
    if (proc == &idle_process) {
        return 1;
    }
    return TIME_SLICE_TICKS + (proc->base_priority / 64);
}

// ---------------------------------------------------------------------------
// Process table helpers
// ---------------------------------------------------------------------------

static int table_insert(process_t* proc)
{
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (all_processes[i] == NULL) {
            all_processes[i] = proc;
            return 1;
        }
    }
    return 0;
}

process_t* process_at_index(uint32_t index)
{
    if (index >= MAX_PROCESSES) {
        return NULL;
    }
    return all_processes[index];
}

const char* process_state_name(process_state_t state)
{
    switch (state) {
    case PROCESS_READY:      return "READY";
    case PROCESS_RUNNING:    return "RUNNING";
    case PROCESS_WAITING:    return "BLOCKED";
    case PROCESS_SLEEPING:   return "SLEEPING";
    case PROCESS_TERMINATED: return "ZOMBIE";
    default:                 return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// The idle process
//
// Guarantees there is always something to run. Without it, an empty ready
// queue used to print "[ERR] No processes ready!" and fall over.
// ---------------------------------------------------------------------------

static void idle_thread(void)
{
    for (;;) {
        // Reclaim exited processes here: a process cannot free the stack it
        // is standing on, so the actual kfree() is deferred to idle.
        scheduler_reap();

        // Sleep the CPU until the next interrupt. Without HLT this loop would
        // peg a real core (and your laptop fan) at 100%.
        __asm__ volatile("hlt");
    }
}

// ---------------------------------------------------------------------------
// Stack setup
// ---------------------------------------------------------------------------

// Runs if a process function returns instead of calling exit(). Without this
// the `ret` would jump to whatever garbage was on the fresh stack.
static void process_thread_exit(void)
{
    process_exit(0);
}

// Prepares `proc` to be started by context_switch_asm().
static void process_setup_context(process_t* proc, void* stack_base,
                                  void (*entry)(void))
{
    // Zero the whole context. The old code left rbx/rcx/r8-r15 holding
    // whatever was in the heap block, and restored that garbage into the CPU
    // the first time the process ran.
    memset(&proc->registers, 0, sizeof(proc->registers));

    // Give the process a valid FXSAVE image. A zeroed one would leave MXCSR at
    // 0, unmasking every SSE exception so the first inexact result traps.
    sse_init_fpu_state(proc->fpu_state);

    uint64_t top = ((uint64_t)stack_base + PROCESS_STACK_SIZE) & ~0xFULL;

    // Plant the exit trampoline as the entry function's return address, so
    // `ret` from a process body lands somewhere sane.
    uint64_t* sp = (uint64_t*)top;
    *(--sp) = (uint64_t)process_thread_exit;

    proc->kernel_stack     = stack_base;
    proc->kernel_stack_top = (void*)top;

    proc->registers.rsp = (uint64_t)sp;
    proc->registers.rbp = 0;              // terminates stack traces cleanly
    proc->registers.rip = (uint64_t)entry;

    // 0x202 = IF (interrupts enabled) plus the always-set bit 1. Restored via
    // POPFQ by the context switcher, so a resumed process gets interrupts back
    // exactly as it left them.
    proc->registers.rflags = 0x202;

    // System V requires RSP % 16 == 8 on entry to a function (as if a CALL had
    // just pushed a return address). `top` is 16-aligned and we pushed one
    // qword, so this holds by construction - but assert it, because a
    // misaligned stack produces bizarre faults much later.
    KASSERT((proc->registers.rsp % 16) == 8);
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void scheduler_init(void)
{
    memset(&sched, 0, sizeof(sched));
    memset(all_processes, 0, sizeof(all_processes));

    sched.next_pid = 1;  // PID 0 is reserved for idle

    // Build the idle process by hand: no heap, never queued, never killable.
    memset(&idle_process, 0, sizeof(idle_process));
    idle_process.pid           = 0;
    idle_process.parent_pid    = 0;
    strlcpy(idle_process.name, "idle", PROCESS_NAME_MAX);
    idle_process.state         = PROCESS_READY;
    idle_process.base_priority = PRIORITY_IDLE;
    idle_process.priority      = PRIORITY_IDLE;
    idle_process.page_table    = NULL;
    process_setup_context(&idle_process, idle_stack, idle_thread);
    idle_process.time_slice_remaining = 1;

    all_processes[0] = &idle_process;
    sched.process_count = 1;

    kok("Scheduler ready: preemptive, priority-based, %u process slots\n",
        (uint32_t)MAX_PROCESSES);
}

// ---------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------

process_t* process_create(const char* name, void (*entry)(void), uint32_t priority)
{
    if (!entry) {
        kerror("process_create: NULL entry point\n");
        return NULL;
    }

    if (priority > 255) {
        priority = 255;
    }

    uint64_t flags = irq_save();

    if (sched.process_count >= MAX_PROCESSES) {
        irq_restore(flags);
        kerror("process_create: process table full (%u)\n",
               (uint32_t)MAX_PROCESSES);
        return NULL;
    }

    // kmalloc_aligned, not kmalloc: the TCB embeds the FXSAVE image, and
    // FXSAVE faults on a destination that is not 16-byte aligned. Anything
    // allocated this way must be released with kfree_aligned().
    process_t* proc = (process_t*)kmalloc_aligned(sizeof(process_t),
                                                  FPU_STATE_ALIGN);
    if (!proc) {
        irq_restore(flags);
        kerror("process_create: out of memory for TCB\n");
        return NULL;
    }

    // kmalloc does not zero. Everything below depends on a clean slate.
    memset(proc, 0, sizeof(process_t));

    void* stack = kmalloc(PROCESS_STACK_SIZE);
    if (!stack) {
        kfree_aligned(proc);
        irq_restore(flags);
        kerror("process_create: out of memory for kernel stack\n");
        return NULL;
    }

    proc->pid        = sched.next_pid++;
    proc->parent_pid = sched.current ? sched.current->pid : 0;

    if (name && name[0]) {
        strlcpy(proc->name, name, PROCESS_NAME_MAX);
    } else {
        ksnprintf(proc->name, PROCESS_NAME_MAX, "proc_%u", proc->pid);
    }

    proc->state                = PROCESS_READY;
    proc->exit_code            = 0;
    proc->base_priority        = priority;
    proc->priority             = priority;
    proc->total_ticks          = 0;
    proc->ready_since          = sched.total_ticks;
    proc->wake_time            = 0;
    proc->wait_channel         = WAIT_CHANNEL_NONE;
    proc->page_table           = NULL;   // shares the kernel address space
    proc->user_stack           = NULL;   // allocated when ring 3 lands

    process_setup_context(proc, stack, entry);
    proc->time_slice_remaining = process_quantum_for(proc);

    if (!table_insert(proc)) {
        kfree(stack);
        kfree_aligned(proc);
        irq_restore(flags);
        kerror("process_create: process table full\n");
        return NULL;
    }

    queue_push_back(proc);
    sched.process_count++;

    irq_restore(flags);

    kprintf_color(VGA_COLOR_LIGHT_CYAN,
                  "[SCHED] spawned %-12s pid=%u prio=%u\n",
                  proc->name, proc->pid, priority);

    return proc;
}

// ---------------------------------------------------------------------------
// The context switch
// ---------------------------------------------------------------------------

void schedule(void)
{
    // Callers must have interrupts disabled: we are about to mutate the ready
    // queue and swap stacks, and an IRQ in the middle of that is unrecoverable.
    process_t* prev = sched.current;

    if (prev && prev->state == PROCESS_RUNNING) {
        // Still runnable, so put it back. Aging resets: it just had the CPU.
        prev->state       = PROCESS_READY;
        prev->priority    = prev->base_priority;
        prev->ready_since = sched.total_ticks;

        // The idle process is never queued; it is the fallback, not a peer.
        if (prev != &idle_process && !queue_contains(prev)) {
            queue_push_back(prev);
        }
    }

    process_t* next = pick_highest_ready();
    if (next) {
        queue_remove(next);
    } else {
        next = &idle_process;
    }

    next->state                = PROCESS_RUNNING;
    next->priority             = next->base_priority;
    next->time_slice_remaining = process_quantum_for(next);
    need_reschedule            = 0;

    if (next == prev) {
        return;  // nothing better to run; keep going
    }

    sched.current = next;
    sched.context_switches++;

    // If prev exited, pass NULL so we do not write into a TCB that idle is
    // about to free.
    process_t* save_into = (prev && prev->state != PROCESS_TERMINATED) ? prev : NULL;

    context_switch_asm(save_into, next);
}

// Disable interrupts, switch, restore. For voluntary switches from normal
// kernel code (yield, sleep, blocking reads).
static void schedule_locked(void)
{
    uint64_t flags = irq_save();
    schedule();
    irq_restore(flags);
}

void process_yield(void)
{
    schedule_locked();
}

// ---------------------------------------------------------------------------
// Sleeping and blocking
// ---------------------------------------------------------------------------

void process_sleep(uint64_t ticks)
{
    if (ticks == 0) {
        process_yield();
        return;
    }

    uint64_t flags = irq_save();

    process_t* current = sched.current;
    if (!current || current == &idle_process) {
        // Nothing to deschedule (very early boot, or the idle loop itself):
        // fall back to a busy wait against the tick counter.
        uint64_t deadline = sched.total_ticks + ticks;
        irq_restore(flags);
        while (sched.total_ticks < deadline) {
            __asm__ volatile("hlt");
        }
        return;
    }

    current->state     = PROCESS_SLEEPING;
    current->wake_time = sched.total_ticks + ticks;

    schedule();
    irq_restore(flags);
}

void process_sleep_ms(uint64_t milliseconds)
{
    // 100 Hz timer => one tick per 10 ms. Round up so sleep(1) is not sleep(0).
    process_sleep((milliseconds + 9) / 10);
}

void process_block(uint64_t channel)
{
    uint64_t flags = irq_save();

    process_t* current = sched.current;
    if (!current || current == &idle_process) {
        // Cannot block the idle process; just wait for an interrupt.
        irq_restore(flags);
        __asm__ volatile("hlt");
        return;
    }

    current->state        = PROCESS_WAITING;
    current->wait_channel = channel;

    schedule();
    irq_restore(flags);
}

uint32_t process_wake_all(uint64_t channel)
{
    uint32_t woken = 0;

    // Called from interrupt handlers, so do not touch the current process or
    // switch here - only move TCBs back onto the ready queue.
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = all_processes[i];
        if (!p || p->state != PROCESS_WAITING || p->wait_channel != channel) {
            continue;
        }

        p->state        = PROCESS_READY;
        p->wait_channel = WAIT_CHANNEL_NONE;
        p->ready_since  = sched.total_ticks;

        if (p != &idle_process && !queue_contains(p)) {
            queue_push_back(p);
        }
        woken++;
    }

    return woken;
}

// ---------------------------------------------------------------------------
// Termination
// ---------------------------------------------------------------------------

void process_exit(int32_t code)
{
    disable_interrupts();

    process_t* current = sched.current;

    if (!current || current == &idle_process) {
        panic("process_exit: the idle process tried to exit (code %d)", code);
    }

    current->exit_code = code;
    current->state     = PROCESS_TERMINATED;

    // Make sure it is not sitting in the ready queue as a zombie.
    if (queue_contains(current)) {
        queue_remove(current);
    }

    kprintf_color(code == 0 ? VGA_COLOR_DARK_GREY : VGA_COLOR_LIGHT_RED,
                  "[SCHED] %s (pid %u) exited with code %d\n",
                  current->name, current->pid, code);

    schedule();

    // schedule() never picks a TERMINATED process, so we cannot get here.
    panic("process_exit: scheduler resumed a terminated process (pid %u)",
          current->pid);
}

int process_kill_pid(uint32_t pid)
{
    if (pid == 0) {
        return 0;  // the idle process is not killable
    }

    uint64_t flags = irq_save();

    process_t* target = NULL;
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (all_processes[i] && all_processes[i]->pid == pid) {
            target = all_processes[i];
            break;
        }
    }

    if (!target || target->state == PROCESS_TERMINATED) {
        irq_restore(flags);
        return 0;
    }

    // Killing yourself is just exiting.
    if (target == sched.current) {
        irq_restore(flags);
        process_exit(-1);
    }

    if (queue_contains(target)) {
        queue_remove(target);
    }
    target->state     = PROCESS_TERMINATED;
    target->exit_code = -1;

    irq_restore(flags);
    return 1;
}

uint32_t scheduler_reap(void)
{
    uint32_t reaped = 0;
    uint64_t flags  = irq_save();

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = all_processes[i];

        if (!p || p->state != PROCESS_TERMINATED || p == &idle_process) {
            continue;
        }

        // Never free the stack we are standing on. Reaping only runs from
        // idle, so this should be impossible - but the check is cheap and the
        // failure mode (use-after-free of the live stack) is catastrophic.
        if (p == sched.current) {
            continue;
        }

        all_processes[i] = NULL;
        sched.process_count--;

        if (p->kernel_stack) kfree(p->kernel_stack);
        if (p->user_stack)   kfree(p->user_stack);
        kfree_aligned(p);   // allocated with kmalloc_aligned for FXSAVE

        reaped++;
    }

    irq_restore(flags);
    return reaped;
}

// ---------------------------------------------------------------------------
// The timer tick
// ---------------------------------------------------------------------------

void scheduler_tick(void)
{
    sched.total_ticks++;

    // --- Wake anything whose sleep has expired ---
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = all_processes[i];
        if (!p || p->state != PROCESS_SLEEPING) {
            continue;
        }
        if (sched.total_ticks >= p->wake_time) {
            p->state       = PROCESS_READY;
            p->wake_time   = 0;
            p->ready_since = sched.total_ticks;
            if (p != &idle_process && !queue_contains(p)) {
                queue_push_back(p);
            }
            need_reschedule = 1;   // a waking process may outrank the current one
        }
    }

    // --- Priority aging ---
    // Anything that has been READY without running for a while gets a boost,
    // so a busy high-priority process cannot starve the rest of the system.
    if (sched.total_ticks - sched.last_aging_tick >= PRIORITY_AGING_INTERVAL) {
        sched.last_aging_tick = sched.total_ticks;

        for (process_t* p = sched.ready_head; p; p = p->next) {
            if (sched.total_ticks - p->ready_since < PRIORITY_AGING_INTERVAL) {
                continue;
            }
            if (p->priority < 250) {
                p->priority += PRIORITY_AGING_STEP;
            }
        }
    }

    // --- Charge the running process and expire its quantum ---
    //
    // If nothing is scheduled yet we are still in early boot: count the tick
    // and leave. Raising need_reschedule here would make irq_handler() call
    // schedule() before the idle process has a valid stack.
    process_t* current = sched.current;
    if (current) {
        current->total_ticks++;

        if (current->time_slice_remaining > 0) {
            current->time_slice_remaining--;
        }

        if (current->time_slice_remaining == 0) {
            need_reschedule = 1;
        }

        // If something in the ready queue now outranks us, switch early
        // instead of making a high-priority process wait out our quantum.
        process_t* best = pick_highest_ready();
        if (best && best->priority > current->priority) {
            need_reschedule = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// Startup and introspection
// ---------------------------------------------------------------------------

void scheduler_start(void)
{
    disable_interrupts();

    process_t* first = pick_highest_ready();
    if (first) {
        queue_remove(first);
    } else {
        first = &idle_process;
    }

    first->state                = PROCESS_RUNNING;
    first->time_slice_remaining = process_quantum_for(first);
    sched.current               = first;

    kok("Handing control to \"%s\" (pid %u)\n\n", first->name, first->pid);

    // NULL means "no context to save" - the boot stack is abandoned here and
    // we never return to kernel_main().
    context_switch_asm(NULL, first);

    panic("scheduler_start: context_switch_asm returned");
}

process_t* get_current_process(void)
{
    return sched.current;
}

uint64_t scheduler_get_ticks(void)
{
    return sched.total_ticks;
}

uint32_t scheduler_get_process_count(void)
{
    return sched.process_count;
}

process_t* scheduler_pick_next(void)
{
    return pick_highest_ready();
}

void scheduler_print_stats(void)
{
    kprintf_color(VGA_COLOR_LIGHT_GREEN, "\nScheduler statistics\n");
    kprintf("  Uptime            : %lu ticks (%lu.%02lu s)\n",
            sched.total_ticks, sched.total_ticks / 100, sched.total_ticks % 100);
    kprintf("  Processes         : %u\n", sched.process_count);
    kprintf("  Context switches  : %lu\n", sched.context_switches);

    if (sched.current) {
        kprintf("  Running           : %s (pid %u, %lu ticks of CPU)\n",
                sched.current->name, sched.current->pid,
                sched.current->total_ticks);
    } else {
        kprintf("  Running           : none\n");
    }
}
