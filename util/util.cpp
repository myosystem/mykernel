#include "util/util.h"
int __rand_seed = 123456789;
__attribute__((naked, noinline))
void simple_hlt() {
    __asm__ __volatile__(
        "hlt\n\t"
        "jmp simple_hlt\n\t"
    );
}