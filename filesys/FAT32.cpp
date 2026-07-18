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
    // "." => current dir; ".." at root => root. (root has no real . / .. entries -> synthesize)
    if ((name[0] == '.' && name[1] == 0) ||
        (name[0] == '.' && name[1] == '.' && name[2] == 0 && dir_cluster == this->bpb.RootCluster)) {
        memset(out_entry, 0, sizeof(FAT32_DirEntry));
        out_entry->Attr = 0x10;
        out_entry->FstClusHI = (uint16_t)(dir_cluster >> 16);
        out_entry->FstClusLO = (uint16_t)(dir_cluster & 0xFFFF);
        if (out_offset) *out_offset = 0;
        return true;
    }
    uint32_t current_clus = dir_cluster;
    char lfn_buf[256];
    bool has_lfn = false;
    for (int lk = 0; lk < 256; lk++) lfn_buf[lk] = 0;

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
            if ((uint8_t)entry.Name[0] == 0xE5) { has_lfn = false; for (int lk = 0; lk < 256; lk++) lfn_buf[lk] = 0; continue; }

            // LFN(긴 파일 이름) 및 볼륨 라벨 등은 일단 무시 (필요시 구현)
            if (entry.Attr == 0x0F) {
                uint8_t* lraw = (uint8_t*)&entry;
                uint8_t lseq = lraw[0] & 0x3F;
                if (lseq >= 1 && lseq <= 20) {
                    static const int lfn_off[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
                    int lbase = (lseq - 1) * 13;
                    for (int lj = 0; lj < 13; lj++) {
                        uint16_t lc = (uint16_t)lraw[lfn_off[lj]] | ((uint16_t)lraw[lfn_off[lj] + 1] << 8);
                        int lpos = lbase + lj;
                        if (lpos >= 0 && lpos < 255) {
                            if (lc == 0 || lc == 0xFFFF) lfn_buf[lpos] = 0;
                            else lfn_buf[lpos] = (lc < 0x80) ? (char)lc : '?';
                        }
                    }
                    has_lfn = true;
                }
                continue;
            }

            // 3. 이름 비교 (8.3 형식 변환 필요하지만 여기선 간단 비교 로직 가정)
            // 실제로는 "TEXT    TXT" 형태를 "text.txt"와 비교하는 함수 필요
            bool lmatch = (has_lfn && strcasecmp(lfn_buf, name) == 0) || compare_fat_name(entry.Name, name);
            has_lfn = false;
            for (int lk = 0; lk < 256; lk++) lfn_buf[lk] = 0;
            if (lmatch) {
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
    if (path[path_idx] == 0) {
        return new DirFile(this, current_cluster);
    }
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
                uint32_t dir_clus = ((uint32_t)entry.FstClusHI << 16) | entry.FstClusLO;
                if (dir_clus == 0) dir_clus = bpb.RootCluster;
                return new DirFile(this, dir_clus);
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

uint64_t FAT32::get_dir_id(const char* path, uint64_t base_dir_id) {
    uint32_t current_cluster = (base_dir_id == 0) ? this->bpb.RootCluster : (uint32_t)base_dir_id;
    int path_idx = 0;
    if (path[path_idx] == '/') path_idx++;
    if (path[path_idx] == 0) return current_cluster;
    char name_buf[256];
    while (path[path_idx] != 0) {
        int i = 0;
        while (path[path_idx] != '/' && path[path_idx] != 0)
            name_buf[i++] = path[path_idx++];
        name_buf[i] = 0;
        if (path[path_idx] == '/') path_idx++;
        if (name_buf[0] == 0) continue;
        FAT32_DirEntry entry;
        uint64_t entry_offset;
        bool found = find_entry(current_cluster, name_buf, &entry, &entry_offset);
        if (!found) return (uint64_t)-1;
        if (!(entry.Attr & 0x10)) return (uint64_t)-1;
        current_cluster = ((uint32_t)entry.FstClusHI << 16) | entry.FstClusLO;
        if (current_cluster == 0) current_cluster = this->bpb.RootCluster;
    }
    return current_cluster;
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
File* FAT32::create_file(const char* path, uint64_t base_dir_id) {
    uint32_t cur = (base_dir_id == 0) ? bpb.RootCluster : (uint32_t)base_dir_id;
    int pi = 0;
    if (path[pi] == '/') pi++;
    if (path[pi] == 0) return nullptr;
    char name[256];
    while (path[pi] != 0) {
        int i = 0;
        while (path[pi] != '/' && path[pi] != 0) name[i++] = path[pi++];
        name[i] = 0;
        if (path[pi] == '/') pi++;
        bool last = (path[pi] == 0);
        if (!last) { // enter existing subdirectory
            FAT32_DirEntry e; uint64_t off;
            if (!find_entry(cur, name, &e, &off)) return nullptr;
            if (!(e.Attr & 0x10)) return nullptr;
            cur = ((uint32_t)e.FstClusHI << 16) | e.FstClusLO;
            if (cur == 0) cur = bpb.RootCluster;
            continue;
        }
        // last component: open if exists, else create
        FAT32_DirEntry ex; uint64_t exoff;
        if (find_entry(cur, name, &ex, &exoff)) {
            uint32_t fc = ((uint32_t)ex.FstClusHI << 16) | ex.FstClusLO;
            return new File(this, fc, exoff, ex.FileSize);
        }
        // build 8.3 name (uppercase)
        char n83[11]; for (int k = 0; k < 11; k++) n83[k] = ' ';
        int di = 0, si = 0;
        while (name[si] && name[si] != '.' && di < 8) {
            char c = name[si++]; if (c >= 'a' && c <= 'z') c -= 32; n83[di++] = c;
        }
        while (name[si] && name[si] != '.') si++;
        if (name[si] == '.') { si++; int ei = 0;
            while (name[si] && ei < 3) { char c = name[si++]; if (c >= 'a' && c <= 'z') c -= 32; n83[8 + ei++] = c; } }
        // scan directory for a free 32-byte slot
        int epc = bpb.BytesPerSector * bpb.SectorsPerCluster / 32;
        uint32_t clus = cur;
        while (clus < 0x0FFFFFF8) {
            uint64_t coff = (uint64_t)(bpb.ReservedSectorCount
                + (uint64_t)bpb.NumFATs * bpb.FATSize32
                + (uint64_t)(clus - 2) * bpb.SectorsPerCluster) * bpb.BytesPerSector;
            for (int e = 0; e < epc; e++) {
                FAT32_DirEntry de;
                partitioner->read((Partitioner::PartitionInfo&)data, coff + (uint64_t)e * 32, &de, 32);
                if ((uint8_t)de.Name[0] == 0x00 || (uint8_t)de.Name[0] == 0xE5) {
                    uint32_t first = alloc_cluster(); // data cluster for the new file
                    if (!first) return nullptr;
                    FAT32_DirEntry ne; memset(&ne, 0, sizeof(ne));
                    memcpy(ne.Name, n83, 8); memcpy(ne.ext, n83 + 8, 3);
                    ne.Attr = 0x20;
                    ne.FstClusHI = (uint16_t)(first >> 16);
                    ne.FstClusLO = (uint16_t)(first & 0xFFFF);
                    ne.FileSize = 0;
                    uint64_t eoff = coff + (uint64_t)e * 32;
                    partitioner->write((Partitioner::PartitionInfo&)data, eoff, &ne, 32);
                    return new File(this, first, eoff, 0);
                }
            }
            clus = get_next_cluster_from_fat(clus);
        }
        return nullptr; // directory full (extension TODO)
    }
    return nullptr;
}

bool FAT32::create_dir(const char* path, uint64_t base_dir_id) {
    uint32_t cur = (base_dir_id == 0) ? bpb.RootCluster : (uint32_t)base_dir_id;
    int pi = 0;
    if (path[pi] == '/') pi++;
    if (path[pi] == 0) return false;
    char name[256];
    while (path[pi] != 0) {
        int i = 0;
        while (path[pi] != '/' && path[pi] != 0) name[i++] = path[pi++];
        name[i] = 0;
        if (path[pi] == '/') pi++;
        bool last = (path[pi] == 0);
        if (!last) { // enter existing subdirectory
            FAT32_DirEntry e; uint64_t off;
            if (!find_entry(cur, name, &e, &off)) return false;
            if (!(e.Attr & 0x10)) return false;
            cur = ((uint32_t)e.FstClusHI << 16) | e.FstClusLO;
            if (cur == 0) cur = bpb.RootCluster;
            continue;
        }
        FAT32_DirEntry ex; uint64_t exoff;
        if (find_entry(cur, name, &ex, &exoff)) return false; // already exists
        char n83[11]; for (int k = 0; k < 11; k++) n83[k] = ' ';
        int di = 0, si = 0;
        while (name[si] && name[si] != '.' && di < 8) {
            char c = name[si++]; if (c >= 'a' && c <= 'z') c -= 32; n83[di++] = c;
        }
        while (name[si] && name[si] != '.') si++;
        if (name[si] == '.') { si++; int ei = 0;
            while (name[si] && ei < 3) { char c = name[si++]; if (c >= 'a' && c <= 'z') c -= 32; n83[8 + ei++] = c; } }
        int epc = bpb.BytesPerSector * bpb.SectorsPerCluster / 32;
        uint64_t eoff = ~0ULL;
        uint32_t clus = cur;
        while (clus < 0x0FFFFFF8 && eoff == ~0ULL) {
            uint64_t coff = (uint64_t)(bpb.ReservedSectorCount
                + (uint64_t)bpb.NumFATs * bpb.FATSize32
                + (uint64_t)(clus - 2) * bpb.SectorsPerCluster) * bpb.BytesPerSector;
            for (int e = 0; e < epc; e++) {
                FAT32_DirEntry de;
                partitioner->read((Partitioner::PartitionInfo&)data, coff + (uint64_t)e * 32, &de, 32);
                if ((uint8_t)de.Name[0] == 0x00 || (uint8_t)de.Name[0] == 0xE5) { eoff = coff + (uint64_t)e * 32; break; }
            }
            if (eoff == ~0ULL) clus = get_next_cluster_from_fat(clus);
        }
        if (eoff == ~0ULL) return false; // parent dir full (extension TODO)
        uint32_t dclus = alloc_cluster();
        if (!dclus) return false;
        uint64_t dcoff = (uint64_t)(bpb.ReservedSectorCount
            + (uint64_t)bpb.NumFATs * bpb.FATSize32
            + (uint64_t)(dclus - 2) * bpb.SectorsPerCluster) * bpb.BytesPerSector;
        uint8_t zero[512]; memset(zero, 0, 512);
        for (uint32_t s = 0; s < bpb.SectorsPerCluster; s++)
            partitioner->write((Partitioner::PartitionInfo&)data, dcoff + (uint64_t)s * 512, zero, 512);
        FAT32_DirEntry dot; memset(&dot, 0, sizeof(dot));
        for (int k = 0; k < 8; k++) dot.Name[k] = ' ';
        for (int k = 0; k < 3; k++) dot.ext[k] = ' ';
        dot.Name[0] = '.'; dot.Attr = 0x10;
        dot.FstClusHI = (uint16_t)(dclus >> 16); dot.FstClusLO = (uint16_t)(dclus & 0xFFFF);
        partitioner->write((Partitioner::PartitionInfo&)data, dcoff, &dot, 32);
        FAT32_DirEntry dd; memset(&dd, 0, sizeof(dd));
        for (int k = 0; k < 8; k++) dd.Name[k] = ' ';
        for (int k = 0; k < 3; k++) dd.ext[k] = ' ';
        dd.Name[0] = '.'; dd.Name[1] = '.'; dd.Attr = 0x10;
        uint32_t par = (cur == bpb.RootCluster) ? 0 : cur;
        dd.FstClusHI = (uint16_t)(par >> 16); dd.FstClusLO = (uint16_t)(par & 0xFFFF);
        partitioner->write((Partitioner::PartitionInfo&)data, dcoff + 32, &dd, 32);
        FAT32_DirEntry ne; memset(&ne, 0, sizeof(ne));
        memcpy(ne.Name, n83, 8); memcpy(ne.ext, n83 + 8, 3);
        ne.Attr = 0x10;
        ne.FstClusHI = (uint16_t)(dclus >> 16); ne.FstClusLO = (uint16_t)(dclus & 0xFFFF);
        ne.FileSize = 0;
        partitioner->write((Partitioner::PartitionInfo&)data, eoff, &ne, 32);
        return true;
    }
    return false;
}

void FAT32::free_cluster_chain(uint32_t start) {
    uint32_t c = start;
    while (c >= 2 && c < 0x0FFFFFF8) {
        uint32_t next = get_next_cluster_from_fat(c);
        uint32_t zero = 0;
        uint64_t fat_off = (uint64_t)bpb.ReservedSectorCount * bpb.BytesPerSector + (uint64_t)c * 4;
        partitioner->write((Partitioner::PartitionInfo&)data, fat_off, &zero, 4);
        c = next;
    }
}
bool FAT32::delete_file(const char* path, uint64_t base_dir_id) {
    uint32_t cur = (base_dir_id == 0) ? bpb.RootCluster : (uint32_t)base_dir_id;
    int pi = 0;
    if (path[pi] == '/') pi++;
    if (path[pi] == 0) return false;
    char name[256];
    while (path[pi] != 0) {
        int i = 0;
        while (path[pi] != '/' && path[pi] != 0) name[i++] = path[pi++];
        name[i] = 0;
        if (path[pi] == '/') pi++;
        bool last = (path[pi] == 0);
        FAT32_DirEntry e; uint64_t off;
        if (!find_entry(cur, name, &e, &off)) return false;
        if (!last) {
            if (!(e.Attr & 0x10)) return false;
            cur = ((uint32_t)e.FstClusHI << 16) | e.FstClusLO;
            if (cur == 0) cur = bpb.RootCluster;
            continue;
        }
        if (e.Attr & 0x10) return false; // directory: use rmdir (not here)
        uint32_t fc = ((uint32_t)e.FstClusHI << 16) | e.FstClusLO;
        if (fc >= 2) free_cluster_chain(fc);
        uint8_t del = 0xE5;
        partitioner->write((Partitioner::PartitionInfo&)data, off, &del, 1);
        return true;
    }
    return false;
}

bool FAT32::truncate_file(uint64_t first_cluster, uint64_t meta_id) {
    uint32_t fc = (uint32_t)first_cluster;
    if (fc >= 2 && fc < 0x0FFFFFF8) {
        uint32_t second = get_next_cluster_from_fat(fc);
        if (second >= 2 && second < 0x0FFFFFF8) free_cluster_chain(second);
        uint32_t eoc = 0x0FFFFFFF;
        uint64_t fat_off = (uint64_t)bpb.ReservedSectorCount * bpb.BytesPerSector + (uint64_t)fc * 4;
        partitioner->write((Partitioner::PartitionInfo&)data, fat_off, &eoc, 4);
    }
    uint32_t zero = 0;
    partitioner->write((Partitioner::PartitionInfo&)data, meta_id + (uint64_t)&((FAT32_DirEntry*)0)->FileSize, &zero, 4);
    return true;
}

bool FAT32::remove_dir(const char* path, uint64_t base_dir_id) {
    uint32_t cur = (base_dir_id == 0) ? bpb.RootCluster : (uint32_t)base_dir_id;
    int pi = 0;
    if (path[pi] == '/') pi++;
    if (path[pi] == 0) return false;
    char name[256];
    while (path[pi] != 0) {
        int i = 0;
        while (path[pi] != '/' && path[pi] != 0) name[i++] = path[pi++];
        name[i] = 0;
        if (path[pi] == '/') pi++;
        bool last = (path[pi] == 0);
        FAT32_DirEntry e; uint64_t off;
        if (!find_entry(cur, name, &e, &off)) return false;
        if (!last) {
            if (!(e.Attr & 0x10)) return false;
            cur = ((uint32_t)e.FstClusHI << 16) | e.FstClusLO;
            if (cur == 0) cur = bpb.RootCluster;
            continue;
        }
        if (!(e.Attr & 0x10)) return false; // not a directory
        uint32_t child = ((uint32_t)e.FstClusHI << 16) | e.FstClusLO;
        if (child < 2) return false;
        int epc = bpb.BytesPerSector * bpb.SectorsPerCluster / 32;
        uint32_t c = child; bool empty = true, done = false;
        while (c >= 2 && c < 0x0FFFFFF8 && !done) {
            uint64_t coff = (uint64_t)(bpb.ReservedSectorCount
                + (uint64_t)bpb.NumFATs * bpb.FATSize32
                + (uint64_t)(c - 2) * bpb.SectorsPerCluster) * bpb.BytesPerSector;
            for (int en = 0; en < epc; en++) {
                FAT32_DirEntry de;
                partitioner->read((Partitioner::PartitionInfo&)data, coff + (uint64_t)en * 32, &de, 32);
                uint8_t c0 = (uint8_t)de.Name[0];
                if (c0 == 0x00) { done = true; break; }
                if (c0 == 0xE5) continue;
                if (de.Attr == 0x0F) continue;
                if (de.Name[0] == '.') continue;
                empty = false; done = true; break;
            }
            if (!done) c = get_next_cluster_from_fat(c);
        }
        if (!empty) return false; // not empty
        free_cluster_chain(child);
        uint8_t del = 0xE5;
        partitioner->write((Partitioner::PartitionInfo&)data, off, &del, 1);
        return true;
    }
    return false;
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
struct FAT32_LFN {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  lfn_type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t fstClus;
    uint16_t name3[2];
} __attribute__((packed));

#define MY_DIRENT_HDR 13
#define DT_REG_FAT 0
#define DT_DIR_FAT 1

int FAT32::getdents64(uint64_t dir_id, uint64_t start_idx, void* buf, uint32_t buf_size) {
    uint32_t current_clus = (uint32_t)dir_id;
    uint8_t* out = (uint8_t*)buf;
    uint32_t written = 0;
    uint64_t entry_idx = 0;
    char lfn_buf[256];
    bool has_lfn = false;
    bool done = false;
    for (int k = 0; k < 256; k++) lfn_buf[k] = 0;

    while (!done && current_clus < 0x0FFFFFF8) {
        int entries_per_cluster = bpb.BytesPerSector * bpb.SectorsPerCluster / 32;
        uint64_t cluster_offset = (uint64_t)(bpb.ReservedSectorCount
            + (uint64_t)bpb.NumFATs * bpb.FATSize32
            + (uint64_t)(current_clus - 2) * bpb.SectorsPerCluster)
            * bpb.BytesPerSector;

        for (int i = 0; i < entries_per_cluster && !done; i++) {
            FAT32_DirEntry entry;
            partitioner->read((Partitioner::PartitionInfo&)data,
                cluster_offset + (uint64_t)i * 32, &entry, 32);

            if (entry.Name[0] == 0x00) { done = true; break; }

            if ((uint8_t)entry.Name[0] == 0xE5) {
                has_lfn = false;
                for (int k = 0; k < 256; k++) lfn_buf[k] = 0;
                continue;
            }

            if (entry.Attr == 0x0F) {
                FAT32_LFN* lfn = (FAT32_LFN*)&entry;
                uint8_t seq = lfn->order & 0x3F;
                if (seq == 0 || seq > 20) continue;
                int base = (int)(seq - 1) * 13;
                for (int j = 0; j < 5; j++) {
                    uint16_t c = lfn->name1[j];
                    if (c == 0 || c == 0xFFFF) break;
                    int pos = base + j;
                    if (pos >= 0 && pos < 255)
                        lfn_buf[pos] = (c < 0x80) ? (char)c : '?';
                }
                for (int j = 0; j < 6; j++) {
                    uint16_t c = lfn->name2[j];
                    if (c == 0 || c == 0xFFFF) break;
                    int pos = base + 5 + j;
                    if (pos >= 0 && pos < 255)
                        lfn_buf[pos] = (c < 0x80) ? (char)c : '?';
                }
                for (int j = 0; j < 2; j++) {
                    uint16_t c = lfn->name3[j];
                    if (c == 0 || c == 0xFFFF) break;
                    int pos = base + 11 + j;
                    if (pos >= 0 && pos < 255)
                        lfn_buf[pos] = (c < 0x80) ? (char)c : '?';
                }
                has_lfn = true;
                continue;
            }

            if (entry.Name[0] == '.') {
                has_lfn = false;
                for (int k = 0; k < 256; k++) lfn_buf[k] = 0;
                continue;
            }

            if (entry_idx < start_idx) {
                entry_idx++;
                has_lfn = false;
                for (int k = 0; k < 256; k++) lfn_buf[k] = 0;
                continue;
            }

            char name_83[13];
            int t = 0;
            if (!has_lfn) {
                for (int j = 0; j < 8 && entry.Name[j] != ' '; j++)
                    name_83[t++] = entry.Name[j];
                if (entry.ext[0] != ' ') {
                    name_83[t++] = '.';
                    for (int j = 0; j < 3 && entry.ext[j] != ' '; j++)
                        name_83[t++] = entry.ext[j];
                }
                name_83[t] = 0;
            }
            const char* final_name = has_lfn ? lfn_buf : name_83;

            uint32_t namelen = 0;
            while (final_name[namelen]) namelen++;

            uint32_t reclen = (uint32_t)(MY_DIRENT_HDR + namelen + 1);
            if (written + reclen > buf_size) { done = true; break; }

            *(uint64_t*)(out + written)     = (uint64_t)reclen;
            *(uint8_t*) (out + written + 8) = (entry.Attr & 0x10) ? DT_DIR_FAT : DT_REG_FAT;
            *(uint32_t*)(out + written + 9) = entry.FileSize;
            char* dst = (char*)(out + written + 13);
            for (uint32_t j = 0; j <= namelen; j++) dst[j] = final_name[j];

            written += reclen;
            entry_idx++;
            has_lfn = false;
            for (int k = 0; k < 256; k++) lfn_buf[k] = 0;
        }
        if (!done)
            current_clus = get_next_cluster_from_fat(current_clus);
    }
    return (int)written;
}
