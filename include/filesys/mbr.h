#ifndef __MBR_H__
#define __MBR_H__

#include "filesys/partitioner.h"
class MBRPartitioner : public Partitioner {
private:
	struct MBRPartitionEntry {
		uint8_t status;
		uint8_t chs_first[3];
		uint8_t type;
		uint8_t chs_last[3];
		uint32_t lba_first;
		uint32_t sector_count;
	} __attribute__((packed));
	struct MBR {
		uint8_t boot_code[446];
		MBRPartitionEntry partitions[4];
		uint16_t signature;
	} __attribute__((packed));
	MBR mbr;
public:
	void init(Disk* disk) override;
	bool read(PartitionInfo& pinfo, uint64_t addr, void* buffer, uint64_t size) override;
};

#endif // __MBR_H__