#include "kernel/timer_handler.h"
#include "arch/lapic.h"
#include "debug/log.h"
#include "kernel/process.h"
char uart_buf[1000];
extern "C" __attribute__((noinline)) uint64_t* c_timer_handler(context_t* frame) {
    now_process->kernel_stack = (uint64_t*)frame;
    //uart_print("now_process:");
	//uart_print_hex((uint64_t)now_process);
	//uart_print("\nnext_process:");
	//uart_print_hex((uint64_t)now_process->next);
    add_process(now_process->process_id);
    if (process_queue->isEmpty()) {
        __asm__ __volatile__("hlt");
    }
    now_process = next_process();
    lapic_eoi();
    now_process->run_process();
    //uint64_t* ret = (uint64_t*)current;
    //asm volatile("" : "+a"(ret));
    return (uint64_t*)0;
}