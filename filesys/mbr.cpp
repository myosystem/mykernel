#include "filesys/partitioner.h"
#include "filesys/mbr.h"
#include "driver/disk.h"
#include "filesys/gpt.h"
#include "mm/allocator"
#include "util/util.h"

// todo - MBR분석 코드 넣어야함
void MBRPartitioner::init(Disk* disk) {
	disk->read_bytes(0, &mbr, sizeof(MBR));
	if (mbr.partitions[0].type == 0xEE) {
		this->~MBRPartitioner();
		GPTPartitioner* new_gpt = ::new (this) GPTPartitioner();
		new_gpt->init(disk);
		return;
	}
}
bool MBRPartitioner::read(PartitionInfo& pinfo, uint64_t addr, void* buffer, uint64_t size) {
	uint64_t partition_size = (pinfo.last_lba - pinfo.first_lba + 1) * SECTOR_SIZE;

	if (addr + size > partition_size) {
		// uart_print("Error: Read out of partition bounds!\n");
		return false;
	}
	uint64_t start_addr = pinfo.first_lba * SECTOR_SIZE + addr;
	this->master_disk->read_bytes(start_addr, buffer, size);
	return true;
}