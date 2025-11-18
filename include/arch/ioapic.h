#ifndef __IOAPIC_H__
#define __IOAPIC_H__

#include "util/size.h"
void init_ioapic_base();
void ioapic_set_redirection(uint8_t irq, uint8_t vector, uint8_t apic_id);

#endif // __IOAPIC_H__