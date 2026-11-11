#include "arch/interrupt.h"

#include "arch/handler.h"
#include "arch/idt.h"
#include "util/size.h"
#include "mm/allocator"
#include "kernel/syscall.h"
#include "kernel/timer_handler.h"

extern "C" void dispatch(context_t* frame);

asm(R"(
.text
.global isr_common
isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, ds
    push rax
    mov rax, es
    push rax
    mov rax, fs
    push rax
    mov rax, gs
    push rax
    mov rdi, rsp
    call dispatch
    pop rax
    mov gs, ax
    pop rax
    mov fs, ax
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq
)");
static uint64_t interrupt_list[IDT_SIZE];
void dispatch(context_t* frame) {
    ((void(*)(interrupt_frame_t*))interrupt_list[frame->vec])(frame);
}
void set_interrupt(int n, uint64_t handler, uint16_t selector, uint8_t flags) {
    set_idt_gate(n, selector, flags);
    interrupt_list[n] = handler;
}
void init_interrupts() {
    for (int i = 0; i < IDT_SIZE; i++) {
        set_interrupt(i, (uint64_t)none_handler, 0x08, 0x8E);
    }
    set_interrupt(8, (uint64_t)double_fault_handler, 0x08, 0x8E);
    set_interrupt(13, (uint64_t)general_protection_fault_handler, 0x08, 0x8E);
    set_interrupt(14, (uint64_t)page_fault_handler, 0x08, 0x8E);
    set_interrupt(12, (uint64_t)stack_segment_fault_handler, 0x08, 0x8E);

    set_interrupt(32, (uint64_t)timer_handler, 0x08, 0x8E);
    set_interrupt(33, (uint64_t)keyboard_handler, 0x08, 0x8E);
    set_interrupt(0x2C, (uint64_t)mouse_handler, 0x08, 0x8E);
    set_interrupt(0x35, (uint64_t)xhci_handler, 0x08, 0x8E);
    set_interrupt(0x36, (uint64_t)e1000_handler, 0x08, 0x8E);
    set_interrupt(0x80, (uint64_t)syscall_handler, 0x08, 0xEE);
    set_interrupt(0x81, (uint64_t)waiting_handler, 0x08, 0xEE);
    load_idt();
}