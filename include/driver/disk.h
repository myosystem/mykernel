#ifndef __DISK_H__
#define __DISK_H__
#include "util/size.h"
#include "driver/ahci.h"
#define SECTOR_SIZE 512
#define DISK_QUEUE_BASE 0xFFFF828000000000ULL
class Disk {
private:
	uint32_t index = 0;
	volatile HBA_PORT* port;
	uint32_t disk_id;
	uint8_t* buffer;
	bool ready = false;
	bool state = false;
public:
	Disk(volatile HBA_PORT* port);
	~Disk();
	uint8_t operator[](uint64_t addr);
	void read_bytes(uint64_t addr, void* buf, uint64_t size);
	void* operator new(size_t size);
	void operator delete(void* ptr);
};
#endif /*__DISK_H__*/