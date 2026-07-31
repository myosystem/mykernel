#ifndef __PAGE_H__
#define __PAGE_H__

#include "mm/allocator"

class Page {
    void* page = nullptr;   // HHDM 가상주소
    void addref() { if (page) phy_page_allocator->get_page((uint64_t)page - HHDM_BASE); }
    void release() { if (page) { phy_page_allocator->put_page((uint64_t)page - HHDM_BASE); page = nullptr; } }
public:
    Page() { page = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE); }   // 새 페이지, rc=1
    Page(const Page& o) : page(o.page) { addref(); }                              // 복사 = rc++
    Page& operator=(const Page& o) { if (this != &o) { release(); page = o.page; addref(); } return *this; }
    Page(Page&& o) : page(o.page) { o.page = nullptr; }                           // 이동 = rc 그대로
    Page& operator=(Page&& o) { if (this != &o) { release(); page = o.page; o.page = nullptr; } return *this; }
    ~Page() { release(); }                                                        // rc--, 0되면 해제

    uint8_t* data() { return (uint8_t*)page; }        // 원시 접근
    static void* operator new(size_t) = delete;
    static void* operator new[](size_t) = delete;
    static void  operator delete(void*) = delete;
    static void  operator delete[](void*) = delete;
	operator void* () const { return page; }
};

#endif // __PAGE_H__