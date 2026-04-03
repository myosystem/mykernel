#ifndef __HANDLE_H__
#define __HANDLE_H__
#define SHARED_SLOT_SIZE 1024
#define MAX_SHARED_ITEMS 4096
#define HANDLE_QUEUE_BASE 0xFFFF838000000000ULL
#include "util/size.h"
class _handle {
protected:
    int ref_count;
    uint8_t flags;
public:
    _handle() : ref_count(1) {}
    virtual ~_handle() {}

    // [참조 카운팅]
    void grab() { ref_count++; }
    void release() {
        if (--ref_count <= 0) delete this;
    }

    // [핵심: 메모리 할당자]
    // 자식 클래스(File, SharedMem)가 new를 호출하면 이 함수가 실행됨
    static void* operator new(size_t size) noexcept {
        // 1. 사이즈 체크 (슬롯보다 크면 안 됨)
        if (size > SHARED_SLOT_SIZE) return nullptr;

        // 2. 빈 슬롯 찾기
        uint64_t addr = HANDLE_QUEUE_BASE;
        while ((((_handle*)addr)->flags & 0x1) != 0) {
            addr += SHARED_SLOT_SIZE;
        }
        ((_handle*)addr)->flags |= 0x1;
        return (void*)addr;
    }

    static void operator delete(void* ptr) {
        _handle* item = (_handle*)ptr;
		item->flags &= ~0x1;
    }
};

#endif // __HANDLE_H__