#include "arch/ioapic.h"
#include "mm/allocator"
#define IOAPIC_BASE  (MMIO_BASE + 0xFEC00000ULL)

void init_ioapic_base() {
    virt_page_allocator->alloc_virt_page(IOAPIC_BASE, IOAPIC_BASE - MMIO_BASE, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
}

static inline void ioapic_write(uint8_t reg, uint32_t value) {
    volatile uint32_t* ioapic = (volatile uint32_t*)IOAPIC_BASE;
    ioapic[0] = reg;
    ioapic[4] = value;
}

static inline uint32_t ioapic_read(uint8_t reg) {
    volatile uint32_t* ioapic = (volatile uint32_t*)IOAPIC_BASE;
    ioapic[0] = reg;
    return ioapic[4];
}

void ioapic_set_redirection(uint8_t irq, uint8_t vector, uint8_t apic_id) {
    uint8_t reg = 0x10 + irq * 2;        // LOW
    uint8_t reg_high = reg + 1;          // HIGH

    // HIGH: destination APIC ID (target CPU)
    uint32_t high = ((uint32_t)apic_id) << 24;

    // LOW 설정
    uint32_t low = 0;
    low |= vector;           // Vector (IDT entry number)
    low |= (0 << 8);         // Delivery Mode = Fixed
    low |= (0 << 11);        // Destination Mode = Physical
    low |= (0 << 13);        // Polarity = Active High
    low |= (0 << 15);        // Trigger Mode = Edge
    low |= (0 << 16);        // Mask = 0 (enable)

    // IOAPIC에 쓰기
    ioapic_write(reg_high, high);  // Destination CPU
    ioapic_write(reg, low);        // Redirection Entry Low
}