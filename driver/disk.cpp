#include "driver/disk.h"
#include "util/size.h"
#include "util/memory.h"
#include "mm/allocator"

Disk::Disk(volatile HBA_PORT* port) : port(port) {
	buffer = (uint8_t*)phy_page_allocator->alloc_phy_page() + MMIO_BASE;
	virt_page_allocator->alloc_virt_page((uint64_t)(buffer), (uint64_t)(buffer) - MMIO_BASE, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
}
Disk::~Disk() {
	virt_page_allocator->free_virt_page((uint64_t)buffer);
    phy_page_allocator->free_phy_page((uint64_t)buffer - MMIO_BASE);
}
uint8_t Disk::operator[](uint64_t addr) {
	uint64_t page = addr / 0x1000;
	addr = addr % 0x1000;
	if(!ready || page != index) {
		ahci_read(port, page * (0x1000 / SECTOR_SIZE), 0x1000 / SECTOR_SIZE, buffer - MMIO_BASE);
		index = (uint32_t)page;
		ready = true;
	}
	return buffer[addr];
}
void Disk::read_bytes(uint64_t addr, void* buf, uint64_t size) {
    uint8_t* out = (uint8_t*)buf;

    while (size > 0) {
        uint64_t page = addr / 0x1000;
        uint64_t offset = addr % 0x1000;
        uint64_t remain_in_page = 0x1000 - offset;

        // 이번에 복사할 크기: 
        // 페이지 끝까지 남은 양 vs 전체 남은 요청량 중 작은 것
        uint64_t copy_size = (size < remain_in_page) ? size : remain_in_page;

        // 1. 캐시 미스면 로딩 (operator[]의 로직을 여기로 가져옴)
        if (!ready || page != index) {
            ahci_read(port, page * (0x1000 / SECTOR_SIZE), 0x1000 / SECTOR_SIZE, buffer - MMIO_BASE);
            index = (uint32_t)page;
            ready = true;
        }

        // 2. 뭉텅이 복사 (Memcpy) - 이게 핵심!
        memcpy(out, buffer + offset, copy_size);

        // 3. 포인터 이동
        size -= copy_size;
        addr += copy_size;
        out += copy_size;
    }
}
void* Disk::operator new(size_t size) {
    Disk* result = (Disk*)DISK_QUEUE_BASE;
    uint64_t index = 0;
    while (result->state == 1) {
        result++;
        index++;
    }
    result->state = 1;
    result->disk_id = index;
    return result;
}
void Disk::operator delete(void* ptr) {
    Disk* p = (Disk*)ptr;
    p->state = 0;
}