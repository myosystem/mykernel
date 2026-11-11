#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__
#include "util/size.h"
void set_interrupt(int n, uint64_t handler, uint16_t selector, uint8_t flags);
void init_interrupts();

#endif // __INTERRUPT