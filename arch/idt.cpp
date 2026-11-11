#include "arch/idt.h"
#include "util/size.h"
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));
idt_entry idt[IDT_SIZE] __attribute__((aligned(16)));
idt_ptr idt_reg;
extern "C" char isr_stubs[];
asm(R"(
.text
.global isr_stubs
.balign 16
isr_stubs:
.set vec, 0
.rept 256
    .balign 16
    .if (vec == 8) || (vec >= 10 && vec <= 14) || (vec == 17) || (vec == 21) || (vec == 29) || (vec == 30)
    .else
        push 0
    .endif
    push vec
    jmp isr_common
    .set vec, vec + 1
.endr
)");
void set_idt_gate(int n, uint16_t selector, uint8_t flags) {
    uint64_t handler = (uint64_t)isr_stubs + (uint64_t)n * 16;
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].offset_middle = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].selector = selector;
    idt[n].zero = 0;
    idt[n].type_attr = flags;
    idt[n].reserved = 0;
}
void _load_idt(idt_ptr* _idt_ptr) {
    __asm__ __volatile__(
        "lidt [%0];"
        : : "r"(_idt_ptr) : "memory"
    );
}
void load_idt() {
    idt_reg.limit = (sizeof(struct idt_entry) * IDT_SIZE) - 1;
    idt_reg.base = (uint64_t)&idt;
    _load_idt(&idt_reg);
}
context_t* current;
context_t* next;
