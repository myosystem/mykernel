#ifndef __DISK_H__
#define __DISK_H__
#include "util/size.h"
#define SECTOR_SIZE 512
#define DISK_QUEUE_BASE 0xFFFF828000000000ULL
#define DISKSTRUCT_SIZE 0x400
class Disk {
protected:
	uint8_t pci_bus, pci_slot, pci_func;
private:
	uint32_t index = 0;
	uint32_t disk_id;
	uint8_t* buffer;
	bool ready = false;
	bool state;
public:
	Disk(uint8_t bus, uint8_t slot, uint8_t func);
	virtual ~Disk();
	virtual void init();
	virtual int read_sector(uint64_t lba, uint32_t count, void* buf);
	uint8_t operator[](uint64_t addr);
	void read_bytes(uint64_t addr, void* buf, uint64_t size);
	void* operator new(size_t size);
	void operator delete(void* ptr);
};
#endif /*__DISK_H__*/