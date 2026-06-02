#ifndef __NEW_H__
#define __NEW_H__
#include "util/size.h"
#include "mm/allocator"
#ifdef SLAB_NEW
class NewObject {
private:
	uint64_t based_addr;
	uint64_t size;
public:
	NewObject() {

	}
	void* operator new(size_t size) {

	}
};
#else
template<uint64_t based_addr, uint64_t size>
class NewObject {
private:
	static uint64_t count;
	static uint64_t biggest;
protected:
	uint64_t state; // 0 = free, 1 = used 나머지 비트는 자유롭게 사용 가능
	uint64_t id;
	NewObject() {

	}
	~NewObject() {}
	void* operator new(size_t size) {
		uint64_t result = based_addr;
		uint64_t index = 0;
		while (((NewObject*)result)->state == 1) {
			result+=size;
			index++;
		}
		((NewObject*)result)->state = 1;
		((NewObject*)result)->id = index;
		if (index > biggest) {
			biggest = index;
		}
		count++;
		return result;
	}
	void operator delete(void* ptr) {
		NewObject* p = (NewObject*)ptr;
		p->state = 0;
		uint64_t page_start = (uint64_t)ptr & ~0xFFFULL;
		uint64_t page_end = page_start + 0x1000;
		auto& before_empty = [&](uint64_t addr) {
			while (addr >= page_start) {
				NewObject* temp = (NewObject*)addr;
				if (temp->state & 0b1) {
					return false;
				}
				addr -= size;
			}
			return true;
			};
		auto& after_empty = [&](uint64_t addr) {
			while (addr < page_end) {
				NewObject* temp = (NewObject*)addr;
				if (temp->state & 0b1) {
					return false;
				}
				addr += size;
			}
			return true;
			};
		if (before_empty((uint64_t)ptr - size) && after_empty((uint64_t)ptr + size)) {
			virt_page_allocator->free_virt_page(page_start);
		}
		if (p->id == biggest) {
			while (biggest > 0) {
				NewObject* temp = (NewObject*)(based_addr)+biggest;
				if (temp->state & 0b1) {
					break;
				}
				biggest--;
			}
		}
		count--;
	}
};
#endif

#endif // __NEW_H__