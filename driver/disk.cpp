#include "driver/disk.h"
#include "util/size.h"
#include "util/memory.h"
#include "mm/allocator"
#include "arch/pci.h"
#include "driver/ahci.h"
#include "driver/nvme.h"
#include "util/util.h"
#include "util/vector.h"
struct UUIDMapping {
    uint8_t  uuid[16];   // GPT GUID 또는 FAT32 볼륨 ID
    uint32_t number;     // #번호
    char     alias[12];  // C: 같은 alias (없으면 빈 문자열)
    uint8_t  uuid_type;  // 0 = FAT32 볼륨 ID, 1 = GPT GUID
};

vector<UUIDMapping>* uuid_map;
Disk::Disk(uint16_t bus, uint16_t slot, uint16_t func, uint32_t port_or_ns) : pci_bus(bus), pci_slot(slot), pci_func(func), port_or_ns(port_or_ns) {
    for (int i = 0; i < DISK_BUFFER_COUNT; i++) {
		DiskBuffer buf;
		buf.dirty = false;
		buf.ready = false;
        buf.buf = (uint8_t*)phy_page_allocator->alloc_phy_page() + MMIO_BASE;
        virt_page_allocator->alloc_virt_page((uint64_t)(buf.buf), (uint64_t)(buf.buf)-MMIO_BASE, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
        buffers.push_back(buf);
    }
}
Disk::~Disk() {
    for (size_t i = 0;i < buffers.size(); i++) {
		auto& buf = buffers[i];
		virt_page_allocator->free_virt_page((uint64_t)buf.buf);
		phy_page_allocator->put_page((uint64_t)buf.buf - MMIO_BASE);
	}
}
void Disk::cleanup() {
    for (size_t i = 0; i < buffers.size(); i++) {
        auto& buf = buffers[i];
        if (buf.dirty) {
            write_sector(buf.index * (0x1000 / SECTOR_SIZE), 0x1000 / SECTOR_SIZE, buf.buf - MMIO_BASE);
            buf.dirty = false;
        }
    }
}
/*
uint8_t Disk::operator[](uint64_t addr) {
	uint64_t page = addr / 0x1000;
	addr = addr % 0x1000;
	if(!ready || page != index) {
		read_sector(page * (0x1000 / SECTOR_SIZE), 0x1000 / SECTOR_SIZE, buffer - MMIO_BASE);
		index = (uint32_t)page;
		ready = true;
	}
	return buffer[addr];
}
*/
void Disk::init() {
    uint16_t vendor_id = pci_read16(this->pci_bus, this->pci_slot, this->pci_func, 0x00);
    if (vendor_id == 0xFFFF) return;
    uint8_t class_code = pci_read8(this->pci_bus, this->pci_slot, this->pci_func, 0x0B);
    uint8_t subclass = pci_read8(this->pci_bus, this->pci_slot, this->pci_func, 0x0A);
    this->~Disk();
    if (class_code == 0x01 && subclass == 0x06) {
        // [AHCI Controller]
        // 기존 메모리 위치에 AhciDisk 덮어쓰기
        //::new (this) AHCIDisk(this->pci_bus, this->pci_slot, this->pci_func, this->port_or_ns);

        // 드라이버 초기화 (VTable이 바뀌었으므로 AhciDisk::driver_init 호출됨)
        //((AHCIDisk*)this)->init();
    }
    else if (class_code == 0x01 && subclass == 0x08) {
        ::new (this) NVMeDisk(this->pci_bus, this->pci_slot, this->pci_func, this->port_or_ns);
        ((NVMeDisk*)this)->init();
    }
    else {
        // [Unknown] 다시 기본 Disk로 복구
    }
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
		uint8_t* buffer = nullptr;
		for (size_t i = 0; i < buffers.size(); i++) {
            auto& buf = buffers[i];
            if(buf.ready && buf.index == page) {
				buffer = buf.buf;
				break;
            }
        }
        if (!buffer) {
			if (buffer_index >= buffers.size()) buffer_index = 0; // 원형 버퍼 방식
			auto& buf = buffers[buffer_index];
            if (buf.dirty) {
				write_sector(buf.index * (0x1000 / SECTOR_SIZE), 0x1000 / SECTOR_SIZE, buf.buf - MMIO_BASE);
            }
            buffer = buf.buf;
			int num = read_sector(page * (0x1000 / SECTOR_SIZE), 0x1000 / SECTOR_SIZE, buffer - MMIO_BASE);
            uart_print("code ");
            uart_print(num);
            uart_print("\n");
            uart_print("disk id ");
            uart_print(disk_id);
            uart_print("\n");
            if (num) {
                // 읽기 실패 처리 (예: 디스크 오류)
                uart_print("Disk read error at page ");
                uart_print_hex(page);
                uart_print("\nError code ");
                uart_print(num);
                uart_print("\n");
				//memset(buffer, 0, 0x1000); // 실패 시 버퍼를 0으로 초기화
            }
			buf.index = page;
			buf.ready = true;
			buf.dirty = false;
            buffer_index++;
        }

        // 2. 뭉텅이 복사 (Memcpy) - 이게 핵심!
        memcpy(out, buffer + offset, copy_size);

        // 3. 포인터 이동
        size -= copy_size;
        addr += copy_size;
        out += copy_size;
    }
}
void Disk::write_bytes(uint64_t addr, const void* buf, uint64_t size) {
    const uint8_t* in = (const uint8_t*)buf;

    while (size > 0) {
        uint64_t page = addr / 0x1000;
        uint64_t offset = addr % 0x1000;
        uint64_t copy_size = (size < (0x1000 - offset)) ? size : (0x1000 - offset);

        // 1. 캐시 탐색 및 로딩 (작성하신 read_bytes 로직과 동일)
        uint8_t* target_entry = nullptr;
        for (size_t i = 0;i < buffers.size(); i++) {
            auto& entry = buffers[i];
            if (entry.ready && entry.index == page) {
                target_entry = entry.buf;
				entry.dirty = true; // 이미 캐시에 있으면 바로 더티 마킹
                break;
            }
        }

        if (!target_entry) {
            // 원형 버퍼 교체 로직
			if (buffer_index >= buffers.size()) buffer_index = 0; // 원형 버퍼 방식
			auto& entry = buffers[buffer_index];
			if (entry.dirty) {
				write_sector(entry.index * (0x1000 / SECTOR_SIZE), 0x1000 / SECTOR_SIZE, entry.buf - MMIO_BASE);
			}
			target_entry = entry.buf;
			read_sector(page * (0x1000 / SECTOR_SIZE), 0x1000 / SECTOR_SIZE, target_entry - MMIO_BASE);
			entry.index = page;
			entry.ready = true;
			entry.dirty = true;
            buffer_index++;
        }

        // 2. 데이터 수정 및 Dirty 마킹
        memcpy(target_entry + offset, in, copy_size);

        size -= copy_size;
        addr += copy_size;
        in += copy_size;
    }
}
void* Disk::operator new(size_t size) {
	uint64_t mem = DISK_QUEUE_BASE;
    uint64_t index = 0;
    while (((Disk*)(mem))->state == 1) {
		mem += DISKSTRUCT_SIZE;
        index++;
    }
    ((Disk*)(mem))->state = 1;
    ((Disk*)(mem))->disk_id = index;
    return (void*)mem;
}
void Disk::operator delete(void* ptr) {
    Disk* p = (Disk*)ptr;
    p->state = 0;
}
int Disk::read_sector(uint64_t lba, uint32_t count, void* buf) {
	return -100; // 기본 Disk는 지원 안 함
}
int Disk::write_sector(uint64_t lba, uint32_t count, const void* buf) {
    return -100; // 기본 Disk는 지원 안 함
}