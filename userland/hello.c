// hello.c - The smallest real BlitzOS program.
//
// Compiled separately from the kernel, shipped in the ISO as a GRUB module,
// loaded at runtime by the ELF loader, and run as its own scheduled process.
// Every line of output below travels through INT 0x80 into the kernel.
//
//   Build:  make -C userland
//   Run:    run /bin/hello

#include <blitz.h>

// _start, not main. There is no C runtime to set up argc/argv and call main
// for us - the kernel jumps straight to whatever the ELF header names as the
// entry point, and the linker records _start.
void _start(void)
{
    print("\n");
    rainbow("Hello from a real user program!");
    print("\n\n");

    print("  I am a separately compiled ELF binary.\n");

    print("  My PID is ");
    print_uint(getpid());
    print(", my parent is ");
    print_uint(getppid());
    print(".\n");

    print("  The system has ");
    print_uint(proc_count());
    print(" processes and has been up ");
    print_uint(uptime_ms());
    print(" ms.\n\n");

    // Prove we are genuinely preemptible: sleep between lines and watch the
    // desktop clock and starfield keep running.
    print("  Counting slowly, so you can see the scheduler still working:\n");

    for (int i = 1; i <= 5; i++) {
        print("    tick ");
        print_uint((uint64_t)i);
        print("\n");
        sleep_ms(400);
    }

    // Write a file through the syscall interface, then read it back.
    int fd = open("/tmp/hello.txt", O_WRITE | O_CREATE | O_TRUNC);
    if (fd >= 0) {
        const char* message = "written by /bin/hello\n";
        write(fd, message, strlen(message));
        close(fd);
        print("\n  Wrote /tmp/hello.txt - try `cat /tmp/hello.txt`.\n");
    }

    print("\n  Exiting cleanly.\n\n");
    exit(0);
}
