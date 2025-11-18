#ifndef __DISK_H__
#define __DISK_H__
#include "util/size.h"
#include "driver/ahci.h"
#define SECTOR_SIZE 512
class Disk {
private:
	uint32_t index = 0;
	volatile HBA_PORT* port;
	uint8_t* buffer;
	bool ready = false;
public:
	Disk(volatile HBA_PORT* port, uint8_t* buffer);
	uint8_t operator[](uint64_t addr);
	void read_bytes(uint64_t addr, void* buf, uint64_t size);
};
#endif /*__DISK_H__*/