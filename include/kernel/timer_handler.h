#ifndef __TIMER_HANDLER_H__
#define __TIMER_HANDLER_H__
#include "arch/idt.h"
#include "util/size.h"
extern "C" __attribute__((noinline)) uint64_t* c_timer_handler(context_t* frame);
#endif // __TIMER_HANDLER_H__