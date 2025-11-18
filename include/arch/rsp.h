#ifndef __RSP_H__
#define __RSP_H__
#include "util/size.h"
static inline uint64_t get_rsp() {
    uint64_t v;
    asm volatile ("mov %0, rsp" : "=r"(v));
    return v;
}
static inline void set_rsp(uint64_t v) {
    asm volatile ("mov rsp, %0" : : "r"(v));
}
#endif // __RSP_H__