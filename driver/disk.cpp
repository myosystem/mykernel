#include "driver/disk.h"
#include "util/size.h"

Disk::Disk(volatile HBA_PORT* port, uint8_t* buffer) : port(port), buffer(buffer) {
}
uint8_t Disk::operator[](uint64_t addr) {
	uint64_t page = addr / 0x1000;
	addr = addr % 0x1000;
	if(!ready || page != index) {
		ahci_read(port, page * (0x1000 / SECTOR_SIZE), 0x1000 / SECTOR_SIZE, buffer);
		index = (uint32_t)page;
		ready = true;
	}
	return buffer[addr];
}
void Disk::read_bytes(uint64_t addr, void* buf, uint64_t size) {
	for (uint64_t i = 0; i < size; i++) {
		((uint8_t*)buf)[i] = (*this)[addr + i];
	}
}