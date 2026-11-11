#ifndef __HANDLER_H__
#define __HANDLER_H__
#include "util/size.h"
#include "arch/idt.h"
typedef context_t interrupt_frame_t;

void keyboard_handler(interrupt_frame_t* frame);
void mouse_handler(interrupt_frame_t* frame);
void none_handler(interrupt_frame_t* frame);
void page_fault_handler(interrupt_frame_t* frame);
void double_fault_handler(interrupt_frame_t* frame);
void general_protection_fault_handler(interrupt_frame_t* frame);
void stack_segment_fault_handler(interrupt_frame_t* frame);
void xhci_handler(interrupt_frame_t* frame);
void e1000_handler(interrupt_frame_t* frame);
void waiting_handler(context_t* frame);
#endif // __HANDLER_H__