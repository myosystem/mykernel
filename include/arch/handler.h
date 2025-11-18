#ifndef __HANDLER_H__
#define __HANDLER_H__
#include "util/size.h"
typedef struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) interrupt_frame_t;

__attribute__((interrupt))  void keyboard_handler(interrupt_frame_t* frame);
__attribute__((interrupt))  void dummy_mouse_handler(interrupt_frame_t* frame);
__attribute__((naked))      void timer_handler();
__attribute__((interrupt))  void none_handler(interrupt_frame_t* frame);
__attribute__((interrupt))  void page_fault_handler(interrupt_frame_t* frame, uint64_t error_code);
__attribute__((interrupt))  void general_protection_fault_handler(interrupt_frame_t* frame, uint64_t error_code);
__attribute__((interrupt))  void stack_segment_fault_handler(interrupt_frame_t* frame, uint64_t error_code);
__attribute__((naked))      void syscall_idthandler();
#endif // __HANDLER_H__