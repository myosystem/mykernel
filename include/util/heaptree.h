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
	HeapTree(void* arr) : size(0) {
		data = (T*)arr;
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
	const T& top() const {
		if (size == 0) return *(T*)0;
		return data[0];
	}
	bool isEmpty() const {
		return size == 0;
	}
};
#endif /*__HEAPTREE_H__*/