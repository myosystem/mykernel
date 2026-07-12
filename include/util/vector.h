#ifndef __VECTOR_H__
#define __VECTOR_H__
#include "util/size.h"
#include "mm/allocator"
#include "debug/log.h"
#include "util/util.h"
template <typename T>
class vector {
private:
    void* front_page;
    void* back_page;
    size_t new_index;
    size_t arr_size;
public:
    vector() {
        front_page = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
        back_page = front_page;
        *((void**)back_page) = nullptr; // 다음 페이지 포인터 초기화 필수!
        new_index = 0;
        arr_size = 0;
    }
    vector(vector&& other) {
        front_page = other.front_page;
        back_page = other.back_page;
        new_index = other.new_index;
        arr_size = other.arr_size;
        other.front_page = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
        other.back_page = other.front_page;
        *((void**)other.back_page) = nullptr;
        other.new_index = 0;
        other.arr_size = 0;
    }
    vector(const vector& other) {
        front_page = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
        back_page = front_page;
        *((void**)back_page) = nullptr;
        new_index = 0;
        arr_size = 0;
        for (size_t i = 0; i < other.arr_size; i++)
            push_back(const_cast<vector&>(other)[i]);
    }
    ~vector() {
        void* curr = front_page;
        while (curr != nullptr) {
            void* next = *((void**)curr);
            phy_page_allocator->put_page((uint64_t)curr - HHDM_BASE);
            curr = next;
        }
    }
    vector& operator=(const vector& other) {
        if (this == &other) return *this;
        // 기존 페이지 해제
        void* curr = front_page;
        while (curr != nullptr) {
            void* next = *((void**)curr);
            phy_page_allocator->put_page((uint64_t)curr - HHDM_BASE);
            curr = next;
        }
        // 새로 복사
        front_page = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
        back_page = front_page;
        *((void**)back_page) = nullptr;
        new_index = 0;
        arr_size = 0;
        for (size_t i = 0; i < other.arr_size; i++)
            push_back(const_cast<vector&>(other)[i]);
        return *this;
    }
    void push_back(const T& item) {
        // 8바이트 정렬을 고려한 오프셋 계산 (선택 사항이지만 권장)
        size_t aligned_size = (sizeof(T) + 7) & ~7;

        if (new_index + aligned_size > PageSize - 8) {
            void* new_page = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
            *((void**)new_page) = nullptr;   // 새 페이지의 next 초기화
            *((void**)back_page) = new_page; // 현재 페이지에 새 페이지 연결
            back_page = new_page;
            new_index = 0;
        }

        ::new ((void*)((uint8_t*)back_page + new_index + 8)) T(item);
        new_index += aligned_size;
        arr_size++;
    }
    void erase(size_t index) {
        if (index >= arr_size) {
            uart_print("vector error!!");
            __asm__ __volatile__("hlt");
        }
        size_t aligned_size = (sizeof(T) + 7) & ~7;
        size_t items_per_page = (PageSize - 8) / aligned_size;

        // 마지막 요소로 덮어쓰기
        if (index != arr_size - 1) {
            (*this)[index] = (*this)[arr_size - 1];
        }
        arr_size--;

        // new_index 업데이트
        if (new_index >= aligned_size) {
            new_index -= aligned_size;
        }
        else if (back_page != front_page) {
            // 현재 페이지가 비었으니 이전 페이지로
            void* prev_page = front_page;
            while (*((void**)prev_page) != back_page) {
                prev_page = *((void**)prev_page);
            }
            phy_page_allocator->put_page((uint64_t)back_page - HHDM_BASE);
            back_page = prev_page;
            *((void**)back_page) = nullptr;
            // 이전 페이지는 꽉 찬 상태였으므로
            new_index = items_per_page * aligned_size;
        }
        else {
            new_index = 0;
        }
    }
    T& operator[](size_t index) {
        if (index >= arr_size) {
			return *((T*)nullptr); // 범위 초과 시 nullptr 반환
		}
        size_t aliged_size = (sizeof(T) + 7) & ~7;
        size_t items_per_page = (PageSize - 8) / aliged_size;
        size_t page_index = index / items_per_page;
        size_t item_index = index % items_per_page;
        void* curr = front_page;
        for (size_t i = 0; i < page_index; i++) {
            curr = *((void**)curr);
            if (curr == 0) {
                uart_print("vector error!!");
                __asm__ __volatile__("hlt");
            }
        }
        if (curr == back_page && item_index * aliged_size >= new_index) {
            uart_print("vector error!!");
            __asm__ __volatile__("hlt");
        }
		T* result = (T*)((uint8_t*)curr + 8 + item_index * aliged_size);
        return *result;
    }
    size_t size() const {
        return arr_size;
    }
};
class pointer_vector {
private:
    struct Page {
        void* slots[511]; // 4088 바이트
        Page* next_level; // 마지막 8바이트를 다음 관리용으로 사용 (선택 사항)
    };
	Page* front_page;
	Page* back_page;
	size_t back_index;  // back_page에서 다음 빈 슬롯 인덱스
    size_t size;        // 전체 요소 수
	void* nullresult = nullptr; // nullptr 반환용
public:
    pointer_vector() {
        front_page = (Page*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
        back_page = front_page;
        back_page->next_level = nullptr; // 다음 페이지 포인터 초기화 필수!
        back_index = 0;
        size = 0;
    }
    uint64_t push_back(void* ptr) {
        for (size_t i = 0; i < size; i++) {
            if ((*this)[i] == nullptr) {
                (*this)[i] = ptr;
                return i;
            }
        }
        back_page->slots[back_index++] = ptr;
        size++;
        if (back_index >= 511) {
            Page* new_page = (Page*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
            new_page->next_level = nullptr;
            back_page->next_level = new_page;
            back_page = new_page;
            back_index = 0;
        }
        return size - 1;
    }
    void erase(size_t index) {
		(*this)[index] = nullptr;
        if(index == size - 1) {
            while(size > 0 && (*this)[size - 1] == nullptr) {
                size--;
                back_index--;
                if (back_index == (size_t)-1) {
                    if (back_page == front_page) {
                        back_index = 0; // 더 내려갈 페이지 없음
                        break;
                    }
                    // 이전 페이지 탐색
                    Page* prev_page = front_page;
                    while (prev_page->next_level != back_page) {
                        prev_page = prev_page->next_level;
                    }
                    phy_page_allocator->put_page((uint64_t)back_page - HHDM_BASE);
                    back_page = prev_page;
                    back_page->next_level = nullptr;
                    back_index = 510; // 이전 페이지의 마지막 유효 슬롯
                }
            }
		}
    }
    void*& operator[](size_t index) {
        size_t page_index = index / 511;
        size_t slot_index = index % 511;
        Page* curr = front_page;
        for (size_t i = 0; i < page_index; i++) {
            curr = curr->next_level;
            if (curr == nullptr) {
				return nullresult; // 범위 초과 시 nullptr 반환
            }
        }
        if (curr == back_page && slot_index >= back_index) {
			return nullresult; // 범위 초과 시 nullptr 반환
        }
		return curr->slots[slot_index];
	}
    size_t get_size() const {
        return size;
	}
};
#endif // __VECTOR_H__