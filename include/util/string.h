#ifndef __STRING_H__
#define __STRING_H__
#include "util/size.h"
#include "mm/allocator"     // phy_page_allocator, HHDM_BASE, PageSize
#include "util/memory.h"    // memcpy
// 페이지 체인 byte-packed 버퍼 (deque). 앞/뒤 둘 다 성장.
//  - 각 페이지: 앞 8B = 다음 페이지 포인터(next), 나머지 [8,4096) 데이터.
//  - head: front_page 내 논리 0 위치, tail: back_page 내 끝 위치.
//  - append(뒤) / prepend(앞, 헤더용, 복사 없이 head--) / 넘치면 페이지만 추가(rebuild 없음).
//  - singly-linked(next만)라 prev 불필요.
class string {
    static constexpr uint32_t PGSZ     = PageSize;      // 4096
    static constexpr uint32_t LINK     = 8;             // next 포인터 자리
    static constexpr uint32_t HEADROOM = 128;           // 앞 초기 예약(헤더 최대치)
    void*    front_page;
    void*    back_page;
    uint32_t head;   // front_page 내 논리 0 오프셋 [LINK, PGSZ]
    uint32_t tail;   // back_page  내 끝 오프셋   [LINK, PGSZ]
    uint64_t len_;

    static void* alloc_page() {
        void* p = (void*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
        *((void**)p) = nullptr;   // next 초기화
        return p;
    }
    static void free_chain(void* pg) {
        while (pg) { void* n = *((void**)pg); phy_page_allocator->put_page((uint64_t)pg - HHDM_BASE); pg = n; }
    }
    void init_empty() {
        front_page = back_page = alloc_page();
        head = tail = LINK + HEADROOM;   // 앞에 HEADROOM 비워둔 상태로 시작
        len_ = 0;
    }
public:
    string() { init_empty(); }
    string(const uint8_t* p, uint32_t n) { init_empty(); append(p, n); }
    ~string() { free_chain(front_page); }

    string(const string& o) { init_empty(); *this += o; }
    string& operator=(const string& o) {
        if (this != &o) { free_chain(front_page); init_empty(); *this += o; }
        return *this;
    }
    string(string&& o) {
        front_page = o.front_page; back_page = o.back_page;
        head = o.head; tail = o.tail; len_ = o.len_;
        o.init_empty();
    }
    string& operator=(string&& o) {
        if (this != &o) {
            free_chain(front_page);
            front_page = o.front_page; back_page = o.back_page;
            head = o.head; tail = o.tail; len_ = o.len_;
            o.init_empty();
        }
        return *this;
    }

    uint64_t size() const { return len_; }

    // 뒤에 이어붙이기 (데이터)
    void append(const uint8_t* p, uint32_t n) {
        while (n) {
            if (tail == PGSZ) {                    // back 페이지 꽉 참 -> 뒤에 페이지 추가
                void* np = alloc_page();
                *((void**)back_page) = np;
                back_page = np; tail = LINK;
            }
            uint32_t space = PGSZ - tail;
            uint32_t chunk = (n < space) ? n : space;
            memcpy((uint8_t*)back_page + tail, p, chunk);
            tail += chunk; p += chunk; n -= chunk; len_ += chunk;
        }
    }

    // 앞에 붙이기 (헤더). p[0..n)이 논리 앞. 복사 없이 head--; 헤드룸 소진되면 앞 페이지 추가.
    void prepend(const uint8_t* p, uint32_t n) {
        while (n) {
            if (head == LINK) {                    // front 헤드룸 소진 -> 앞에 페이지 추가
                void* np = alloc_page();
                *((void**)np) = front_page;
                front_page = np; head = PGSZ;
            }
            uint32_t fit = head - LINK;
            uint32_t chunk = (n < fit) ? n : fit;
            // 남은 p의 마지막 chunk 바이트를 [head-chunk, head)에 (뒤쪽이 기존 데이터에 인접)
            memcpy((uint8_t*)front_page + head - chunk, p + n - chunk, chunk);
            head -= chunk; n -= chunk; len_ += chunk;
        }
    }

    string& operator+=(const string& s) {
        void* pg = s.front_page;
        uint32_t pstart = s.head;
        uint32_t pend = (pg == s.back_page) ? s.tail : PGSZ;
        uint64_t remaining = s.len_;
        while (remaining) {
            uint32_t span = pend - pstart;
            uint32_t chunk = (remaining < span) ? (uint32_t)remaining : span;
            append((const uint8_t*)pg + pstart, chunk);
            remaining -= chunk;
            if (remaining) { pg = *((void**)pg); pstart = LINK; pend = (pg == s.back_page) ? s.tail : PGSZ; }
        }
        return *this;
    }
    string& operator+=(uint8_t b) { append(&b, 1); return *this; }

    // 논리 off로 페이지/오프셋 이동 (front는 head, 그 외는 LINK 시작; back은 tail에서 끝)
    void locate(uint64_t off, void*& pg, uint32_t& idx, uint32_t& pend) const {
        pg = front_page;
        uint32_t pstart = head;
        pend = (pg == back_page) ? tail : PGSZ;
        while (off >= (uint64_t)(pend - pstart)) {
            off -= (pend - pstart);
            pg = *((void**)pg); pstart = LINK;
            pend = (pg == back_page) ? tail : PGSZ;
        }
        idx = pstart + (uint32_t)off;
    }

    void copy_out(uint64_t off, uint8_t* dst, uint64_t n) const {
        void* pg; uint32_t idx, pend; locate(off, pg, idx, pend);
        while (n) {
            uint32_t avail = pend - idx;
            uint32_t chunk = (n < avail) ? (uint32_t)n : avail;
            memcpy(dst, (const uint8_t*)pg + idx, chunk);
            dst += chunk; n -= chunk;
            if (n) { pg = *((void**)pg); idx = LINK; pend = (pg == back_page) ? tail : PGSZ; }
        }
    }

    void write_at(uint64_t off, const uint8_t* p, uint64_t n) {
        void* pg; uint32_t idx, pend; locate(off, pg, idx, pend);
        while (n) {
            uint32_t avail = pend - idx;
            uint32_t chunk = (n < avail) ? (uint32_t)n : avail;
            memcpy((uint8_t*)pg + idx, p, chunk);
            p += chunk; n -= chunk;
            if (n) { pg = *((void**)pg); idx = LINK; pend = (pg == back_page) ? tail : PGSZ; }
        }
    }

    void write_at(uint64_t off, const string& str) {
        uint8_t* page = (uint8_t*)str.front_page;
        uint64_t pstart = str.head;
        uint64_t pend = (page == str.back_page) ? str.tail : str.PGSZ;
        uint64_t writed = 0;
        while (writed < str.size()) {
            write_at(off, page + pstart, (pend - pstart));
            off += (pend - pstart);
            writed += (pend - pstart);
            page = *((uint8_t**)page); pstart = str.LINK;
            pend = (page == str.back_page) ? str.tail : str.PGSZ;
        }
    }

    // 다른 string src의 [off, off+n)을 이 string 뒤에 이어붙임 (임시 없이)
    void append_from(const string& src, uint64_t off, uint64_t n) {
        void* pg; uint32_t idx, pend; src.locate(off, pg, idx, pend);
        while (n) {
            uint32_t avail = pend - idx;
            uint32_t chunk = (n < avail) ? (uint32_t)n : avail;
            append((const uint8_t*)pg + idx, chunk);
            n -= chunk;
            if (n) { pg = *((void**)pg); idx = LINK; pend = (pg == src.back_page) ? src.tail : PGSZ; }
        }
    }

    string substr(uint64_t off, uint64_t n) const {
        string s; s.append_from(*this, off, n); return s;
    }

    // 인터넷 체크섬(RFC1071) — off부터 n바이트. 16비트 짝은 논리 위치(pos)로 맞춤(ip_checksum과 동일 순서).
    uint16_t checksum16(uint64_t off, uint64_t n) const {
        uint32_t sum = 0; uint64_t pos = 0;
        void* pg; uint32_t idx, pend; locate(off, pg, idx, pend);
        while (n) {
            uint32_t avail = pend - idx;
            uint32_t chunk = (n < avail) ? (uint32_t)n : avail;
            const uint8_t* d = (const uint8_t*)pg + idx;
            for (uint32_t i = 0; i < chunk; i++) {
                if (((pos + i) & 1) == 0) sum += d[i];
                else                      sum += (uint32_t)d[i] << 8;
            }
            pos += chunk; n -= chunk;
            if (n) { pg = *((void**)pg); idx = LINK; pend = (pg == back_page) ? tail : PGSZ; }
        }
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        return (uint16_t)~sum;
    }
};
#endif // __STRING_H__
