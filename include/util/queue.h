#ifndef __QUEUE_H__
#define __QUEUE_H__
#include "util/size.h"
#include "mm/allocator"
#include "debug/log.h"
template <typename T>
class queue {
	public:
    constexpr queue() {
        front_page = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
        back_page = front_page;
        *((void**)back_page) = nullptr; // 다음 페이지 포인터 초기화 필수!
        front_index = 0;
        back_index = 0;
    }
    constexpr ~queue() {
        volatile void* curr = front_page;
        while (curr != nullptr) {
            void* next = *((void**)curr);
            phy_page_allocator->put_page((uint64_t)curr - HHDM_BASE);
            curr = next;
        }
    }
    constexpr void enqueue(const T& item) {
        // 8바이트 정렬을 고려한 오프셋 계산 (선택 사항이지만 권장)
        size_t aligned_size = (sizeof(T) + 7) & ~7;

        if (back_index + aligned_size > PageSize - 8) {
            void* new_page = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
            *((void**)new_page) = nullptr;   // 새 페이지의 next 초기화
            *((void**)back_page) = new_page; // 현재 페이지에 새 페이지 연결
            back_page = new_page;
            back_index = 0;
        }

        *((T*)((uint8_t*)back_page + back_index + 8)) = item;
        back_index += aligned_size;
    }
    constexpr T dequeue() {
        // isEmpty() 체크는 호출하는 쪽이나 여기서 확실히!
        T item = *((T*)((uint8_t*)front_page + front_index + 8));
        size_t aligned_size = (sizeof(T) + 7) & ~7;
        front_index += aligned_size;

        // 만약 현재 페이지를 다 읽었고, 다음 페이지가 있다면 교체
        // (front_page == back_page)인 경우는 아직 다음 페이지가 없는 상태임
        if (front_index + aligned_size > PageSize - 8 && front_page != back_page) {
            volatile void* temp_page = front_page;
            front_page = *((void**)front_page);
            phy_page_allocator->put_page((uint64_t)temp_page - HHDM_BASE);
            front_index = 0;
        }
        else if (front_page == back_page && front_index == back_index) {
            // "어? 나랑 꼬리랑 만났네? 큐 비었네?"
            // 아까운 페이지 버리지 말고, 인덱스만 0으로 돌려서 재사용하자!
            front_index = 0;
            back_index = 0;
        }
        return item;
    }
    constexpr bool isEmpty() const {
        return (front_page == back_page) && (front_index == back_index);
    }
    constexpr T* peek_back() {
        if (isEmpty()) return nullptr;
        size_t aligned_size = (sizeof(T) + 7) & ~7;
        size_t last_index = back_index - aligned_size;
        return (T*)((uint8_t*)back_page + last_index + 8);
    }
private:
    volatile void* front_page;
    volatile void* back_page;
    volatile size_t front_index;
    volatile size_t back_index;
};

#endif // __QUEUE_H__