#ifndef __BUFFER_H__
#define __BUFFER_H__
#include <util/size.h>
#include <mm/allocator>
template <typename>
class BufferPool {
private:
	volatile inline static uint64_t count = 0;
	volatile inline static uint64_t biggest = 0;
	inline static uint64_t based_addr = 0;
	inline static uint64_t size = 0;
protected:
	BufferPool() {}
	virtual ~BufferPool() {}
public:
	volatile uint64_t state;
	uint64_t id;
	uint8_t padding[64 - 24];
	void* operator new(size_t) {
		uint64_t result = based_addr;
		uint64_t index = 0;
		while (((volatile BufferPool*)result)->state & 1) {
			result += size + 64;
			index++;
		}
		((volatile BufferPool*)result)->state = 1;
		((volatile BufferPool*)result)->id = index;
		if (index >= biggest) {
			biggest = index + 1; // biggest = 지금까지 쓴 최대 슬롯 수
		}
		count = count + 1;
		return (void*)result;
	}
	void operator delete(void* ptr) {
		volatile BufferPool* p = (volatile BufferPool*)ptr;
		p->state = 0;
		count = count - 1;
	}
	static void* get(uint64_t index) {
		if (virt_page_allocator->get_pa((based_addr + index * (size + 64)) & ~0xFFFULL) == ~0ULL)
			return nullptr;
		if (!(((volatile BufferPool*)(based_addr + index * (size + 64)))->state & 1))
			return nullptr;
		return (void*)(based_addr + index * (size + 64));
	}
	static uint64_t max() { return biggest; }
	static uint64_t get_count() { return count; }
	void* addr() { return (uint8_t*)this + sizeof(*this); }
};

#endif // __BUFFER_H__