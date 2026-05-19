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
					refcount[i * 64 + j] = 1; // 참조 카운트 초기화
                    _unlockp();
                    return (i * 64 + j) * 4096;
                }
            }
        }
    }
    _unlockp();
    return 0; // 더 이상 할당할 페이지가 없음
}
uint64_t PhysPageAllocator::alloc_phy_pages(uint64_t page_count) {
    _lockp();
    uint64_t start_page = 0;
    uint64_t found_count = 0;
    for (uint64_t i = 0; i < (total_pages + 63) / 64; i++) {
        for (int j = 0; j < 64; j++) {
            if (!(bitmap[i] & (1ULL << j))) { // 빈 페이지 발견
                if (found_count == 0) {
                    start_page = i * 64 + j;
                }
                found_count++;
                if (found_count == page_count) { // 원하는 페이지 수만큼 찾음
                    for (uint64_t k = 0; k < page_count; k++) {
                        uint64_t page_index = start_page + k;
                        bitmap[page_index / 64] |= (1ULL << (page_index % 64));
                        refcount[page_index] = 1; // 참조 카운트 초기화
                    }
                    used_pages += page_count;
                    _unlockp();
                    return start_page * 4096;
                }
            } else {
                found_count = 0; // 연속된 빈 페이지가 끊김
            }
        }
    }
    _unlockp();
    return 0; // 더 이상 할당할 페이지가 없음
}
uint64_t PhysPageAllocator::get_page(uint64_t addr) {
    _lockp();
    uint64_t result = 0;
    uint64_t page_index = addr / 4096;
    uint64_t i = page_index / 64;
    uint64_t j = page_index % 64;
    if (bitmap[i] & (1ULL << j)) { // 페이지가 할당되어 있는지 확인
        refcount[page_index]++;
        result = 1;
    }
    _unlockp();
    return result;
}
uint64_t PhysPageAllocator::get_pages(uint64_t addr, uint64_t page_count) {
    _lockp();
    uint64_t result = 0;
    for (uint64_t k = 0; k < page_count; k++) {
        uint64_t page_index = (addr / 4096) + k;
        uint64_t i = page_index / 64;
        uint64_t j = page_index % 64;
        if (bitmap[i] & (1ULL << j)) { // 페이지가 할당되어 있는지 확인
            refcount[page_index]++;
            result++;
        }
    }
    _unlockp();
    return result;
}
extern bool booting;
uint64_t PhysPageAllocator::put_page(uint64_t addr) {
    _lockp();
    uint64_t result = 1;
    uint64_t page_index = addr / 4096;
    uint64_t i = page_index / 64;
    uint64_t j = page_index % 64;
    if (bitmap[i] & (1ULL << j)) { // 이미 할당된 페이지인지 확인
		if(--refcount[page_index] == 0) {
			bitmap[i] &= ~(1ULL << j);
			used_pages--;
		}
        result = 0;
    }
    _unlockp();
    return result;
}
uint64_t PhysPageAllocator::put_pages(uint64_t addr, uint64_t page_count) {
    _lockp();
    uint64_t result = 0;
    for (uint64_t k = 0; k < page_count; k++) {
        uint64_t page_index = (addr / 4096) + k;
        uint64_t i = page_index / 64;
        uint64_t j = page_index % 64;
        if (bitmap[i] & (1ULL << j)) { // 이미 할당된 페이지인지 확인
            if(--refcount[page_index] == 0) {
                bitmap[i] &= ~(1ULL << j);
                used_pages--;
            }
            result++;
        }
    }
    _unlockp();
    return result;
}
PhysPageAllocator::PhysPageAllocator() = default;
void PhysPageAllocator::init(uint64_t* _bitmap, uint64_t* _refcount, uint64_t _total_pages) {
    total_pages = _total_pages;
	uart_print("PhysPageAllocator init: total_pages=");
	uart_print_hex(total_pages);
	uart_print("\n");
	uart_print("bitmap addr=");
	uart_print_hex((uint64_t)_bitmap);
	uart_print("\n");
    bitmap = (uint64_t*)((uint64_t)_bitmap | HHDM_BASE);
	refcount = (uint64_t*)((uint64_t)_refcount | HHDM_BASE);
	used_pages = 0;
	for (uint64_t i = 0; i < total_pages; i++) {
        if(bitmap[i / 64] & (1ULL << (i % 64))) {
            used_pages++;
			refcount[i] = 1; // refcount는 부트로더가 초기화 안해줌
        }
    }
}
uint64_t PhysPageAllocator::get_total_pages() const { return total_pages; }
uint64_t PhysPageAllocator::get_used_pages() const { return used_pages; }
uint64_t PhysPageAllocator::get_free_pages() const { return total_pages - used_pages; }
uint64_t PhysPageAllocator::get_refcount(uint64_t addr) const {
    uint64_t page_index = addr / 4096;
    return refcount[page_index];
}