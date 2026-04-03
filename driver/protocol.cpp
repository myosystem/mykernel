#include "driver/protocol.h"

void* Protocol::operator new(size_t size) {
    uint64_t mem = PROTOCOL_QUEUE_BASE;
    uint64_t index = 0;
    while (((Protocol*)(mem))->state == 1) {
        mem += PROTOCOL_STRUCT_SIZE;
        index++;
    }
    ((Protocol*)(mem))->state = 1;
    ((Protocol*)(mem))->protocol_id = index;
    return (void*)mem;
}
void Protocol::operator delete(void* ptr) {
    Protocol* p = (Protocol*)ptr;
    p->state = 0;
}