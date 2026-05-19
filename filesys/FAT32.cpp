#include "filesys/FAT32.h"
#include "driver/disk.h"
#include "util/size.h"
#include "util/memory.h"
#include "debug/log.h"
#include "filesys/partition.h"

#define PATH_SEP '/'

FAT32::FAT32(PartitionInfo& pinfo, Partitioner* partitioner) : Partition(pinfo, partitioner) {
    flags |= 0b1;
}
FAT32::~FAT32() = default;

void FAT32::init() {
	partitioner->read((Partitioner::PartitionInfo&)data, 0, &bpb, sizeof(FAT32_BPB));
    char label[12];
    memcpy(label, bpb.VolumeLabel, 11);
    label[11] = 0;
    for (int i = 10; i >= 0; i--) {
        if (label[i] == ' ') label[i] = 0;
        else break;
    }
    if (label[0] == 0 || strncmp(label, "NO NAME", 7) == 0) {
    }
    else {
        this->set_alias(label);
        uart_print("Volume Mounted: "); uart_print(label); uart_print("\n");
    }
	uart_print("\nBytes per sector: ");
	uart_print(bpb.BytesPerSector);
	uart_print("\nSectors per cluster: ");
	uart_print(bpb.SectorsPerCluster);
	uart_print("\nReserved sectors: ");
	uart_print(bpb.ReservedSectorCount);
	uart_print("\nNumber of FATs: ");
	uart_print(bpb.NumFATs);
	uart_print("\nFAT size (sectors): ");
	uart_print(bpb.FATSize32);
	uart_print("\nRoot cluster: ");
	uart_print(bpb.RootCluster);
	uart_print("\n");
}

bool compare_fat_name(const char* src, const char* dst) {
    char temp_name[14]; // 변환된 이름을 담을 버퍼 "TEST.TXT\0"
    int t = 0;

    // 1. 파일명 부분 (앞 8자리)
    for (int i = 0; i < 8; i++) {
        if (src[i] == ' ') break; // 공백 만나면 끝
        temp_name[t++] = src[i];
    }

    // 2. 확장자 부분 (뒤 3자리) -> 확장자가 있다면 점(.) 추가
    if (src[8] != ' ') {
        temp_name[t++] = '.';
        for (int i = 8; i < 11; i++) {
            if (src[i] == ' ') break;
            temp_name[t++] = src[i];
        }
    }
    temp_name[t] = 0; // Null Terminate

    // 3. 대소문자 무시 비교 (strcasecmp)
    // 커널에 strcasecmp가 없다면 직접 구현하거나 대문자로 통일해서 비교
    return !strcasecmp(temp_name, dst);
}
uint32_t FAT32::get_next_cluster_from_fat(uint32_t current_cluster) {
    // 1. 읽으려는 엔트리의 오프셋 계산
    // FAT 시작 위치 = 예약된 섹터 수 * 섹터 크기
    // 엔트리 위치 = FAT 시작 위치 + (현재 클러스터 번호 * 4바이트)

    uint64_t fat_offset = (uint64_t)bpb.ReservedSectorCount * bpb.BytesPerSector
        + (uint64_t)current_cluster * 4;

    uint32_t next_cluster_entry = 0;

    // 2. 4바이트(32비트) 읽기
    bool success = this->partitioner->read(
        (Partitioner::PartitionInfo&)this->data,
        fat_offset,
        &next_cluster_entry,
        sizeof(uint32_t)
    );

    if (!success) {
        // 읽기 실패 시, 안전하게 EOC(End of Chain) 리턴하여 루프 종료 유도
        return 0x0FFFFFFF;
    }

    // 3. 상위 4비트 제거 (FAT32 스펙 필수)
    // 상위 4비트는 시스템에 따라 다른 용도로 쓰일 수 있어 무시해야 함
    uint32_t next_cluster = next_cluster_entry & 0x0FFFFFFF;

    // 4. EOC(End of Chain) 확인 및 반환
    // 0x0FFFFFF8 이상이면 체인의 끝을 의미함
    return next_cluster;
}
bool FAT32::find_entry(uint32_t dir_cluster, const char* name, FAT32_DirEntry* out_entry, uint64_t* out_offset) {
    uint32_t current_clus = dir_cluster;

    // 디렉토리도 여러 클러스터에 걸쳐 있을 수 있음 (체인 추적)
    while (current_clus < 0x0FFFFFF8) { // EOC(End of Chain) 체크
        int entries_per_cluster = this->bpb.BytesPerSector * this->bpb.SectorsPerCluster / 32;
        uint64_t cluster_offset = (uint64_t)(bpb.ReservedSectorCount
                                + (uint64_t)bpb.NumFATs * bpb.FATSize32
                                + (uint64_t)(current_clus - 2) * bpb.SectorsPerCluster)
                                * bpb.BytesPerSector;

        for (int i = 0; i < entries_per_cluster; i++) {
            FAT32_DirEntry entry;
            this->partitioner->read(
                (Partitioner::PartitionInfo&)this->data,
                cluster_offset + (i * sizeof(FAT32_DirEntry)), // 오프셋
                &entry,                                        // 버퍼
                sizeof(FAT32_DirEntry)                         // 크기(32)
            );
            // 빈 엔트리(0x00)면 더 이상 데이터 없음 -> 검색 실패
            if (entry.Name[0] == 0x00) return false;

            // 삭제된 엔트리(0xE5)는 건너뜀
            if ((uint8_t)entry.Name[0] == 0xE5) continue;

            // LFN(긴 파일 이름) 및 볼륨 라벨 등은 일단 무시 (필요시 구현)
            if (entry.Attr == 0x0F) continue;

            // 3. 이름 비교 (8.3 형식 변환 필요하지만 여기선 간단 비교 로직 가정)
            // 실제로는 "TEXT    TXT" 형태를 "text.txt"와 비교하는 함수 필요
            if (compare_fat_name(entry.Name, name)) {
                *out_entry = entry;
                if (out_offset) {
                    *out_offset = cluster_offset + (i * sizeof(FAT32_DirEntry));
				}
                return true; // 찾았다!
            }
        }

        // 3. 다음 클러스터로 이동 (FAT 테이블 조회)
        current_clus = get_next_cluster_from_fat(current_clus);
    }

    return false; // 체인 끝까지 뒤졌는데 없음
}
File* FAT32::open_file(const char* path, uint64_t base_dir_id) {
	uint32_t current_cluster = (base_dir_id == 0) ? this->bpb.RootCluster : (uint32_t)base_dir_id;
    int path_idx = 0;
    if (path[path_idx] == '/') path_idx++;
    char name_buf[256];
    while (path[path_idx] != 0) {

        // 2. 경로 토큰 추출 (예: "usr/bin/vi" -> "usr")
        int i = 0;
        while (path[path_idx] != '/' && path[path_idx] != 0) {
            name_buf[i++] = path[path_idx++];
        }
        name_buf[i] = 0;

        // 구분자 건너뛰기
        if (path[path_idx] == '/') path_idx++;

        // 3. 현재 디렉토리에서 검색
        FAT32_DirEntry entry;
		uint64_t entry_offset;
        bool found = find_entry(current_cluster, name_buf, &entry, &entry_offset);

        if (!found) return nullptr; // 경로가 틀림

        // 4. 경로 끝인가?
        bool is_last_token = (path[path_idx] == 0);

        if (is_last_token) {
            // [도착] 
            if (entry.Attr & 0x10) {
                // 디렉토리를 open 하려고 함 -> 현재는 nullptr (나중에 opendir 지원)
                return nullptr;
            }

            // 파일 발견! -> File 객체 생성
            // High/Low 클러스터 합치기
            uint32_t file_clus = ((uint32_t)entry.FstClusHI << 16) | entry.FstClusLO;

            return new File(this, file_clus, entry_offset, entry.FileSize);
        }
        else {
            // [진행 중]
            if (!(entry.Attr & 0x10)) {
                // 경로가 남았는데 파일임 (예: text.txt/abc) -> 에러
                return nullptr;
            }

            // 다음 디렉토리로 진입
            current_cluster = ((uint32_t)entry.FstClusHI << 16) | entry.FstClusLO;

            // [FAT32 특이사항] 클러스터 번호 0은 루트를 의미할 수 있으나,
            // 데이터 영역에서는 보통 2번부터 시작하므로 그대로 씀.
        }
    }

    return nullptr; // 빈 경로 등
}

int FAT32::read_file(uint64_t start_cluster, uint64_t offset, void* buffer, uint32_t size) {
    uint32_t current_cluster = (uint32_t)start_cluster;
    uint32_t bytes_per_cluster = bpb.BytesPerSector * bpb.SectorsPerCluster;
    // 1. 오프셋에 해당하는 클러스터로 이동
    uint64_t cluster_offset = offset / bytes_per_cluster;
    uint64_t intra_cluster_offset = offset % bytes_per_cluster;
    for (uint64_t i = 0; i < cluster_offset; i++) {
        current_cluster = get_next_cluster_from_fat(current_cluster);
        if (current_cluster >= 0x0FFFFFF8) {
            return 0; // EOC 도달, 더 이상 읽을 수 없음
        }
    }
    uint8_t* out_buf = (uint8_t*)buffer;
    uint32_t total_read = 0;
    uint64_t data_start_lba = bpb.ReservedSectorCount + (uint64_t)bpb.NumFATs * bpb.FATSize32;
    while (total_read < size) {
        // 현재 클러스터의 시작 오프셋 계산
        uint64_t cluster_start_offset = (data_start_lba + (uint64_t)(current_cluster - 2) * bpb.SectorsPerCluster)
            * bpb.BytesPerSector;
        // 읽을 수 있는 최대 크기 계산
        uint32_t to_read = bytes_per_cluster - intra_cluster_offset;
        if (to_read > size - total_read) {
            to_read = size - total_read;
        }
        // 데이터 읽기
        bool success = this->partitioner->read(
            (Partitioner::PartitionInfo&)this->data,
            cluster_start_offset + intra_cluster_offset,
            out_buf + total_read,
            to_read
        );
        if (!success) {
            break; // 읽기 실패 시 중단
        }
        total_read += to_read;
        intra_cluster_offset = 0; // 이후부터는 클러스터 시작부터 읽음
		if (total_read >= size) break; // 다 읽음
        // 다음 클러스터로 이동
        current_cluster = get_next_cluster_from_fat(current_cluster);
        if (current_cluster >= 0x0FFFFFF8) {
            break; // EOC 도달, 더 이상 읽을 수 없음
        }
    }
	if (total_read < size) {
		memset((uint8_t*)buffer + total_read, 0, size - total_read);
    }
	return total_read;
}
int FAT32::write_file(uint64_t file_id, uint64_t meta_id, uint64_t& file_size, uint64_t offset, const void* buffer, uint32_t size) {
    uint32_t current_cluster = (uint32_t)file_id;
    uint32_t bytes_per_cluster = bpb.BytesPerSector * bpb.SectorsPerCluster;

    uint64_t cluster_offset = offset / bytes_per_cluster;
    uint64_t intra_cluster_offset = offset % bytes_per_cluster;

    // 시작 클러스터까지 이동
    uint32_t prev_cluster = current_cluster;
    for (uint64_t i = 0; i < cluster_offset; i++) {
        prev_cluster = current_cluster;
        current_cluster = get_next_cluster_from_fat(current_cluster);
        if (current_cluster >= 0x0FFFFFF8) {
            // 클러스터 부족 -> 새로 할당
            uint32_t new_cluster = alloc_cluster();
            if (!new_cluster) return 0;
            append_cluster(prev_cluster, new_cluster);
            current_cluster = new_cluster;
        }
    }

    const uint8_t* in_buf = (const uint8_t*)buffer;
    uint32_t total_written = 0;
    uint64_t data_start_lba = bpb.ReservedSectorCount + (uint64_t)bpb.NumFATs * bpb.FATSize32;

    while (total_written < size) {
        uint64_t cluster_start_offset = (data_start_lba + (uint64_t)(current_cluster - 2) * bpb.SectorsPerCluster)
            * bpb.BytesPerSector;

        uint32_t to_write = bytes_per_cluster - intra_cluster_offset;
        if (to_write > size - total_written) to_write = size - total_written;

        bool success = this->partitioner->write(
            (Partitioner::PartitionInfo&)this->data,
            cluster_start_offset + intra_cluster_offset,
            in_buf + total_written,
            to_write
        );
        if (!success) break;

        total_written += to_write;
        intra_cluster_offset = 0;
        if (total_written >= size) break;

        prev_cluster = current_cluster;
        current_cluster = get_next_cluster_from_fat(current_cluster);
        if (current_cluster >= 0x0FFFFFF8) {
            uint32_t new_cluster = alloc_cluster();
            if (!new_cluster) break;
            append_cluster(prev_cluster, new_cluster);
            current_cluster = new_cluster;
        }
    }

    // 파일 크기 업데이트
    if (offset + total_written > file_size) {
        file_size = offset + total_written;
        // 디렉토리 엔트리 크기 업데이트
        uint32_t new_size = (uint32_t)file_size;
        partitioner->write(
            (Partitioner::PartitionInfo&)this->data,
            meta_id + (uint64_t) & ((FAT32_DirEntry*)0)->FileSize,
            &new_size,
            sizeof(uint32_t)
        );
    }

    return total_written;
}
void FAT32::list_directory(const char* path) {

}
void FAT32::close_file(void* file_handle) {

}
uint32_t FAT32::alloc_cluster() {
    uint64_t fat_start = (uint64_t)bpb.ReservedSectorCount * bpb.BytesPerSector;
    uint32_t total_clusters = (bpb.FATSize32 * bpb.BytesPerSector) / 4;

    for (uint32_t i = 2; i < total_clusters; i++) {
        uint32_t entry = 0;
        partitioner->read(
            (Partitioner::PartitionInfo&)this->data,
            fat_start + (uint64_t)i * 4,
            &entry,
            sizeof(uint32_t)
        );
        entry &= 0x0FFFFFFF;
        if (entry == 0) {
            // 찾았으면 EOC로 마킹
            uint32_t eoc = 0x0FFFFFFF;
            partitioner->write(
                (Partitioner::PartitionInfo&)this->data,
                fat_start + i * 4,
                &eoc,
                sizeof(uint32_t)
            );
            return i;
        }
    }
    return 0; // 실패
}
bool FAT32::append_cluster(uint32_t last_cluster, uint32_t new_cluster) {
    uint64_t fat_start = (uint64_t)bpb.ReservedSectorCount * bpb.BytesPerSector;
    uint32_t val = new_cluster & 0x0FFFFFFF;
    return partitioner->write(
        (Partitioner::PartitionInfo&)this->data,
        fat_start + last_cluster * 4,
        &val,
        sizeof(uint32_t)
    );
}