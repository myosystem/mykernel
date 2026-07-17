#ifndef __HEAPTREE_H__
#define __HEAPTREE_H__
#include "util/size.h"
template<typename T>
class HeapTree {
	private:
	T* data;
	size_t size;
	void swap(size_t i, size_t j) {
		T temp = data[i];
		data[i] = data[j];
		data[j] = temp;
	}
	void heapify_up(size_t index) {
		while (index > 0) {
			size_t parent = (index - 1) / 2;
			if (data[index] < data[parent]) {
				swap(index, parent);
				index = parent;
			} else {
				break;
			}
		}
	}
	void heapify_down(size_t index) {
		while (true) {
			size_t left = 2 * index + 1;
			size_t right = 2 * index + 2;
			size_t smallest = index;
			if (left < size && data[left] < data[smallest]) {
				smallest = left;
			}
			if (right < size && data[right] < data[smallest]) {
				smallest = right;
			}
			if (smallest != index) {
				swap(index, smallest);
				index = smallest;
			} else {
				break;
			}
		}
	}
public:
	T nullresult{};
	HeapTree(void* arr) : data((T*)arr), size(0) {
	}
	~HeapTree() {
	}
	bool push(const T& value) {
		data[size] = value;
		heapify_up(size);
		size++;
		return true;
	}
	bool pop() {
		if (size == 0) return false;
		data[0] = data[size - 1];
		size--;
		heapify_down(0);
		return true;
	}
	bool remove(size_t index) {
		if (index >= size) return false;

		// 마지막 요소로 교체
		data[index] = data[size - 1];
		size--;

		if (index >= size) return true; // 마지막 요소였으면 그냥 끝

		// 위아래로 heapify
		heapify_up(index);
		heapify_down(index);

		return true;
	}

	// timer_id로 찾아서 제거
	bool remove_by(auto predicate) {
		for (size_t i = 0; i < size; i++) {
			if (predicate(data[i])) {
				return remove(i);
			}
		}
		return false;
	}
	const T& top() const {
		if (size == 0) return nullresult;
		return data[0];
	}
	bool isEmpty() const {
		return size == 0;
	}
};
#endif /*__HEAPTREE_H__*/