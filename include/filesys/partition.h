#ifndef __PARTITION_H__
#define __PARTITION_H__

#include "util/size.h"
#include "filesys/partitioner.h"
#include "util/memory.h"
#define PARTITIONSTRUCT_SIZE 0x400

class File;
extern uint64_t max_partition_index;

class Partition {
public:
    struct PartitionInfo {
        uint8_t  type_guid[16];
        uint64_t first_lba;
        uint64_t last_lba;
        uint64_t attrs;
        uint16_t name[36]; // UTF-16LE ÀÌ¸§
    } __attribute__((packed));
    char alias[12];
	Partition(PartitionInfo pinfo, Partitioner* partitioner) : data(pinfo), partitioner(partitioner) {
        memset(alias, 0, 12);
    }
    void set_alias(const char* new_name) {
        memcpy(this->alias, new_name, 11);
    }
    virtual ~Partition() {}
    virtual void init();
    virtual File* open_file(const char* path, uint64_t base_dir_id) {
        return nullptr;
    }
    virtual int read_file(uint64_t file_id, uint64_t offset, void* buffer, uint32_t size) { return -1; }
    virtual int write_file(uint64_t file_id, uint64_t meta_id, uint64_t& file_size, uint64_t offset, const void* buffer, uint32_t size) { return -1; }
    virtual void list_directory(const char* path) {}
    virtual void close_file(void* file_handle) {}
	void* operator new(size_t size) noexcept;
	void operator delete(void* ptr);
    uint8_t flags;
    PartitionInfo data;
protected:
    Partitioner* partitioner;
};

#define PARTITION_QUEUE_BASE 0xFFFF800000000000ULL

#endif /*__PARTITION_H__*/