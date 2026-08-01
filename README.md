# BlitzOS - A Modern x86-64 Operating System

> Building a high-performance operating system from scratch with multitasking support

## Project Status: PREEMPTIVE MULTITASKING + SYSCALLS + SHELL

**Current achievement:** Timer-driven preemption, a live `INT 0x80` system call
interface, an in-memory filesystem, and an interactive shell. Boot it and type
`help`.

Verified in QEMU: 156 context switches over 20 s of uptime with three
concurrent workers at different priorities, all sleeping, exiting and being
reaped with the heap returning to its exact pre-spawn size.

## 📚 Documentation Structure

### Essential Reading (Start Here)
- **[whole documentation/INDEX.md](whole documentation/INDEX.md)** - Complete documentation index and navigation
- **[whole documentation/achieved.md](whole documentation/achieved.md)** - ✅ All completed features
- **[whole documentation/OS_QUICK_REFERENCE.md](whole documentation/OS_QUICK_REFERENCE.md)** - Strategic overview & current status
- **[whole documentation/learning.md](whole documentation/learning.md)** - Complete implementation guide (3,500+ lines with multitasking!)
- **[whole documentation/OS_PROGRESS_TRACKING.md](whole documentation/OS_PROGRESS_TRACKING.md)** - Feature comparison matrix

### Strategic Documents
- **[whole documentation/OS_COMPETITIVE_ADVANTAGE.md](whole documentation/OS_COMPETITIVE_ADVANTAGE.md)** - Why BlitzOS is better than Linux for specific use cases
- **[whole documentation/architecture.md](whole documentation/architecture.md)** - Architecture decisions and technology stack

## 🏗️ Architecture Decisions

**Design Philosophy**: Unix-like monolithic kernel  
**Target Architecture**: x86-64 (64-bit long mode)  
**Primary Language**: C (92%) + Assembly x86-64 (8%)  
**Build System**: GNU Make + GCC cross-compiler  
**Testing Platform**: QEMU emulator  

### Why These Choices?
- **Unix-like**: Proven design, excellent learning resources, everything-is-a-file simplicity
- **Monolithic kernel**: Simpler to implement initially, better performance, easier debugging
- **x86-64**: Widespread hardware support, comprehensive documentation, modern architecture
- **C**: Industry standard, direct hardware access, no runtime overhead

## 🚀 Quick Start

### Prerequisites
1. WSL2 (Windows Subsystem for Linux) or native Linux
2. Cross-compiler toolchain (x86_64-elf-gcc)
3. QEMU emulator
4. NASM assembler
5. Git for version control

### Installation
```bash
# Install WSL2 (PowerShell as Administrator)
wsl --install -d Ubuntu

# Inside WSL, install development tools
sudo apt update
sudo apt install build-essential nasm qemu-system-x86 gdb git
sudo apt install libgmp-dev libmpfr-dev libmpc-dev texinfo
```

### Build & Run Your Kernel
```bash
# From Windows (WSL2):
wsl -e bash -c "cd /mnt/c/Users/over9/Desktop/Coding/OS && make clean && make all"

# Or from inside WSL:
cd ~/OS  # or wherever you cloned
make all              # Compile kernel and create BlitzOS.iso
make run              # Boot in QEMU
make run-serial       # Boot with the kernel log mirrored to your terminal
make run-headless     # No video window; drive the shell over serial
make run-trace        # Interrupt/reset tracing, for hunting triple faults
make debug            # Boot paused, waiting for GDB on :1234
make check            # Compile-check every C file with the host gcc
make config           # Show the toolchain and every discovered source file
make clean            # Remove build artifacts
make help             # Show all available commands
```

## 📖 Learning Path & Current Progress

### ✅ Phase 1: Foundation (COMPLETE)
- ✅ Environment setup (cross-compiler, QEMU, build system)
- ✅ Bootloader (GRUB2 + Multiboot2)
- ✅ Basic kernel with VGA text output
- ✅ Interrupt handling (GDT, IDT, ISR)

### ✅ Phase 2: Memory Management (COMPLETE)
- ✅ Physical memory manager (bitmap-based)
- ✅ Virtual memory (4-level paging)
- ✅ Kernel heap allocator (kmalloc/kfree)
- ✅ Memory protection via paging

### ✅ Phase 3: Process Management & Multitasking (COMPLETE!) 🎉
- ✅ Process structures (Task Control Block - TCB)
- ✅ Context switching (save/restore CPU registers)
- ✅ Scheduler implementation (round-robin)
- ✅ Cooperative multitasking
- ✅ Process creation and lifecycle
- ✅ **DEMO: 3 concurrent processes printing AAABBBCCC...**

### Phase 4: Preemptive Multitasking (COMPLETE)
- Timer-driven context switches from IRQ context
- Priority scheduling (0-255) with aging to prevent starvation
- Sleep/wake: sleeping processes leave the run queue entirely
- Blocking I/O via wait channels
- An idle process, and deferred reaping of exited processes

### Phase 5: File System (COMPLETE - in memory)
- VFS layer with path resolution, `.` and `..`
- ramfs: directories, files, growable data buffers
- File descriptors: open / read / write / seek / close
- Filesystem system calls, driven from the shell

### Phase 6: Advanced Drivers (PARTIAL)
- Serial port (COM1) for debugging, both output and input
- PC speaker tone generation
- Disk driver (ATA/AHCI) - not started
- Network stack - not started

### Phase 7: System Calls (COMPLETE)
- `INT 0x80` gate installed with DPL 3, ready for ring 3
- 26 syscalls: process control, filesystem, console
- Caller-side wrappers in `syscall.h`


### Phase 6: User Space (Weeks 21-24)
- ELF loader
- Standard C library port
- Shell implementation
- Basic utilities

### Phase 7: Advanced Features (Weeks 25+)
- Multi-core support (SMP)
- Network stack
- Security features
- Performance optimization

## 🎓 Essential Resources

### Must-Read
- [OSDev Wiki](https://wiki.osdev.org/) - THE essential resource
- [Intel Software Developer Manual](https://www.intel.com/sdm) Vol. 3 - Hardware reference
- [Operating Systems: Three Easy Pieces](https://pages.cs.wisc.edu/~remzi/OSTEP/) - Theory

### Reference Operating Systems
- **xv6** - MIT's educational Unix (9,000 lines, perfect for learning)
- **Linux** - Industry reference (start with older 2.6 versions)
- **SerenityOS** - Modern from-scratch OS with excellent documentation

### Community Support
- Reddit: r/osdev
- Discord: OSDev server
- Forum: forum.osdev.org
- IRC: #osdev on Libera.Chat

## 🛠️ Development Tools

- **Compiler**: GCC (x86_64-elf-gcc cross-compiler)
- **Assembler**: NASM
- **Linker**: GNU ld
- **Debugger**: GDB
- **Emulator**: QEMU
- **Version Control**: Git
- **Editor**: VS Code (recommended) / Vim / Emacs

## 🐛 Common Issues

- **Black screen**: Check VGA memory address (0xB8000), verify code reaches output
- **Triple fault**: Usually GDT/IDT setup issue, use Bochs for detailed debugging
- **Cross-compiler not found**: Add to PATH, check installation
- **Build errors**: Verify linker script syntax, check Makefile dependencies

See [troubleshooting.md](troubleshooting.md) for detailed solutions.

## 📁 Project Structure (Planned)

```
OS/
├── boot/                   # Bootloader code
├── kernel/                 # Core kernel
│   ├── arch/x86_64/        # Boot, GDT/TSS, IDT, interrupt stubs, context switch
│   ├── core/               # kernel_main, panic handler
│   ├── lib/                # kprintf, string routines
│   ├── mm/                 # PMM, paging, kernel heap
│   ├── proc/               # Scheduler and process table
│   ├── fs/vfs/             # VFS + ramfs
│   ├── shell/              # Interactive shell
│   └── sys/                # System calls
├── drivers/                # Device drivers
├── lib/                    # Kernel library functions
├── include/                # Header files
├── userspace/              # User programs and shell
├── build/                  # Build artifacts
├── docs/                   # Documentation
├── tools/                  # Development utilities
└── whole documentation/    # All the ReadMe with whole structure 
```

## Current Status

**Status**: v0.5 "Preemption"

**What works**
- Boot: GRUB2 + Multiboot2, 32 to 64-bit transition, 1 GB identity map
- Memory: PMM with contiguous allocation, 4-level paging, kmalloc/kfree
- Interrupts: GDT with user segments and a real TSS, IDT, PIC, spurious IRQ handling
- Preemptive scheduler: priorities, aging, sleep/wake, blocking I/O, reaping
- System calls: `INT 0x80`, 26 entry points, ring 3 ready
- Filesystem: ramfs with directories, file descriptors, syscalls
- Drivers: VGA (with hardware cursor), PIT, PS/2 keyboard, COM1 serial, PC speaker
- Interactive shell: 30 commands, line editing, history, tab-stop rendering
- Diagnostics: `kprintf`, panic screen with register dump, CR2 decoding, stack walk

**Next milestones**: per-process address spaces (needed for `fork`), an ELF
loader (needed for `exec`), and moving user processes into ring 3.

## Driving the shell over serial

The kernel accepts console input from COM1 as well as the PS/2 keyboard, so the
whole system can be scripted without a video window:

```bash
make run-headless

# or pipe a script in
(sleep 6; echo ps; sleep 1; echo mem) | \
    qemu-system-x86_64 -cdrom BlitzOS.iso -m 256M -display none -serial stdio
```

## Shell commands

```
help ver clear echo ps mem uptime irqstat
ls cd pwd cat write append touch mkdir rm stat
spawn kill sleep syscalls color beep rainbow party
crash history reboot halt
```

`syscalls` exercises every `INT 0x80` entry point end to end.
`spawn` starts a background worker so you can watch preemption interleave it
with the shell. `crash <div0|null|ud|assert|bp>` faults on purpose to show the
panic screen.

## 🤝 Contributing

This is a personal learning project, but suggestions and feedback are welcome! Feel free to:
- Report issues or ask questions
- Suggest improvements to documentation
- Share your own OS development experiences

## 📝 Development Journal

Document your daily progress, challenges, and solutions. This will be invaluable for:
- Tracking learning progress
- Debugging similar issues later
- Helping others who follow this path
- Building a portfolio of your work

## 🏆 Milestones

- [x] Environment setup complete
- [x] First bootable kernel
- [x] VGA text output working
- [x] Keyboard input functional
- [x] Interrupt system operational (GDT, IDT, PIC)
- [x] Timer driver working (PIT @ 100Hz)
- [x] Memory management operational (PMM, paging, heap)
- [x] Interactive shell with line editing and history
- [x] Process scheduler and multitasking (preemptive, priority-based)
- [x] System call interface (INT 0x80)
- [x] File system reads and writes files (ramfs)
- [x] Serial console for debugging
- [x] Panic handler with register dump and stack trace
- [ ] Per-process address spaces (prerequisite for fork)
- [ ] User mode execution (ring 3)
- [ ] ELF program loader
- [ ] Disk driver and a persistent filesystem
- [ ] Boots on real hardware

## 📜 License

This educational project and its documentation are for learning purposes. Code will be released under MIT License once substantial implementation exists.

## 🌟 Acknowledgments

Standing on the shoulders of giants:
- The OSDev community
- xv6 and MINIX for educational inspiration
- Linux and BSD for production references
- Countless tutorials and guides shared freely

---

## 📊 Project Statistics

```
Kernel Code:         ~5,000 lines
Documentation:       ~6,000 lines
Total Project:       ~11,000 lines
Comparison:          0.018% of Linux kernel size
Boot Time (QEMU):    <100ms
Memory Footprint:    ~2-5 MB
```

**Key Achievement**: Complete understanding of every single line of code in the OS. No black boxes, no mysteries.

---

**Remember**: Every expert OS developer started as a beginner. The journey of a thousand lines begins with a single boot sector! 🚀

*Version: 0.5 - Preemption*
