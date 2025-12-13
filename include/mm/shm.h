#ifndef __SHAREDMEM_H__
#define __SHAREDMEM_H__
#include "util/size.h"
#include "mm/allocator"
#define SHM_NAME_MAX_LEN 64
#define SHM_QUEUE_BASE 0xFFFF820000000000ULL
typedef struct IndexPage {
	struct IndexPage* next;
	uint64_t pages[PageSize / sizeof(uint64_t) - 1];
}__attribute__((packed)) IndexPage;

class SharedMem {
private:
	char name[SHM_NAME_MAX_LEN];
	uint32_t id;
	uint64_t size;
	uint32_t page_count;
	IndexPage* index_page_head;
	bool is_unlinked;
	int owner_pid;
	int ref_count;
	bool state;
	
	volatile uint32_t shm_lock = 0;
	SharedMem* next;
public:
	SharedMem() = default;
	~SharedMem() = default;
	void* operator new(size_t size);
	void operator delete(void* ptr);
	
};
#endif // __SHAREDMEM_H__