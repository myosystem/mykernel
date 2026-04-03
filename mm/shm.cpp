#include "mm/shm.h"
#include "util/memory.h"

void* SharedMem::operator new(size_t size) {
    SharedMem* result = (SharedMem*)SHARED_MEM_QUEUE_BASE;
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
SharedMem::SharedMem(uint64_t owner_pid, uint64_t size) : size(size), is_unlinked(false), owner_pid(owner_pid), ref_count(1) {
    page_count = (size + 4095) / 4096;
    for (uint64_t i = 0; i < page_count; i++) {
        phy_pages.push_back(0);
    }
}
SharedMem::~SharedMem() {

}
SharedMem* get_shared_mem(uint64_t id) {
    SharedMem* ptr = (SharedMem*)(SHARED_MEM_QUEUE_BASE + id * sizeof(SharedMem));
    if (ptr->state == 1) return ptr;
    return nullptr;
}