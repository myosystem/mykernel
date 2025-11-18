#ifndef __UTIL_H__
#define __UTIL_H__
extern int __rand_seed;

static inline int simple_rand() {
    __rand_seed ^= __rand_seed << 13;
    __rand_seed ^= __rand_seed >> 17;
    __rand_seed ^= __rand_seed << 5;
    return __rand_seed;
}
static const char hex_digits[] = "0123456789ABCDEF";
static inline void bytes_to_hex_string(const char* src, int len, char* dst) {
    for (int i = 0; i < len; i++) {
        unsigned char byte = (unsigned char)src[i];
        dst[i * 3 + 0] = hex_digits[(byte >> 4) & 0xF]; // 상위 4비트
        dst[i * 3 + 1] = hex_digits[byte & 0xF];        // 하위 4비트
        dst[i * 3 + 2] = ' ';                           // 간격용
    }
    dst[len * 3] = '\0';  // null-terminate
}
static inline bool is_all_zero(const void* ptr, unsigned long long size) {
    const unsigned char* p = (const unsigned char*)ptr;
    for (unsigned long long i = 0; i < size; i++) {
        if (p[i] != 0) {
            return false;
        }
    }
    return true;
}
extern "C" __attribute__((naked, noinline)) void simple_hlt();
#ifdef _MSC_VER
inline void* operator new(unsigned long long, void* p) noexcept { return p; }
inline void* operator new[](unsigned long long, void* p) noexcept { return p; }
#else
inline void* operator new(unsigned long, void* p) noexcept { return p; }
inline void* operator new[](unsigned long, void* p) noexcept { return p; }
#endif

#endif // __UTIL_H__