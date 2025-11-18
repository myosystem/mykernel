#ifndef __DISKSID_H__
#define __DISKSID_H__
#define MAX_DISKS 64
#include "size.h"
#include "disk.h"
struct PartitionInfo {
    uint8_t  type_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attrs;
    uint16_t name[36]; // UTF-16LE 이름
    Disk* disk;
    uint8_t disk_type; // 1 = AHCI/SATA, 2 = NVMe, 3 = USB MSC ...
    uint16_t disk_id;
    uint8_t flags;
    uint8_t disk_buffer[sizeof(Disk)]; // placement new용
} __attribute__((packed));

#define partitions_base ((PartitionInfo*)0xFFFF800000000000)
//확장필요
extern uint64_t disk_bitmap; // 1 = 사용 중, 0 = 비어있음

static inline uint16_t allocate_disk_id() {
    for (unsigned short i = 0; i < MAX_DISKS; i++) {
        if (!(disk_bitmap & (1ULL << i))) {
            disk_bitmap |= (1ULL << i);
            return i;
        }
    }
    return -1; // 꽉 참
}

static inline void free_disk_id(int id) {
    disk_bitmap &= ~(1ULL << id);
}
static inline PartitionInfo* find_partition(uint32_t disk_id, uint32_t index) {
    PartitionInfo* p = partitions_base;
    while (p->flags & 1) {
        if (p->disk_id == disk_id) {
            if (index-- == 0) {
                return p;
            }
        }
        p++;
    }
    return 0;
}
#endif