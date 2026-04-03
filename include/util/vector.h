#ifndef __VECTOR_H__
#define __VECTOR_H__
#include "util/size.h"
#include "mm/allocator"
#include "debug/log.h"
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
    ~vector() {
        void* curr = front_page;
        while (curr != nullptr) {
            void* next = *((void**)curr);
            phy_page_allocator->put_page((uint64_t)curr - HHDM_BASE);
            curr = next;
        }
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

        *((T*)((uint8_t*)back_page + new_index + 8)) = item;
        new_index += aligned_size;
        arr_size++;
    }
    void erase(size_t index) {
        if (index >= arr_size) {
            uart_print("vector error!!");
            __asm__ __volatile__("hlt");
        }
        // 삭제된 요소를 마지막 요소로 덮어쓰기
        (*this)[index] = (*this)[arr_size - 1];
        arr_size--;
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
        return ((T*)((uint64_t)curr + 8))[item_index];
    }
    size_t size() const {
        return arr_size;
    }
};

#endif // __VECTOR_H__