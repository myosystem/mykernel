#include "mm/allocator"
#include "util/util.h"

PhysPageAllocator* phy_page_allocator;
VirtPageAllocator* virt_page_allocator;

alignas(PhysPageAllocator) static uint8_t phy_buf[sizeof(PhysPageAllocator)];
alignas(VirtPageAllocator) static uint8_t virt_buf[sizeof(VirtPageAllocator)];

void init_allocators(uint64_t* bitmap, uint64_t total_pages) {
    phy_page_allocator = new (phy_buf) PhysPageAllocator();
    phy_page_allocator->init(bitmap, total_pages);

    virt_page_allocator = new (virt_buf) VirtPageAllocator();
    virt_page_allocator->init(phy_page_allocator);
}