#ifndef __NEW_H__
#define __NEW_H__
#include "util/size.h"

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
public:
	NewObject() {

	}
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
};
#endif

#endif // __NEW_H__