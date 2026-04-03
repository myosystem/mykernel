#ifndef __PARTITIONER_H__
#define __PARTITIONER_H__
#include "util/size.h"
class Disk;
#define PARTITIONER_SLOT_SIZE 4096
#define PARTITIONER_QUEUE_BASE 0xFFFF830000000000ULL



class Partitioner {
protected:
	Disk* master_disk;
public:
	uint8_t flags;
	struct PartitionInfo {
		uint8_t  type_guid[16];
		uint64_t first_lba;
		uint64_t last_lba;
		uint64_t attrs;
		uint16_t name[36]; // UTF-16LE 이름
	} __attribute__((packed));
	Partitioner() {}
	virtual void init(Disk* disk) = 0;
	virtual bool read(PartitionInfo& pinfo, uint64_t addr, void* buffer, uint64_t size) = 0;
	virtual ~Partitioner() {}
	static Partitioner* create_default();
	static void* operator new(size_t size) noexcept {
		if (size > PARTITIONER_SLOT_SIZE) return nullptr;
		uint64_t addr = PARTITIONER_QUEUE_BASE;
		addr += PARTITIONER_SLOT_SIZE; //0번 파티션은 부트
		while ((((Partitioner*)addr)->flags & 0x1) != 0) {
			addr += PARTITIONER_SLOT_SIZE;
		}
		((Partitioner*)addr)->flags |= 0x1;
		return (void*)addr;
	}

	static void operator delete(void* ptr) {
		if (ptr) ((Partitioner*)ptr)->flags &= ~0x1;
		// 페이지 해제하지 않음 (깃발만 내림)
	}
};
#endif