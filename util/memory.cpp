#include "util/memory.h"

void* memcpy(void* dest, const void* src, unsigned long long size) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    while (size--) {
        *d++ = *s++;
    }

    return dest;
}

void* memset(void* dest, int value, unsigned long long size) {
    unsigned char* d = (unsigned char*)dest;
    while (size--) {
        *d++ = (unsigned char)value;
    }
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}