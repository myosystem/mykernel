#ifndef __LAPIC_H__
#define __LAPIC_H__
#include "util/size.h"
#include "arch/msr.h"
#include "debug/log.h"

extern volatile uint64_t lapic_base;
extern uint64_t g_tsc_hz;
extern bool tsc_available;
extern uint64_t fake_tsc;
extern uint64_t fake_deadline;
extern uint64_t g_lapic_hz;

static inline void lapic_write(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(lapic_base + reg) = value;
}
// 초기화
void init_lapic_base();
void disable_pic();
void enable_apic();
void enable_cursor();
void enable_keyboard();
// 타이머
void setup_lapic_timer_tsc_deadline(uint8_t vector);
void tsc_init(void);

// TSC
static inline uint64_t rdtsc_get(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// 스케줄러/타이머용 (TSC-Deadline 없으면 fake_tsc 반환)
static inline uint64_t tsc_get(void) {
    if (tsc_available)
        return rdtsc_get();
    else
        return fake_tsc;
}
uint64_t tsc_get_freq_cpuid(void);
uint64_t calibrate_with_pit(void);
static inline uint64_t ns_to_ticks(uint64_t ns) {
    return ((tsc_available ? g_tsc_hz : g_lapic_hz) * ns) / 1000000000ULL;
}

static inline uint64_t ms_to_ticks(uint64_t ms) {
    return ((tsc_available ? g_tsc_hz : g_lapic_hz) * ms) / 1000ULL;
}

// TSC-Deadline
static inline void tsc_deadline_set(uint64_t deadline) {
    if (tsc_available) {
        uart_print("TSC deadline set: ");
		uart_print_hex(deadline);
        uart_print("\n");
        __asm__ volatile ("mfence" ::: "memory");
        wrmsr(0x6E0, deadline);
    }
    else {
        fake_deadline = deadline;
        uint64_t diff = deadline - fake_tsc;  // fake_tsc 단위 = LAPIC 틱
        lapic_write(0x380, (deadline > fake_tsc) ? diff : 1);
    }
}

static inline void lapic_tsc_deadline_set(uint64_t delay_ns) {
    tsc_deadline_set(tsc_get() + ns_to_ticks(delay_ns));
}

static inline void lapic_tsc_deadline_set_ms(uint64_t delay_ms) {
    tsc_deadline_set(tsc_get() + ms_to_ticks(delay_ms));
}
void lapic_tsc_deadline_cancel(void);

// EOI
#define LAPIC_EOI_REGISTER 0xB0
static inline void lapic_eoi() {
    *(volatile uint32_t*)(lapic_base + LAPIC_EOI_REGISTER) = 0;
}

#endif // __LAPIC_H__