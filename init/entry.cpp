extern "C" void main();
extern "C" __attribute__((naked, section(".entry"))) void _start() {
    __asm__ __volatile__(
        //"hlt;"
        "jmp main;"
        "hlt;"
    );
}