#include "arch/controller.h"
#include "util/size.h"
void* Controller::operator new(size_t size) {
    uint64_t mem = CONTROLLER_QUEUE_BASE;
    uint64_t index = 0;
    while (((Controller*)(mem))->state == 1) {
        mem += CONTROLLERSTRUCT_SIZE;
        index++;
    }
    ((Controller*)(mem))->state = 1;
    ((Controller*)(mem))->controler_id = index;
    return (void*)mem;
}
void Controller::operator delete(void* ptr) {
    Controller* p = (Controller*)ptr;
    p->state = 0;
}
uint64_t Controller::get_type() {
    return 0;
}