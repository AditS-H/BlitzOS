#include "string.h"

void* memset(void* dest, int value, size_t count)
{
    uint8_t* d = (uint8_t*)dest;
    uint8_t  v = (uint8_t)value;

    // Byte-at-a-time until 8-byte aligned, then 8 bytes at a time.
    while (count && ((uintptr_t)d & 7)) {
        *d++ = v;
        count--;
    }

    uint64_t pattern = 0x0101010101010101ULL * v;
    while (count >= 8) {
        *(uint64_t*)d = pattern;
        d += 8;
        count -= 8;
    }

    while (count--) {
        *d++ = v;
    }

    return dest;
}

void* memcpy(void* dest, const void* src, size_t count)
{
    uint8_t*       d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    while (count >= 8) {
        *(uint64_t*)d = *(const uint64_t*)s;
        d += 8;
        s += 8;
        count -= 8;
    }

    while (count--) {
        *d++ = *s++;
    }

    return dest;
}

void* memmove(void* dest, const void* src, size_t count)
{
    uint8_t*       d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    if (d == s || count == 0) {
        return dest;
    }

    // Overlapping and dest is after src: copy backwards so we do not
    // clobber bytes we have not read yet.
    if (d > s && d < s + count) {
        d += count;
        s += count;
        while (count--) {
            *--d = *--s;
        }
        return dest;
    }

    return memcpy(dest, src, count);
}

int memcmp(const void* a, const void* b, size_t count)
{
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;

    while (count--) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
    }
    return 0;
}

size_t strlen(const char* str)
{
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

int strcmp(const char* a, const char* b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char* a, const char* b, size_t n)
{
    while (n && *a && (*a == *b)) {
        a++;
        b++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

char* strcpy(char* dest, const char* src)
{
    char* out = dest;
    while ((*dest++ = *src++)) {
        // copy including the terminator
    }
    return out;
}

char* strlcpy(char* dest, const char* src, size_t size)
{
    if (size == 0) {
        return dest;
    }

    size_t i = 0;
    while (i + 1 < size && src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return dest;
}

char* strchr(const char* str, int c)
{
    char target = (char)c;
    while (*str) {
        if (*str == target) {
            return (char*)str;
        }
        str++;
    }
    return target == '\0' ? (char*)str : NULL;
}

char* strrchr(const char* str, int c)
{
    char        target = (char)c;
    const char* found  = NULL;

    while (*str) {
        if (*str == target) {
            found = str;
        }
        str++;
    }
    if (target == '\0') {
        return (char*)str;
    }
    return (char*)found;
}

int64_t str_to_int(const char* str)
{
    if (!str) {
        return 0;
    }

    while (*str == ' ' || *str == '\t') {
        str++;
    }

    int negative = 0;
    if (*str == '-') {
        negative = 1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    int64_t value = 0;
    while (*str >= '0' && *str <= '9') {
        value = value * 10 + (*str - '0');
        str++;
    }

    return negative ? -value : value;
}

int strcasecmp_ascii(const char* a, const char* b)
{
    for (;;) {
        char ca = *a;
        char cb = *b;

        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;

        if (ca != cb) {
            return (int)(uint8_t)ca - (int)(uint8_t)cb;
        }
        if (ca == '\0') {
            return 0;
        }
        a++;
        b++;
    }
}
