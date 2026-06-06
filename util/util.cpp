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
        "mov rax, 0x0\n\t"
        "int 0x81\n\t"
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