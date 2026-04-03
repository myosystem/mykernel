#ifndef __FILE_H__
#define __FILE_H__
#include "filesys/partition.h"
#include "kernel/process.h"
#define FILE_QUEUE_BASE 0xFFFF838000000000ULL
struct PathResolveResult {
	Partition* target_partition; // 찾은 파티션 객체
	const char* relative_path;   // 파티션 내부 경로 (예: "System/Kernel.elf")
};
PathResolveResult resolve_path(const char* path, Partition* cwd_partition);
File* vfs_open(const char* path, Partition* cwd_part, uint64_t cwd_id);
File* kernel_open_file(const char* path);
class File {
private:
	Partition* partition;
	uint64_t file_size;
	uint64_t current_offset;
	uint64_t start_cluster;
	uint8_t state;

public:
	File(Partition* part, uint64_t start_cluster, uint64_t size)
		: partition(part), file_size(size), current_offset(0), start_cluster(start_cluster) {
	}
	virtual ~File() {}
	int read(void* buf, uint32_t len);
	int write(const void* buf, uint32_t len);
	int seek(uint64_t offset);
	uint64_t tell();
	uint64_t size();
	void close();
	void* operator new(size_t size);
	void operator delete(void* ptr);
};
#endif // __FILE_H__