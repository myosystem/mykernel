#ifndef __NEW_H__
#define __NEW_H__
#include "util/size.h"
#include "mm/allocator"
//#define SLAB_NEW
#ifdef SLAB_NEW
template<uint64_t based_addr, uint64_t size, void(*init)(void*), void(*destroy)(void*)>
class NewObject {
private:
	inline static uint64_t count = 0;
	static constexpr uint64_t BUFCTL_END = 0xFFFF;
	static constexpr uint32_t SLAB_COUNT = 4072 / (size + 2);
	struct SlabMeta {
		uint32_t   refcnt;     // 현재 할당된 객체 수
		uint32_t   s_free;     // 첫 번째 free 슬롯 인덱스 (BUFCTL_END면 꽉 참)
		void* next;
		void* prev;
		uint16_t bufctl[SLAB_COUNT];
	};
	inline static SlabMeta* partial = nullptr;
	inline static SlabMeta* full = nullptr;
	inline static SlabMeta* empty = nullptr;
	inline static uint64_t new_id = 0;
	static void* slab_alloc() {
		if (!partial) {		// 사용할 수 있는 슬랩이 없으면 새로 할당
			if (empty) {	// 빈 슬랩이 있으면 그것부터 사용
				SlabMeta* s = empty;
				empty = (SlabMeta*)s->next;
				s->next = partial;
				if (partial) {
					((SlabMeta*)partial)->prev = s;
				}
				partial = s;
			}
			else {	// 빈 슬랩도 없으면 새로 할당
				uint64_t new_slab = phy_page_allocator->alloc_phy_page() + HHDM_BASE;
				for (uint32_t i = 0; i < SLAB_COUNT; i++) {
					NewObject* obj = (NewObject*)new_slab;
					if (init)
						init(obj);	// 기초적인 초기화
					obj->id = new_id++;
					new_slab += size;
				}
				SlabMeta* s = (SlabMeta*)new_slab;
				for (uint32_t i = 0; i < SLAB_COUNT - 1; i++) {
					s->bufctl[i] = i + 1;
				}
				s->bufctl[SLAB_COUNT - 1] = BUFCTL_END;
				s->refcnt = 0;
				s->s_free = 0;
				s->next = nullptr;
				s->prev = nullptr;
				partial = s; // partial이 비어있으므로
			}
		}
		partial->refcnt++;
		void* result = (void*)(((uint64_t)partial & ~0xFFFULL) + partial->s_free * size);
		((NewObject*)result)->state = 1;	// 사용 중으로 표시
		((void**)based_addr)[((NewObject*)result)->id] = result;
		partial->s_free = partial->bufctl[partial->s_free];
		count++;
		if (partial->refcnt == SLAB_COUNT) {
			SlabMeta* s = partial;
			partial = (SlabMeta*)s->next;
			if (partial)
				partial->prev = nullptr;
			s->next = full;
			if (full) {
				((SlabMeta*)full)->prev = s;
			}
			full = s;
		}
		return result;
	}
	static void slab_free(void* ptr) {
		((NewObject*)ptr)->state = 0;	// 사용 중이 아님으로 표시
		((void**)based_addr)[((NewObject*)ptr)->id] = nullptr;
		uint64_t slab_addr = (uint64_t)ptr & ~0xFFFULL;
		SlabMeta* s = (SlabMeta*)(slab_addr + size * SLAB_COUNT);
		s->refcnt--;
		count--;
		uint32_t index = ((uint64_t)ptr - slab_addr) / size;
		s->bufctl[index] = s->s_free;
		s->s_free = index;
		if (s->refcnt == SLAB_COUNT - 1) { // full -> partial
			if (s == full) {
				full = (SlabMeta*)s->next;
				if (full)
					full->prev = nullptr;
			}
			else {
				if (s->prev)
					((SlabMeta*)s->prev)->next = s->next;
				if (s->next)
					((SlabMeta*)s->next)->prev = s->prev;
			}
			s->next = partial;
			if (partial)
				partial->prev = s;
			s->prev = nullptr;
			partial = s;
		}
		else if (s->refcnt == 0) { // partial -> empty
			if (s == partial) {
				partial = (SlabMeta*)s->next;
				if (partial)
					partial->prev = nullptr;
			}
			else {
				if (s->prev)
					((SlabMeta*)s->prev)->next = s->next;
				if (s->next)
					((SlabMeta*)s->next)->prev = s->prev;
			}
			s->next = empty;
			if (empty)
				((SlabMeta*)empty)->prev = s;
			s->prev = nullptr;
			empty = s;
		}
	}
protected:
	NewObject() {}
	virtual ~NewObject() {}
public:
	void* operator new(size_t) {
		return slab_alloc();
	}
	void operator delete(void* ptr) {
		slab_free(ptr);
	}
	static void* get(uint64_t index) {
		if (virt_page_allocator->get_pa((based_addr + index * sizeof(void*)) & ~0xFFFULL) == ~0ULL) {
			return nullptr; // 아직 할당되지 않은 인덱스
		}
		return ((void**)based_addr)[index];
	}
	volatile uint64_t state; // 0 = free, 1 = used 나머지 비트는 자유롭게 사용 가능 다른 구현과의 통일성을 위해 유지
	uint64_t id;
	static uint64_t max() { return new_id; }
	static uint64_t get_count() { return count; }
};
#else
template<uint64_t based_addr, uint64_t size, void(*init)(void*), void(*destroy)(void*)>
class NewObject {
private:
	inline static uint64_t count = 0;
	inline static uint64_t biggest = 0;
protected:
	NewObject() {}
	virtual ~NewObject() {}
public:
	volatile uint64_t state;
	uint64_t id;
	void* operator new(size_t) {
		uint64_t result = based_addr;
		uint64_t index = 0;
		while (((volatile NewObject*)result)->state & 1) {
			result += size;
			index++;
		}
		((volatile NewObject*)result)->state = 1;
		((volatile NewObject*)result)->id = index;
		if (index >= biggest) {
			biggest = index + 1; // biggest = 지금까지 쓴 최대 슬롯 수
			if (init)
				init((void*)result);
		}
		count++;
		return (void*)result;
	}
	void operator delete(void* ptr) {
		volatile NewObject* p = (volatile NewObject*)ptr;
		p->state = 0;
		count--;
	}
	static void* get(uint64_t index) {
		if (virt_page_allocator->get_pa((based_addr + index * size) & ~0xFFFULL) == ~0ULL)
			return nullptr;
		if (!(((volatile NewObject*)(based_addr + index * size))->state & 1))
			return nullptr;
		return (void*)(based_addr + index * size);
	}
	static uint64_t max() { return biggest; }
	static uint64_t get_count() { return count; }
};
#endif

#endif // __NEW_H__