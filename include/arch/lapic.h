#ifndef __LAPIC_H__
#define __LAPIC_H__
#include "util/size.h"
extern volatile uint64_t lapic_base;
void init_lapic_base();
void disable_pic();
void enable_apic();
void setup_lapic_timer(uint8_t vector);
#define LAPIC_EOI_REGISTER  0xB0

static inline void lapic_eoi() {
    *(volatile uint32_t*)(lapic_base + LAPIC_EOI_REGISTER) = 0;
}

#endif // __LAPIC_H__