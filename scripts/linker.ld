/* Linker script for BlitzOS kernel - x86-64 architecture
 * This script tells the linker how to organize our kernel in memory
 */

/* Entry point of our kernel - _start is defined in boot.asm */
ENTRY(_start)

/* Define the sections and their memory layout */
SECTIONS
{
    /* Kernel starts at 1MB (0x100000) - standard location for kernels */
    . = 1M;

    /* Multiboot header section - must be near the start of the file */
    .multiboot : ALIGN(4K)
    {
        *(.multiboot)
    }

    /* Read-only data section - contains code and constants */
    .text : ALIGN(4K)
    {
        *(.text)           /* All code sections */
        *(.text.*)         /* All subsections of code */
    }

    /* Read-only data (constants, string literals, etc.) */
    .rodata : ALIGN(4K)
    {
        *(.rodata)         /* Read-only data */
        *(.rodata.*)       /* Read-only data subsections */
    }

    /* Initialized data section - writable data with initial values */
    .data : ALIGN(4K)
    {
        *(.data)           /* All data sections */
        *(.data.*)         /* All subsections of data */
    }

    /* BSS section - uninitialized data (zeroed at startup) */
    .bss : ALIGN(4K)
    {
        *(COMMON)          /* Common uninitialized data */
        *(.bss)            /* BSS sections */
        *(.bss.*)          /* BSS subsections */
    }

    /* Mark the end of the kernel for memory management */
    kernel_end = .;

    /* Discard unnecessary sections that the compiler might generate */
    /DISCARD/ :
    {
        *(.comment)        /* Compiler comments */
        *(.eh_frame)       /* Exception handling frames (not needed yet) */
        *(.note.*)         /* Note sections */
    }
}
