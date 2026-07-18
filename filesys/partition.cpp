#include "filesys/partition.h"
#include "util/memory.h"
#include "filesys/FAT32.h"
#include "util/util.h"
uint64_t max_partition_index = 0;
void* Partition::operator new(size_t size) noexcept {
	if (size > PARTITIONSTRUCT_SIZE) return nullptr;
	uint64_t addr = PARTITION_QUEUE_BASE;
	uint64_t index = 0;
	while ((((Partition*)addr)->flags & 0x1) != 0) {
		addr += PARTITIONSTRUCT_SIZE;
		index++;
	}
	((Partition*)addr)->flags |= 0x1;
	max_partition_index = (index > max_partition_index) ? index : max_partition_index;
	return (void*)addr;
}

void Partition::operator delete(void* ptr) {
	Partition* p = (Partition*)ptr;
	p->flags &= ~0x1;
	uint64_t index = ((uint64_t)p - PARTITION_QUEUE_BASE) / PARTITIONSTRUCT_SIZE;
	if (index == max_partition_index) {
		// 최댓값 갱신
		while (max_partition_index > 0) {
			Partition* check_part = (Partition*)(PARTITION_QUEUE_BASE + PARTITIONSTRUCT_SIZE * (max_partition_index - 1));
			max_partition_index--;
			if (check_part->flags & 0x1) {
				break;
			}
		}
	}
}

void Partition::init() {
	uint8_t boot_sector[512];
	this->partitioner->read((Partitioner::PartitionInfo&)this->data, 0, boot_sector, 512);
	bool is_fat32 = (strncmp((char*)&boot_sector[0x52], "FAT32   ", 8) == 0);
	if(is_fat32) {
		// FAT32 파티션 초기화
		PartitionInfo saved_info = this->data;
		this->~Partition();
		::new (this) FAT32(saved_info, partitioner);
		FAT32* fat32_part = (FAT32*)this;
		fat32_part->init();
		return;
	}
}