#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__
#include "util/size.h"
#include "util/new.h"
#include "kernel/kernel.h"
#define CONTROLLER_QUEUE_BASE 0xFFFF848000000000ULL
#define CONTROLLERSTRUCT_SIZE 0x200
class Controller : public NewObject<pml4_addr(PML4::CONTROLLER_HEAP), CONTROLLERSTRUCT_SIZE, nullptr, nullptr> {
protected:
    uint8_t pci_bus, pci_slot, pci_func;
private:
    uint64_t controler_id;
public:
    Controller(uint8_t bus, uint8_t slot, uint8_t func)
        : pci_bus(bus), pci_slot(slot), pci_func(func) {}
    virtual ~Controller() {}
    virtual void init() = 0;
    virtual uint64_t get_type();
    void (*on_disk_found)(class Disk* disk) = nullptr;
};
#endif // __CONTROLLER_H__