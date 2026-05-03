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
// PS/2 컨트롤러에서 마우스 활성화 및 인터럽트 설정
void enable_cursor() {
    // 1. 버퍼 비우기
    while (inb(0x64) & 0x01) inb(0x60);

    // 2. 마우스 포트 활성화
    outb(0x64, 0xA8);

    // 3. CCB 읽어서 수정
    outb(0x64, 0x20);
    while (!(inb(0x64) & 0x01));
    uint8_t ccb = inb(0x60);
    ccb |= 0x02;   // 마우스 IRQ12 활성화
    ccb &= ~0x20;  // 마우스 클럭 활성화
    outb(0x64, 0x60);
    outb(0x60, ccb);

    // 4. 마우스 리셋
    outb(0x64, 0xD4);
    outb(0x60, 0xFF);
    while (!(inb(0x64) & 0x01));
    inb(0x60); // 0xFA (ACK)
    while (!(inb(0x64) & 0x01));
    inb(0x60); // 0xAA (self-test pass)
    while (!(inb(0x64) & 0x01));
    inb(0x60); // 0x00 (mouse ID)

    // 5. 샘플레이트 설정 (너무 높으면 VMware에서 버퍼 과부하)
    outb(0x64, 0xD4); outb(0x60, 0xF3); // Set Sample Rate
    while (!(inb(0x64) & 0x01)); inb(0x60); // ACK
    outb(0x64, 0xD4); outb(0x60, 100);
    while (!(inb(0x64) & 0x01)); inb(0x60); // ACK

    // 6. 마우스 활성화
    outb(0x64, 0xD4);
    outb(0x60, 0xF4);
    while (!(inb(0x64) & 0x01));
    inb(0x60); // ACK

    // 7. 남은 버퍼 완전히 비우기
    while (inb(0x64) & 0x01) inb(0x60);
}
// PS/2 컨트롤러에서 키보드 활성화
void enable_keyboard() {
    outb(0x64, 0xAE);  // 키보드 활성화
    while (inb(0x64) & 0x01) {
        inb(0x60); // 남아있는 ACK나 쓰레기 데이터를 싹 비움
    }
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

static inline int tsc_deadline_supported(void) {
    uint32_t ecx;
    __asm__ volatile (
        "cpuid"
        : "=c"(ecx)
        : "a"(1)
        : "ebx", "edx"
        );
    // CPUID.01H:ECX[24] = TSC-Deadline 지원 비트
    return (ecx >> 24) & 1;
}
// 기존 정의 재사용
#define LAPIC_REG_TIMER         0x320
#define LAPIC_REG_TIMER_DIVIDE  0x3E0
#define LAPIC_REG_TIMER_INIT    0x380
#define LAPIC_REG_EOI           0xB0

// 추가 정의
#define LAPIC_TIMER_MODE_TSC_DEADLINE (2 << 17)  // 기존 PERIODIC (1<<17)과 동일 패턴
#define LAPIC_TIMER_MODE_ONESHOT  (0 << 17)
#define MSR_IA32_TSC_DEADLINE         0x6E0
#define PIT_HZ          1193182ULL
#define PIT_CMD         0x43
#define PIT_CH2         0x42
#define PIT_GATE        0x61
uint64_t g_tsc_hz = 0;
uint64_t g_lapic_hz = 0;
bool tsc_available = false;
uint64_t fake_tsc = 0;
uint64_t fake_deadline = 0;
uint64_t calibrate_with_pit(void) {
    uint16_t pit_ticks = (uint16_t)(PIT_HZ / 100); // 10ms

    // PIT 설정
    outb(PIT_CMD, 0b10110010);
    outb(PIT_CH2, (uint8_t)(pit_ticks & 0xFF));
    outb(PIT_CH2, (uint8_t)(pit_ticks >> 8));

    // GATE 시작
    uint8_t gate = inb(PIT_GATE);
    outb(PIT_GATE, (gate & ~1) | 1);

    if (tsc_available) {
        // TSC Hz 측정
        uint64_t start = rdtsc_get();
        while (!(inb(PIT_GATE) & 0x20));
        return (rdtsc_get() - start) * 100;
    }
    else {
        lapic_write(LAPIC_REG_TIMER, LAPIC_TIMER_MODE_ONESHOT | 0xFF);
        lapic_write(LAPIC_REG_TIMER_DIVIDE, 0b0011);
        lapic_write(LAPIC_REG_TIMER_INIT, 0xFFFFFFFF);

        uint64_t tsc_start = rdtsc_get();  // TSC도 같이 측정 시작
        while (!(inb(PIT_GATE) & 0x20));
        uint64_t tsc_elapsed = rdtsc_get() - tsc_start;

        uint32_t lapic_current = *(volatile uint32_t*)(lapic_base + 0x390);
        uint32_t lapic_elapsed = 0xFFFFFFFF - lapic_current;

		g_tsc_hz = (tsc_elapsed * 100) / lapic_elapsed; // TSC Hz 계산

        return (uint64_t)lapic_elapsed * 100;
    }
}
uint64_t tsc_get_freq_cpuid(void) {
    uint32_t eax, ebx, ecx;
    __asm__ volatile ("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx)
        : "a"(0x15)
        : "edx");

    // eax = TSC/crystal 분모, ebx = 분자, ecx = crystal Hz
    if (eax == 0 || ebx == 0 || ecx == 0)
        return 0; // 지원 안 함 → 방법 2로 fallback

    return (uint64_t)ecx * ebx / eax;
}
// TSC-Deadline 초기화 (기존 setup_lapic_timer와 동일한 시그니처)
void setup_lapic_timer_tsc_deadline(uint8_t vector) {
    // Divide, Init Count는 TSC-Deadline 모드에서 무시됨 → 설정 불필요
    if(tsc_available)
        lapic_write(LAPIC_REG_TIMER, LAPIC_TIMER_MODE_TSC_DEADLINE | vector);
    else {
        lapic_write(LAPIC_REG_TIMER, LAPIC_TIMER_MODE_ONESHOT | vector);
        lapic_write(LAPIC_REG_TIMER_DIVIDE, 0b0011);
    }
}
void tsc_init(void) {
    if (!tsc_deadline_supported()) {
        g_lapic_hz = calibrate_with_pit();// lapic_ticks_per_tsc_tick 구하기
        return;
    }
	tsc_available = true;
    g_tsc_hz = tsc_get_freq_cpuid();
    if (g_tsc_hz == 0)
        g_tsc_hz = calibrate_with_pit(); // fallback
}

// 취소
void lapic_tsc_deadline_cancel(void) {
    if (tsc_available) {
        //__asm__ volatile ("mfence" ::: "memory");
        wrmsr(MSR_IA32_TSC_DEADLINE, 0);
    }
    else {
        lapic_write(0x380, 0);
    }
}