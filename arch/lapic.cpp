#include "arch/lapic.h"
#include "mm/allocator"
#include "arch/msr.h"
#include "arch/io.h"
#include "util/size.h"

volatile uint64_t lapic_base;

void init_lapic_base() {
    lapic_base = rdmsr(0x1B) & 0xFFFFF000;  // 하위 12비트는 무시
	lapic_base += MMIO_BASE; // MMIO 오프셋 더하기
    virt_page_allocator->alloc_virt_page(lapic_base, lapic_base - MMIO_BASE, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
}

// PIC 비활성화
void disable_pic() {
    outb(0xA1, 0xFF); // 슬레이브 PIC 마스크
    outb(0x21, 0xFF); // 마스터 PIC 마스크
}

// Local APIC 활성화
void enable_apic() {
    // 전역 lapic_base 사용
    uint64_t val = rdmsr(0x1B);
    val |= (1 << 11);  // APIC Global Enable
    wrmsr(0x1B, val);

    volatile uint32_t* lapic = (volatile uint32_t*)lapic_base;
    lapic[0xF0 / 4] = (lapic[0xF0 / 4] & 0xFFFFFDFF) | 0x100;
}
// IOAPIC 레지스터 접근

static inline void lapic_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(lapic_base + reg) = value;
}
#define LAPIC_REG_TIMER         0x320
#define LAPIC_TIMER_MODE_PERIODIC (1 << 17)
#define LAPIC_REG_TIMER_DIVIDE 0x3E0
#define LAPIC_REG_TIMER_INIT   0x380
void setup_lapic_timer(uint8_t vector) {
    // 1. Divide Configuration (Divide by 16)
    lapic_write(LAPIC_REG_TIMER_DIVIDE, 0b0011); // Divide by 16

    // 2. Set LVT Timer Register (인터럽트 벡터 설정)
    lapic_write(LAPIC_REG_TIMER, LAPIC_TIMER_MODE_PERIODIC | vector);

    // 3. Set Initial Count (클럭 사이클 수)
    lapic_write(LAPIC_REG_TIMER_INIT, 0x100000); // 값 작게 = 빠름, 크면 = 느림
}