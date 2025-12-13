#include "filesys/gpt.h"
#include "filesys/partition.h"
#include "util/memory.h"
#include "driver/disk.h"
#include "debug/log.h"
#include "util/util.h"

void GPTPartitioner::init(Disk* disk) {
    this->master_disk = disk;
	disk->read_bytes(1 * SECTOR_SIZE, &gpt_header, sizeof(gpt_header));
	uint32_t entry_size = gpt_header.size_part_entry;
	uint32_t count = gpt_header.num_part_entries;
	uint64_t table_lba = gpt_header.part_entry_lba;
	uint8_t sector_buffer[512];
	uint32_t entries_per_sector = 512 / entry_size;
    for (uint32_t i = 0; i < count; i += entries_per_sector) {

        // 섹터 하나를 통째로 읽음
        uint64_t current_lba = table_lba + (i / entries_per_sector);
        disk->read_bytes(current_lba * SECTOR_SIZE, sector_buffer, 512);

        // 읽은 섹터 안에서 엔트리들을 파싱
        for (uint32_t j = 0; j < entries_per_sector; j++) {
            uint32_t entry_idx = i + j;
            if (entry_idx >= count) break;

            // 버퍼 내 오프셋 계산
            Gpt_entry* entry = (Gpt_entry*)(sector_buffer + (j * entry_size));

            // 비어있는지 확인 (Type GUID가 0인지)
            if (is_all_zero(entry->type_guid, 16)) continue;

            // PartitionInfo 생성
            Partition::PartitionInfo pinfo;
            pinfo.attrs = entry->attrs;
            pinfo.first_lba = entry->first_lba;
            pinfo.last_lba = entry->last_lba;
            memcpy(pinfo.name, entry->name, sizeof(pinfo.name));
            memcpy(pinfo.type_guid, entry->type_guid, sizeof(pinfo.type_guid));

            // [중요] 여기서 this를 넘겨줌 (Partitioner 위임 구조일 경우)
            (new Partition(pinfo, this))->init();
            // 이름 출력 로직 등...
        }
    }
}

bool GPTPartitioner::read(PartitionInfo& pinfo, uint64_t addr, void* buffer, uint64_t size) {
    uint64_t partition_size = (pinfo.last_lba - pinfo.first_lba + 1) * SECTOR_SIZE;

    if (addr + size > partition_size) {
        // uart_print("Error: Read out of partition bounds!\n");
        return false;
    }
	uint64_t start_addr = pinfo.first_lba * SECTOR_SIZE + addr;
    this->master_disk->read_bytes(start_addr, buffer, size);
    return true;
}