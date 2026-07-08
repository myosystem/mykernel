#ifndef __FILE_H__
#define __FILE_H__
#include "filesys/partition.h"
#include "kernel/process.h"
#include "util/new.h"
#define FILE_QUEUE_BASE 0xFFFF838000000000ULL
#define EOF (-1)
struct PathResolveResult {
	Partition* target_partition; // 타겟 파티션 객체
	const char* relative_path;   // 파티션 내부 경로 (예: "System/Kernel.elf")
};
PathResolveResult resolve_path(const char* path, Partition* cwd_partition);
class File;
File* vfs_open(const char* path, Partition* cwd_part, uint64_t cwd_id);
File* kernel_open_file(const char* path);
int vfs_chdir(const char* path, Partition* cwd_part, uint64_t cwd_id,
              Partition** out_partition, uint64_t* out_cluster);
class File : public NewObject<FILE_QUEUE_BASE,0x200,nullptr,nullptr> {
protected:
	Partition* partition;
	uint64_t file_size;
	uint64_t current_offset;
	uint64_t file_id;
	uint64_t meta_id;
	uint64_t refcount;
public:
	File(Partition* part, uint64_t file_id, uint64_t meta_id, uint64_t size)
		: partition(part), file_size(size), current_offset(0), file_id(file_id), meta_id(meta_id), refcount(1) {
	}
	virtual ~File() {}
	virtual int read(void* buf, uint32_t len);
	virtual int write(const void* buf, uint32_t len);
	virtual int seek(uint64_t offset);
	virtual uint64_t tell();
	virtual uint64_t size();
	virtual void close();
	virtual void open();
	virtual bool is_directory() const { return false; }
	uint64_t get_file_id() const { return file_id; }
	Partition* get_partition() const { return partition; }
	uint64_t get_refcount() const { return refcount; }
};
class DirFile : public File {
	uint8_t  entry_buf[300];
	uint16_t entry_bytes;
	uint16_t entry_consumed;
public:
	DirFile(Partition* part, uint64_t dir_cluster)
		: File(part, dir_cluster, 0, 0), entry_bytes(0), entry_consumed(0) {
		for (int i = 0; i < 300; i++) entry_buf[i] = 0;
	}
	int read(void* buf, uint32_t len) override;
	int write(const void* buf, uint32_t len) override { return -1; }
	int seek(uint64_t offset) override { current_offset = offset; return 0; }
	bool is_directory() const override { return true; }
};
class STDIn : public File {
	public:
	STDIn() : File(nullptr,0, 0, 0) {}
	int read(void* buf, uint32_t len) override;
	int write(const void* buf, uint32_t len) override { return -1; }
	int seek(uint64_t offset) override { return -1; }
	uint64_t tell() override { return 0; }
	uint64_t size() override { return 0; }
};
class STDOut : public File {
	public:
	STDOut() : File(nullptr,0, 0, 0) {}
	int read(void* buf, uint32_t len) override { return -1; }
	int write(const void* buf, uint32_t len) override;
	int seek(uint64_t offset) override { return -1; }
	uint64_t tell() override { return 0; }
	uint64_t size() override { return 0; }
};
#endif // __FILE_H__