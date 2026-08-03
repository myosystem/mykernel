#ifndef __STRING_H__
#define __STRING_H__
#include "util/size.h"
#include "mm/allocator"     // phy_page_allocator, HHDM_BASE, PageSize
#include "util/memory.h"    // memcpy
// 페이지 체인 기반 byte-packed 문자열(바이트 버퍼).
//  - 각 페이지: 앞 8바이트 = 다음 페이지 포인터, 나머지(CAP)는 데이터로 꽉 채움.
//  - queue<char>와 달리 원소 정렬 패딩이 없어서 1바이트=1바이트.
//  - 네트워크 계층 조립용: append(헤더) / += (안쪽 payload) / copy_out / checksum16.
class string {
    static constexpr uint64_t CAP = PageSize - 8;   // 페이지당 데이터 바이트 수(4088)
    void*    front_page;
    void*    back_page;
    uint64_t back_index;   // back_page에서 다음 쓸 위치 (0..CAP)
    uint64_t len_;

    static void* alloc_page() {
        void* p = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
        *((void**)p) = nullptr;   // next 초기화 필수
        return p;
    }
    static void free_chain(void* pg) {
        while (pg) {
            void* next = *((void**)pg);
            phy_page_allocator->put_page((uint64_t)pg - HHDM_BASE);
            pg = next;
        }
    }
    void init_empty() {
        front_page = alloc_page();
        back_page  = front_page;
        back_index = 0;
        len_       = 0;
    }
public:
    string() { init_empty(); }
    string(const uint8_t* p, uint64_t n) { init_empty(); append(p, n); }
    ~string() { free_chain(front_page); }

    // 복사
    string(const string& o) { init_empty(); *this += o; }
    string& operator=(const string& o) {
        if (this == &o) return *this;
        free_chain(front_page);
        init_empty();
        *this += o;
        return *this;
    }
    // 이동 (페이지 넘기고 원본은 빈 상태로)
    string(string&& o) {
        front_page = o.front_page; back_page = o.back_page;
        back_index = o.back_index; len_ = o.len_;
        o.init_empty();
    }
    string& operator=(string&& o) {
        if (this == &o) return *this;
        free_chain(front_page);
        front_page = o.front_page; back_page = o.back_page;
        back_index = o.back_index; len_ = o.len_;
        o.init_empty();
        return *this;
    }

    uint64_t size() const { return len_; }

    // raw 바이트 이어붙이기 (헤더용). 페이지 경계 넘으면 나눠서 memcpy.
    void append(const uint8_t* p, uint64_t n) {
        while (n) {
            uint64_t space = CAP - back_index;
            if (space == 0) {
                void* np = alloc_page();
                *((void**)back_page) = np;   // 현재 페이지에 새 페이지 연결
                back_page = np;
                back_index = 0;
                space = CAP;
            }
            uint64_t chunk = (n < space) ? n : space;
            memcpy((uint8_t*)back_page + 8 + back_index, p, chunk);
            back_index += chunk;
            p += chunk; n -= chunk; len_ += chunk;
        }
    }

    // 다른 string 이어붙이기 (안쪽 payload). s의 유효 바이트만 append.
    string& operator+=(const string& s) {
        void* pg = s.front_page;
        uint64_t remaining = s.len_;
        while (remaining) {
            uint64_t chunk = (remaining < CAP) ? remaining : CAP;   // 꽉 찬 페이지=CAP, 마지막=나머지
            append((const uint8_t*)pg + 8, chunk);
            remaining -= chunk;
            pg = *((void**)pg);
        }
        return *this;
    }
    string& operator+=(uint8_t b) { append(&b, 1); return *this; }

    // 논리 오프셋 off부터 n바이트를 dst로 복사 (fragment slice / dev 전달용).
    void copy_out(uint64_t off, uint8_t* dst, uint64_t n) const {
        void* pg = front_page;
        for (uint64_t skip = off / CAP; skip; skip--) pg = *((void**)pg);
        uint64_t idx = off % CAP;
        while (n) {
            uint64_t space = CAP - idx;
            uint64_t chunk = (n < space) ? n : space;
            memcpy(dst, (const uint8_t*)pg + 8 + idx, chunk);
            dst += chunk; n -= chunk; idx = 0;
            if (n) pg = *((void**)pg);
        }
    }

    // 다른 string src의 [off, off+n) 구간을 이 string 뒤에 이어붙임 (임시 string 없이 바로).
    void append_from(const string& src, uint64_t off, uint64_t n) {
        void* pg = src.front_page;
        for (uint64_t skip = off / CAP; skip; skip--) pg = *((void**)pg);
        uint64_t idx = off % CAP;
        while (n) {
            uint64_t space = CAP - idx;
            uint64_t chunk = (n < space) ? n : space;
            append((const uint8_t*)pg + 8 + idx, chunk);
            n -= chunk; idx = 0;
            if (n) pg = *((void**)pg);
        }
    }

    // [off, off+n) 구간을 새 string으로 떼어냄 (fragment slice용).
    string substr(uint64_t off, uint64_t n) const {
        string s;
        s.append_from(*this, off, n);
        return s;
    }

    // 논리 오프셋 off에 n바이트 덮어쓰기 (예: checksum 계산 후 헤더 필드에 써넣기).
    void write_at(uint64_t off, const uint8_t* p, uint64_t n) {
        void* pg = front_page;
        for (uint64_t skip = off / CAP; skip; skip--) pg = *((void**)pg);
        uint64_t idx = off % CAP;
        while (n) {
            uint64_t space = CAP - idx;
            uint64_t chunk = (n < space) ? n : space;
            memcpy((uint8_t*)pg + 8 + idx, p, chunk);
            p += chunk; n -= chunk; idx = 0;
            if (n) pg = *((void**)pg);
        }
    }

    // 인터넷 체크섬(RFC1071) — off부터 n바이트. 페이지 경계 걸쳐도 16비트 워드 짝을
    // 논리 위치(pos)로 맞춰서 계산. 바이트 순서는 ip_checksum(uint16* 리틀엔디언)과 일치.
    uint16_t checksum16(uint64_t off, uint64_t n) const {
        uint64_t sum = 0;
        void* pg = front_page;
        for (uint64_t skip = off / CAP; skip; skip--) pg = *((void**)pg);
        uint64_t idx = off % CAP;
        uint64_t pos = 0;   // 체크섬 범위 내 논리 위치(짝/홀 판정)
        while (n) {
            uint64_t space = CAP - idx;
            uint64_t chunk = (n < space) ? n : space;
            const uint8_t* d = (const uint8_t*)pg + 8 + idx;
            for (uint64_t i = 0; i < chunk; i++) {
                if (((pos + i) & 1) == 0) sum += d[i];              // 짝수 위치 = low 바이트
                else                      sum += (uint64_t)d[i] << 8; // 홀수 위치 = high 바이트
            }
            pos += chunk; n -= chunk; idx = 0;
            if (n) pg = *((void**)pg);
        }
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);   // 캐리 접기
        return (uint16_t)~sum;                                  // 1의 보수
    }
};
#endif // __STRING_H__
