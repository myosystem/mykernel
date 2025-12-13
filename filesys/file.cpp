#include "filesys/file.h"

int File::read(void* buf, uint32_t len) {
	if (current_offset >= file_size) return 0; //EOF
	if (current_offset + len > file_size) {
		len = (uint32_t)(file_size - current_offset);
	}
	int res = partition->read_file(start_cluster, current_offset, buf, len);
	current_offset += res;
	return res;
}
int File::write(const void* buf, uint32_t len) {
	return -1; //읽기 전용 파일 시스템
}
int File::seek(uint64_t offset) {
	if (offset > file_size) return -1;
	current_offset = offset;
	return 0;
}
uint64_t File::tell() {
	return current_offset;
}
uint64_t File::size() {
	return file_size;
}
void File::close() {
}
PathResolveResult resolve_path(const char* path, Partition* cwd_partition) {
    PathResolveResult result = { nullptr, nullptr };
#define GET_PART(i) ((Partition*)(PARTITION_QUEUE_BASE + PARTITIONSTRUCT_SIZE * (i)))
    // 1. [Case A] 인덱스 접근 (#N/...)
    if (path[0] == '#') {
        uint64_t idx = 0;
        int i = 1;
        // 숫자 파싱
        while (path[i] >= '0' && path[i] <= '9') {
            idx = idx * 10ull + (uint64_t)(path[i] - '0');
            i++;
        }
        if (idx > max_partition_index) return result;
        // 유효성 체크 (슬롯이 사용 중인지)
        Partition* p = GET_PART(idx);
        if (p->flags & 0b1) {
            result.target_partition = p;
            if (path[i] == '/') i++;
            result.relative_path = &path[i];
            return result;
        }
        return result;
    }

    // 2. [Case B] 현재 파티션 루트 (@/...)
    if (path[0] == '@') {
        // [수정 2] 커널 모드(proc == nullptr) 방어
        if (!cwd_partition) return result; // 커널은 @ 사용 불가 (또는 기본 파티션 리턴)

        result.target_partition = cwd_partition;
        int i = 1;
        if (path[i] == '/') i++;
        result.relative_path = &path[i];
        return result;
    }

    // 3. [Case C] 별명 접근 (Name:/...) 혹은 상대 경로
    // 콜론(:)이 있는지 확인
    int colon_idx = -1;
    for (int i = 0; path[i] != 0 && path[i] != '/'; i++) {
        if (path[i] == ':') {
            colon_idx = i;
            break;
        }
    }

    if (colon_idx != -1) {
        char alias_buf[12];
        int len = (colon_idx > 11) ? 11 : colon_idx;
        memcpy(alias_buf, path, len);
        alias_buf[len] = 0;

        // [수정 3] 루프 조건 (<=)
        for (uint64_t i = 0; i <= max_partition_index; i++) {
            Partition* p = GET_PART(i);

            // 살아있는 파티션인지 확인
            if (p->flags & 0b1) {
                if (strncmp(p->alias, alias_buf, 11) == 0) {
                    result.target_partition = p;
                    int next_idx = colon_idx + 1;
                    if (path[next_idx] == '/') next_idx++;
                    result.relative_path = &path[next_idx];
                    return result;
                }
            }
        }
        return result;
    }
    if (!cwd_partition) return result;
    // 4. [Case D] 그냥 상대 경로 (usr/bin/...)
    result.target_partition = cwd_partition;
    result.relative_path = path;
    return result;
}
File* vfs_open(const char* path, Partition* cwd_part, uint64_t cwd_id) {
    // 1. 경로 해석
    PathResolveResult res = resolve_path(path, cwd_part);

    if (res.target_partition == nullptr) return nullptr;

    // 2. Base ID 결정
    uint64_t base_id = 0;
    bool is_partition_changed = (cwd_part != nullptr) && (res.target_partition != cwd_part);
    bool is_explicit_root = (res.relative_path != path);

    if (!is_partition_changed && !is_explicit_root) {
        base_id = cwd_id;
    }

    // 3. 파티션에게 위임 (리턴 타입 File*)
    return res.target_partition->open_file(res.relative_path, base_id);
}

File* kernel_open_file(const char* path) {
    return vfs_open(path, nullptr, 0);
}