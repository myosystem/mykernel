#ifndef __FAT32_H__
#define __FAT32_H__
#include "util/size.h"
#include "filesys/file.h"
#include "filesys/partition.h"
struct FAT32_BPB {
    uint8_t  jmpBoot[3];        // 점프 코드
    char     OEMName[8];        // OEM 이름
    uint16_t BytesPerSector;    // 섹터 크기
    uint8_t  SectorsPerCluster; // 클러스터당 섹터 수
    uint16_t ReservedSectorCount; // 예약 섹터 수 (FAT 시작기준)
    uint8_t  NumFATs;           // FAT 테이블 수
    uint16_t RootEntryCount;    // FAT12/16용, FAT32는 0
    uint16_t TotalSectors16;    // 16비트 전체 섹터 수, FAT32는 0
    uint8_t  Media;             // 미디어 타입
    uint16_t FATSize16;         // FAT12/16용, FAT32는 0
    uint16_t SectorsPerTrack;
    uint16_t NumberOfHeads;
    uint32_t HiddenSectors;     // 파티션 시작 LBA
    uint32_t TotalSectors32;    // FAT32 전체 섹터 수
    uint32_t FATSize32;         // FAT32 FAT 테이블 크기(섹터 단위)
    uint16_t ExtFlags;
    uint16_t FSVersion;
    uint32_t RootCluster;       // 루트 디렉토리 시작 클러스터
    uint16_t FSInfo;
    uint16_t BackupBootSector;
    uint8_t  Reserved[12];
    uint8_t  DriveNumber;
    uint8_t  Reserved1;
    uint8_t  BootSignature;
    uint32_t VolumeID;
    char     VolumeLabel[11];
    char     FSType[8];         // "FAT32   "
} __attribute__ ((packed));

struct FAT32_DirEntry {
    char     Name[8];       // 8자 파일명
	char     ext[3];        // 확장자
    uint8_t  Attr;           // 속성 (0x10=디렉토리, 0x20=파일)
    uint8_t  NTRes;
    uint8_t  CrtTimeTenth;
    uint16_t CrtTime;
    uint16_t CrtDate;
    uint16_t LstAccDate;
    uint16_t FstClusHI;
    uint16_t WrtTime;
    uint16_t WrtDate;
    uint16_t FstClusLO;
    uint32_t FileSize;
} __attribute__((packed));

class FAT32 : public Partition {
private:
    uint32_t get_next_cluster_from_fat(uint32_t current_cluster);
    bool find_entry(uint32_t dir_cluster, const char* name, FAT32_DirEntry* out_entry, uint64_t* out_offset);
public:
    FAT32_BPB bpb;
    FAT32(PartitionInfo& pinfo, Partitioner* partitioner);
    ~FAT32();
    void init() override;
    File* open_file(const char* path, uint64_t base_dir_id) override;
    int read_file(uint64_t start_cluster, uint64_t offset, void* buffer, uint32_t size) override;
    int write_file(uint64_t file_id, uint64_t meta_id, uint64_t& file_size, uint64_t offset, const void* buffer, uint32_t size) override;
    void list_directory(const char* path) override;
    void close_file(void* file_handle) override;
    uint64_t get_dir_id(const char* path, uint64_t base_dir_id) override;
    int getdents64(uint64_t dir_id, uint64_t start_idx, void* buf, uint32_t buf_size) override;
    uint32_t alloc_cluster();
    bool append_cluster(uint32_t last_cluster, uint32_t new_cluster);
};
#endif