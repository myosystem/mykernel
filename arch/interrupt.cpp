#include "arch/interrupt.h"

#include "arch/handler.h"
#include "arch/idt.h"
#include "util/size.h"
#include "mm/allocator"
void init_interrupts() {
    for (int i = 0; i < IDT_SIZE; i++) {
        set_idt_gate(i, (uint64_t)none_handler, 0x08, 0x8E);
    }
    set_idt_gate(8, (uint64_t)double_fault_handler, 0x08, 0x8E);
    set_idt_gate(13, (uint64_t)general_protection_fault_handler, 0x08, 0x8E);
    set_idt_gate(14, (uint64_t)page_fault_handler, 0x08, 0x8E);
    set_idt_gate(12, (uint64_t)stack_segment_fault_handler, 0x08, 0x8E);

    set_idt_gate(32, (uint64_t)timer_handler, 0x08, 0x8E);
    set_idt_gate(33, (uint64_t)keyboard_handler, 0x08, 0x8E);
    set_idt_gate(0x2C, (uint64_t)mouse_handler, 0x08, 0x8E);
    set_idt_gate(0x35, (uint64_t)xhci_handler, 0x08, 0x8E);
    set_idt_gate(0x36, (uint64_t)e1000_handler, 0x08, 0x8E);
    set_idt_gate(0x80, (uint64_t)syscall_idthandler, 0x08, 0xEE);
    set_idt_gate(0x81, (uint64_t)waiting_idthandler, 0x08, 0xEE);
    load_idt();
}