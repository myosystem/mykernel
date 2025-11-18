#include "filesys/gpt.h"
#include "kernel/kernel.h"
#include "util/memory.h"
#include "filesys/disksid.h"
#include "debug/log.h"
#include "util/util.h"
struct gpt_header {
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

struct gpt_entry {
    uint8_t  type_guid[16];
    uint8_t  uniq_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attrs;
    uint16_t name[36]; // UTF-16LE 이름
} __attribute__((packed));


//read함수 다른것도 사용할 수 있도록 type도 받아야함
uint16_t init_gpt(volatile HBA_PORT* port, void* header) {
    gpt_header gpt = *(gpt_header*)header;

    uint64_t entry_lba = gpt.part_entry_lba;
    uint32_t entry_size = gpt.size_part_entry;
    uint32_t entry_count = gpt.num_part_entries;

    uint32_t entries_per_sector = 512 / entry_size;
    PartitionInfo* partitions = partitions_base;
	uint32_t disk_id = allocate_disk_id();
	uart_print("GPT detected:\ndisk id:");
	uart_print(disk_id);
	uart_print("\ntotal entries:");
	uart_print(entry_count);
	uart_print("\nentry size:");
	uart_print(entry_size);
	uart_print("\nentries per sector:");
	uart_print(entries_per_sector);

    for (unsigned int i = 0; i < entry_count; i++) {
        if (i % entries_per_sector == 0) {
            ahci_read(port, entry_lba + (i / entries_per_sector), 1, header);
        }
        while ((partitions->flags & 1) == 1) partitions++;
        gpt_entry* entry = (gpt_entry*)((uint64_t)header + (entry_size * (i % entries_per_sector)));
        if (is_all_zero((void*)entry->uniq_guid, sizeof(entry->uniq_guid))) {
			continue;
        }// 비어있는 엔트리
        memcpy(partitions->type_guid, entry->type_guid,sizeof(partitions->type_guid));
        memcpy(partitions->name, entry->name,sizeof(partitions->name));
        partitions->first_lba = entry->first_lba;
        partitions->last_lba = entry->last_lba;
        partitions->attrs = entry->attrs;
		partitions->disk = new (partitions->disk_buffer) Disk(port, (uint8_t*)header);
		partitions->disk_type = 1; // 일단 AHCI/SATA만
        partitions->disk_id = disk_id;
        partitions->flags = 1;
		uart_print("GPT Partition found:\ntype:");
		char console[16 * 3 + 1] = "";
		bytes_to_hex_string((char*)partitions->type_guid, 16, (char*)console);
		uart_print((char*)console);
		uart_print("\nuniq:");
		bytes_to_hex_string((char*)entry->uniq_guid, 16, (char*)console);
		uart_print((char*)console);
		uart_print("\nfirst:");
		uart_print_hex(partitions->first_lba);
		uart_print("\nlast:");
		uart_print_hex(partitions->last_lba);
		uart_print("\nattrs:");
		uart_print_hex(partitions->attrs);
		uart_print("\n\n");
    }
	return disk_id;
}