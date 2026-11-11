#ifndef __IDT_H__
#define __IDT_H__
#define IDT_SIZE 256
#include "kernel/kernel.h"
#include "util/memory.h"
#include "util/size.h"
#include "util/new.h"
void load_idt();
void set_idt_gate(int n, uint16_t selector, uint8_t flags);
typedef struct __context_t {
    uint64_t gs, fs, es, ds;
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vec, errno;
    uint64_t rip, cs, rflags, rsp, ss;
} context_t;
class Context : public NewObject<pml4_addr(PML4::PROCESS_CONTEXT_HEAP), 256, nullptr, nullptr>, public context_t {
public:
    uint64_t args[(256 - 24 - sizeof(context_t)) / 8];
    Context() {
        memset((context_t*)this, 0, sizeof(context_t));
        memset(args, 0, sizeof(args));
    }
    Context(const context_t& src) {
        *(context_t*)this = src;
        memset(args, 0, sizeof(args));
    }
};
#endif // __IDT_H__