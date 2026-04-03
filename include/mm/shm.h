#ifndef __SHAREDMEM_H__
#define __SHAREDMEM_H__
#include "util/size.h"
#include "mm/allocator"
#include "util/vector.h"
#define SHM_NAME_MAX_LEN 64
#define SHARED_MEM_QUEUE_BASE 0xFFFF820000000000ULL
class SharedMem {
private:
    uint64_t id;
    uint64_t size;
    uint64_t page_count;
    bool is_unlinked;
    uint64_t owner_pid;
    uint64_t ref_count;
    uint64_t state;
    
    volatile uint32_t shm_lock = 0;
public:
    SharedMem(uint64_t owner_pid, uint64_t size);
    ~SharedMem();
    vector<uint64_t> phy_pages; // 실제 물리 페이지 주소들

    void* operator new(size_t size);
    void operator delete(void* ptr);
	uint64_t get_id() const { return id; }
	uint64_t get_size() const { return size; }
	friend SharedMem* get_shared_mem(uint64_t id);
};
SharedMem* get_shared_mem(uint64_t id);
#endif // __SHAREDMEM_H__