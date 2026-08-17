#ifndef __DISK_H__
#define __DISK_H__
#include "util/size.h"
#include "util/new.h"
#include "util/vector.h"
#include "kernel/kernel.h"
#define SECTOR_SIZE 512
#define DISK_QUEUE_BASE 0xFFFF828000000000ULL
#define DISKSTRUCT_SIZE 0x200
#define DISK_BUFFER_COUNT 512
struct DiskBuffer {
	uint8_t* buf;
	uint32_t index;
	bool ready;
	bool dirty;
};
class Disk : public NewObject<pml4_addr(PML4::DISK_HEAP), DISKSTRUCT_SIZE, nullptr, nullptr> {
protected:
	uint8_t  type;       // 1 = AHCI/SATA, 2 = NVMe, 3 = USB MSC ...
	uint16_t pci_bus;
	uint16_t pci_slot;
	uint16_t pci_func;
private:
	vector<DiskBuffer> buffers;
	uint32_t disk_id;
	uint32_t buffer_index; // 현재 버퍼 인덱스
	uint32_t busy;
public:
	Disk(uint16_t bus, uint16_t slot, uint16_t func, uint32_t port_or_ns);
	virtual ~Disk();
	virtual void init();
	virtual int read_sector(uint64_t lba, uint32_t count, void* buf);
	virtual int write_sector(uint64_t lba, uint32_t count, const void* buf);
	void cleanup();
	void read_bytes(uint64_t addr, void* buf, uint64_t size);
	void write_bytes(uint64_t addr, const void* buf, uint64_t size);
	uint32_t port_or_ns;
	bool operator==(boot_device_info_t device) const {
		return device.pci_bus == pci_bus && device.pci_slot == pci_slot && device.pci_func == pci_func && device.port_or_ns == port_or_ns;
	}
	bool is_vaild() const {
		return state & 0b1;
	}
};
extern vector<Disk*>* disks;
#endif /*__DISK_H__*/