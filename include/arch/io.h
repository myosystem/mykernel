#ifndef __IO_H__
#define __IO_H__
#include "util/size.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("out dx, al" :: "a"(val), "d"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("in al, dx" : "=a"(ret) : "d"(port));
    return ret;
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("out dx, eax" :: "d"(port), "a"(val));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("in eax, dx" : "=a"(ret) : "d"(port));
    return ret;
}
#endif // __IO_H__