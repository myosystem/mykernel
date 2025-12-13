#include "mm/shm.h"

void* SharedMem::operator new(size_t size) {
	SharedMem* result = (SharedMem*)SHM_QUEUE_BASE;
	uint64_t index = 0;
	while (result->state == 1) {
		result++;
		index++;
	}
	result->state = 1;
	result->id = index;
	return result;
}
void SharedMem::operator delete(void* ptr) {
	SharedMem* p = (SharedMem*)ptr;
	p->state = 0;
}