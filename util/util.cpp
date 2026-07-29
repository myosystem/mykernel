#include "util/util.h"
int __rand_seed = 123456789;
__attribute__((naked, noinline))
void simple_hlt() {
    __asm__ __volatile__(
        "hlt\n\t"
        "jmp simple_hlt\n\t"
    );
}
__attribute__((naked, noinline))
unsigned long long call_xhci(...) {
    __asm__ __volatile__(
        "mov rax, 0x35\n\t"
        "int 0x81\n\t"
        "ret\n\t"
		::: "rax", "rcx", "r11", "memory"
    );
}
__attribute__((naked,noinline))
unsigned long long yield() {
    __asm__ __volatile__(
        "mov rax, 34\n\t"
        "int 0x80\n\t"
        "ret\n\t"
        ::: "rax", "rcx", "r11", "memory"
    );
}
__attribute__((naked, noinline))
unsigned long long call_msg_block() {
    __asm__ __volatile__(
        "mov rax, 0x4\n\t"
        "int 0x81\n\t"
        "ret\n\t"
        ::: "rax", "rcx", "r11", "memory"
    );
}
__attribute__((naked, noinline))
unsigned long long child_zombie_wait() {
	__asm__ __volatile__(
		"mov rax, 0x6\n\t"
		"int 0x81\n\t"
		"ret\n\t"
		::: "rax", "rcx", "r11", "memory"
	);
}
__attribute__((naked, noinline))
unsigned long long simple_wait() {
    __asm__ __volatile__(
        "mov rax, 0x5\n\t"
        "int 0x81\n\t"
        "ret\n\t"
        ::: "rax", "rcx", "r11", "memory"
    );
}
uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
uint32_t swap32(uint32_t v) { return ((v >> 24) & 0xff) | ((v << 8) & 0xff0000) | ((v >> 8) & 0xff00) | ((v << 24) & 0xff000000); }