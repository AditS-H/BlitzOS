// string.h - Freestanding string / memory helpers for the BlitzOS kernel.
//
// The kernel is built with -ffreestanding, so there is no libc. Every routine
// the kernel needs has to live here. These are intentionally simple, portable
// C implementations rather than hand-tuned assembly.

#ifndef KERNEL_LIB_STRING_H
#define KERNEL_LIB_STRING_H

#include <stdint.h>
#include <stddef.h>

// -------- Memory --------
void*  memset(void* dest, int value, size_t count);
void*  memcpy(void* dest, const void* src, size_t count);
void*  memmove(void* dest, const void* src, size_t count);
int    memcmp(const void* a, const void* b, size_t count);

// -------- Strings --------
size_t strlen(const char* str);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
char*  strcpy(char* dest, const char* src);

// Always NUL-terminates (unlike the C standard strncpy). Returns dest.
char*  strlcpy(char* dest, const char* src, size_t size);

char*  strchr(const char* str, int c);
char*  strrchr(const char* str, int c);

// -------- Conversion --------
// Parses a base-10 signed integer. Skips leading spaces, accepts an optional
// sign. Stops at the first non-digit. Returns 0 for input with no digits.
int64_t  str_to_int(const char* str);

// Case-insensitive ASCII compare, used by the shell so "PS" == "ps".
int strcasecmp_ascii(const char* a, const char* b);

#endif // KERNEL_LIB_STRING_H
