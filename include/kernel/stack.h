#ifndef __STACK_H__
#define __STACK_H__
#include "util/size.h"
#include "mm/allocator"
#include "kernel/kernel.h"
#include "util/vector.h"
#include "util/util.h"
class Kernel_Stack {
private:
	static constexpr inline size_t page = 2;
	static inline vector<uint64_t>* bitmap;
	static inline uint8_t bitmap_buff[sizeof(vector<uint64_t>)];
public:
	static void init() {
		bitmap = new (bitmap_buff) vector<uint64_t>();
	}
	void* operator new(size_t size) {
		uint64_t n = 0;
		while ((n >> 6) < bitmap->size()) {
			if ((*bitmap)[n >> 6] != (uint64_t)-1) {
				n += __builtin_ctzll(~(*bitmap)[n >> 6]);
				break;
			}
			n += 64;
		}
		if (n >> 6 >= bitmap->size()) {
			bitmap->push_back(0);
		}
		(*bitmap)[n >> 6] |= (1ull << (n & 63));
		uint64_t addr = virt_page_allocator->alloc_virt_pages(pml4_addr(PML4::PROCESS_KERNEL_STACK_HEAP) + (n * (page + 1) + 1) * PageSize, PageSize * page, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::G);
		uint64_t* result = (uint64_t*)(addr + page * PageSize);

		return (void*)(addr + page * PageSize);
	}
	void operator delete(void* ptr) {
		if (ptr == nullptr) return;
		uint64_t n = ((uint64_t)ptr - pml4_addr(PML4::PROCESS_KERNEL_STACK_HEAP)) / PageSize;
		n /= (page + 1);
		if (n >> 6 >= bitmap->size()) return;
		(*bitmap)[n >> 6] &= ~(1ull << (n & 63));
		uint64_t addr = pml4_addr(PML4::PROCESS_KERNEL_STACK_HEAP) + (n * (page + 1) + 1) * PageSize;
		virt_page_allocator->free_virt_pages(addr, PageSize * page);
	}
	static uint64_t get_stack_top(uint64_t stack_addr) {
		uint64_t stack_top = (stack_addr - pml4_addr(PML4::PROCESS_KERNEL_STACK_HEAP)) / PageSize;
		stack_top = (stack_top / (page + 1)) * (page + 1) + page + 1;
		stack_top *= PageSize;
		return stack_top + pml4_addr(PML4::PROCESS_KERNEL_STACK_HEAP);
	}
};

#endif // __STACK_H__