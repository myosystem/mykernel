#include "util/vector.h"
#include "mm/allocator"

template <typename T>
vector<T>::vector() {
	front_page = phy_page_allocator->alloc_phy_page() + HHDM_BASE;
	back_page = front_page;
	*((void**)back_page) = nullptr; // 다음 페이지 포인터 초기화 필수!
	new_index = 0;
}
template <typename T>
vector<T>::~vector() {
	void* curr = front_page;
	while (curr != nullptr) {
		void* next = *((void**)curr);
		phy_page_allocator->free_phy_page((uint64_t)curr - HHDM_BASE);
		curr = next;
	}
}
template <typename T>
void vector<T>::push_back(const T& item) {
	// 8바이트 정렬을 고려한 오프셋 계산 (선택 사항이지만 권장)
	size_t aligned_size = (sizeof(T) + 7) & ~7;

	if (new_index + aligned_size > PageSize - 8) {
		void* new_page = phy_page_allocator->alloc_phy_page() + HHDM_BASE;
		*((void**)new_page) = nullptr;   // 새 페이지의 next 초기화
		*((void**)back_page) = new_page; // 현재 페이지에 새 페이지 연결
		back_page = new_page;
		new_index = 0;
	}

	*((T*)((uint8_t*)back_page + new_index + 8)) = item;
	new_index += aligned_size;
}
template <typename T>
void vector<T>::erase(size_t index) {

}
template <typename T>
T& vector<T>::operator[](size_t index) {
	return 0;
}