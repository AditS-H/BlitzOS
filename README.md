# BlitzOS - A Modern x86-64 Operating System

> Building a high-performance operating system from scratch with multitasking support

## 🎉 Project Status: MULTITASKING WORKING! 🎉

**Current Achievement:** Full cooperative multitasking with 3+ concurrent processes, round-robin scheduling, and context switching. See A, B, C characters printing in rotation!

## � Documentation Structure

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
make run              # Run in QEMU and watch multitasking!
make run-serial       # Run in QEMU with serial output
make debug            # Debug with GDB
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

### ⏳ Phase 4: Preemptive Multitasking (PLANNED)
- ⏳ Timer interrupt forced context switches
- ⏳ Process priorities
- ⏳ Sleep/wake mechanisms
- ⏳ Preemptive scheduler improvements

### ⏳ Phase 5: File System (NOT STARTED)
- ⏳ VFS layer design
- ⏳ Basic filesystem implementation
- ⏳ File operations
- ⏳ Directory management

### ⏳ Phase 6: Advanced Drivers (NOT STARTED)
- ⏳ Disk driver (ATA/AHCI)
- ⏳ Serial port for debugging
- ⏳ Network stack (future)
- ⏳ GPU support (future)


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
│   ├── arch/               # Architecture-specific code
│   ├── mm/                 # Memory management
│   ├── process/            # Process management
│   └── fs/                 # File system
├── drivers/                # Device drivers
├── lib/                    # Kernel library functions
├── include/                # Header files
├── userspace/              # User programs and shell
├── build/                  # Build artifacts
├── docs/                   # Documentation
├── tools/                  # Development utilities
└── whole documentation/    # All the ReadMe with whole structure 
```

## 🎯 Current Status

**Status**: ✅ Kernel Foundation Complete (v0.5 - Post-Heap Edition)  
**Lines of Code**: ~5,000 lines kernel + 6,000 lines documentation  
**What Works**:
- ✅ Boot system (GRUB2 + Multiboot2, 32→64-bit transition)
- ✅ Memory management (PMM, 4-level paging, kernel heap with kmalloc/kfree)
- ✅ Interrupts (GDT, IDT, PIC, ISR/IRQ handlers)
- ✅ Drivers (VGA text-mode, PIT timer @ 100Hz, PS/2 keyboard)
- ✅ Interactive shell (keyboard echo)

**Next Milestone**: Process scheduler & multitasking  
**Timeline**: Following 3-month roadmap to production  

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
- [x] Interactive shell (keyboard echo)
- [ ] Process scheduler & multitasking
- [ ] Virtual memory & per-process address spaces
- [ ] System calls interface
- [ ] User mode execution
- [ ] File system reads files
- [ ] ELF program loader
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

*Last Updated: December 16, 2025*  
*Version: 0.5 - Post-Heap Edition*
