#include "util/memory.h"

void* memcpy(void* dest, const void* src, unsigned long long size) {
    // [Intel Syntax]
    // 1. RCX = size / 8 (QWORD 개수)
    // 2. rep movsq (8바이트씩 고속 복사)
    // 3. RCX = size % 8 (나머지 바이트)
    // 4. rep movsb (나머지 처리)
    __asm__ __volatile__(
        "mov rcx, rdx      \n\t"  // rdx(size)를 rcx에 복사
        "shr rcx, 3        \n\t"  // size / 8 (Shift Right 3)
        "rep movsq         \n\t"  // 8바이트 단위 복사
        "and rdx, 7        \n\t"  // size % 8 (나머지 구하기)
        "mov rcx, rdx      \n\t"  // 나머지를 rcx에 넣음
        "rep movsb         \n\t"  // 나머지 1바이트 단위 복사
        :
    : "D"(dest), "S"(src), "d"(size) // RDI=dest, RSI=src, RDX=size
        : "rcx", "memory" // RCX 파괴됨, 메모리 변경됨
        );
    return dest;
}

void* memset(void* dest, int value, unsigned long long size) {
    // [Intel Syntax]
    // 1. AL(value)를 RAX 전체에 복사 (0xAB -> 0xABAB...ABAB)
    // 2. 8바이트씩 쓰기 (rep stosq)
    // 3. 나머지 바이트 쓰기 (rep stosb)
    __asm__ __volatile__(
        "movzx eax, sil    \n\t"  // value(esi의 하위 8비트)를 eax에 로드
        "mov ah, al        \n\t"  // ax = 0xABAB
        "mov rcx, rax      \n\t"  // rcx = 0x000000000000ABAB
        "shl rcx, 16       \n\t"  // rcx = 0x00000000ABAB0000
        "or  rax, rcx      \n\t"  // rax = 0x00000000ABABABAB
        "mov rcx, rax      \n\t"
        "shl rcx, 32       \n\t"
        "or  rax, rcx      \n\t"  // rax = 0xABABABABABABABAB (완성!)

        "mov rcx, rdx      \n\t"  // size를 rcx로
        "shr rcx, 3        \n\t"  // size / 8
        "rep stosq         \n\t"  // 8바이트씩 한 번에 씀 (메모리 대역폭 풀가동)

        "and rdx, 7        \n\t"  // 나머지
        "mov rcx, rdx      \n\t"
        "rep stosb         \n\t"  // 찌꺼기 처리
        :
    : "D"(dest), "S"(value), "d"(size) // RDI=dest, RSI=value(int지만 sil씀), RDX=size
        : "rax", "rcx", "memory"
        );
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    int result;
    // [Intel Syntax - Optimized by User]
    // 1. SUB AL, CL을 먼저 해버림 (결과 저장)
    // 2. 0이 아니면(다르면) 바로 루프 탈출 -> AL에 이미 정답 들어있음!
    // 3. 0이면(같으면) CL이 NULL인지 확인 -> 둘 다 끝났으면 종료
    __asm__ __volatile__ (
        "xor rax, rax       \n\t" // 반환값 32비트 확장을 위해 상위 비트 클리어
        "xor rcx, rcx       \n\t"

    "1:                     \n\t"
        "mov al, [rdi]      \n\t" // s1 로드
        "mov cl, [rsi]      \n\t" // s2 로드
        
        "sub al, cl         \n\t" // [핵심] 그냥 빼버림! (CMP 안 씀)
        "jnz 2f             \n\t" // 0이 아니면(다르면) 즉시 종료 (AL에 답 있음)

        "test cl, cl        \n\t" // 둘 다 같아서 AL=0인 상태. 근데 CL이 0(NULL)인가?
        "jz 2f              \n\t" // 그렇다면 문자열 끝. 종료. (AL=0인 상태로 리턴)

        "inc rdi            \n\t" // 포인터 증가
        "inc rsi            \n\t"
        "jmp 1b             \n\t" // 루프

    "2:                     \n\t"
        "movsx eax, al      \n\t" // AL(8bit)을 int(32bit)로 부호 확장
        : "=a"(result)
        : "D"(s1), "S"(s2)
        : "rcx", "memory"
    );
    return result;
}
int strncmp(const char* s1, const char* s2, unsigned long long n) {
    int result;
    __asm__ __volatile__ (
        "xor rax, rax       \n\t" // 반환값 32비트 확장을 위해 상위 비트 클리어
        "xor rcx, rcx       \n\t"
    "1:                     \n\t"
        "test rdx, rdx      \n\t" // n이 0인가?
        "jz 2f              \n\t" // 0이면 종료 (AL=0인 상태로 리턴)
        "mov al, [rdi]      \n\t" // s1 로드
        "mov cl, [rsi]      \n\t" // s2 로드
        
        "sub al, cl         \n\t" // [핵심] 그냥 빼버림! (CMP 안 씀)
        "jnz 2f             \n\t" // 0이 아니면(다르면) 즉시 종료 (AL에 답 있음)
        "test cl, cl        \n\t" // 둘 다 같아서 AL=0인 상태. 근데 CL이 0(NULL)인가?
        "jz 2f              \n\t" // 그렇다면 문자열 끝. 종료. (AL=0인 상태로 리턴)
        "inc rdi            \n\t" // 포인터 증가
        "inc rsi            \n\t"
        "dec rdx            \n\t" // n 감소
        "jmp 1b             \n\t" // 루프
    "2:                     \n\t"
        "movsx eax, al      \n\t" // AL(8bit)을 int(32bit)로 부호 확장
        : "=a"(result)
        : "D"(s1), "S"(s2), "d"(n)
        : "rcx", "memory"
    );
    return result;
}
static const unsigned char cmaptab[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x40,
    // 'A'~'Z' -> 'a'~'z' 매핑
    0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
    0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C,
    0x6D, 0x6E, 0x6F, 0x70,
	0x71, 0x72, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A,
    0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60,
    // 'a'~'z'는 그대로
    0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
    0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C,
	0x6D, 0x6E, 0x6F, 0x70,
    0x71, 0x72, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A,
    0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
    // 나머지 값들은 그대로
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
    0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
	0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
	0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};
int strcasecmp(const char* s1, const char* s2) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    unsigned char c1, c2;

    do {
        // 테이블 조회로 분기(Branch) 없이 대소문자 통일
        c1 = cmaptab[*p1++];
        c2 = cmaptab[*p2++];
    } while (c1 && c1 == c2);

    return c1 - c2;
}
/*
void* memcpy(void* dest, const void* src, unsigned long long size) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    while (size--) {
        *d++ = *s++;
    }

    return dest;
}

void* memset(void* dest, int value, unsigned long long size) {
    unsigned char* d = (unsigned char*)dest;
    while (size--) {
        *d++ = (unsigned char)value;
    }
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}
*/