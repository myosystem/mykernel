#ifndef __GPT_H__
#define __GPT_H__
#include "util/size.h"
#include "filesys/partitioner.h"
#include "filesys/partition.h"
class GPTPartitioner : public Partitioner {
private:
    struct Gpt_header {
        char     signature[8];
        uint32_t revision;
        uint32_t header_size;
        uint32_t header_crc32;
        uint32_t reserved;
        uint64_t current_lba;
        uint64_t backup_lba;
        uint64_t first_usable_lba;
        uint64_t last_usable_lba;
        uint8_t  disk_guid[16];      // GUID는 바이트 배열로
        uint64_t part_entry_lba;
        uint32_t num_part_entries;
        uint32_t size_part_entry;
        uint32_t part_entry_crc32;
    } __attribute__((packed));

    struct Gpt_entry {
        uint8_t  type_guid[16];
        uint8_t  uniq_guid[16];
        uint64_t first_lba;
        uint64_t last_lba;
        uint64_t attrs;
        uint16_t name[36]; // UTF-16LE 이름
    } __attribute__((packed));
	Gpt_header gpt_header;
public:
	void init(Disk* disk) override;
    bool read(PartitionInfo& pinfo, uint64_t addr, void* buffer, uint64_t size) override;
    bool write(PartitionInfo& pinfo, uint64_t addr, const void* buffer, uint64_t size) override;
};
#endif