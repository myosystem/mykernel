#include "mm/PhysPageAllocator"
#include "mm/allocator"
#include "debug/log.h"
volatile uint32_t ppaspinv = 0;
inline void _lockp() {
	while (__atomic_test_and_set(&ppaspinv, __ATOMIC_ACQUIRE)) {
        __asm__ __volatile__("pause");
    }
}
inline void _unlockp() {
	__atomic_clear(&ppaspinv, __ATOMIC_RELEASE);
}
uint64_t PhysPageAllocator::alloc_phy_page() {
    _lockp();
    for (uint64_t i = 0; i < (total_pages + 63) / 64; i++) {
        if (bitmap[i] != 0xFFFFFFFFFFFFFFFF) { // 아직 빈 페이지가 있음
            for (int j = 0; j < 64; j++) {
                if (!(bitmap[i] & (1ULL << j))) { // 빈 페이지 발견
                    bitmap[i] |= (1ULL << j);
                    used_pages++;
                    _unlockp();
                    return (i * 64 + j) * 4096;
                }
            }
        }
    }
    _unlockp();
    return 0; // 더 이상 할당할 페이지가 없음
}
void PhysPageAllocator::free_phy_page(uint64_t addr) {
    _lockp();
    uint64_t page_index = addr / 4096;
    uint64_t i = page_index / 64;
    uint64_t j = page_index % 64;
    if (bitmap[i] & (1ULL << j)) { // 이미 할당된 페이지인지 확인
        bitmap[i] &= ~(1ULL << j);
        used_pages--;
    }
    _unlockp();
}
PhysPageAllocator::PhysPageAllocator() = default;
void PhysPageAllocator::init(uint64_t* _bitmap, uint64_t _total_pages) {
    total_pages = _total_pages;
	uart_print("PhysPageAllocator init: total_pages=");
	uart_print_hex(total_pages);
	uart_print("\n");
	uart_print("bitmap addr=");
	uart_print_hex((uint64_t)_bitmap);
	uart_print("\n");
    bitmap = (uint64_t*)((uint64_t)_bitmap | HHDM_BASE);
	used_pages = 0;
}
uint64_t PhysPageAllocator::get_total_pages() const { return total_pages; }
uint64_t PhysPageAllocator::get_used_pages() const { return used_pages; }
uint64_t PhysPageAllocator::get_free_pages() const { return total_pages - used_pages; }