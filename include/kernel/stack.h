#ifndef __STACK_H__
#define __STACK_H__
#include "util/size.h"
#include "mm/allocator"
#include "kernel/kernel.h"
class Kernel_Stack {
private:
	static inline uint64_t stack_count = 0;
	static constexpr inline size_t page = 2;
public:
	void* operator new(size_t size) {
		uint64_t addr = virt_page_allocator->alloc_virt_pages(pml4_addr(PML4::PROCESS_KERNEL_STACK_HEAP) + (stack_count + 1) * PageSize, PageSize * page, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::G);
		stack_count += page + 1;
		return (void*)(addr + page * PageSize);
	}
};

#endif // __STACK_H__