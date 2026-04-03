#ifndef __NVME_H__
#define __NVME_H__

#include "util/size.h"
#include "driver/disk.h"

class NVMeDisk : public Disk {
private:
    struct NVMeCmd {
        uint8_t  opc;      // Offset 0
        uint8_t  flags;    // Offset 1 (Fuse + PSDT)
        uint16_t cid;      // Offset 2
        uint32_t nsid;     // Offset 4

        uint64_t rsv1;     // Offset 8 (여기가 8바이트여야 함!)

        uint64_t mptr;     // Offset 16
        uint64_t prp1;     // Offset 24
        uint64_t prp2;     // Offset 32

        uint32_t cdw10;    // Offset 40
        uint32_t cdw11;    // Offset 44
        uint32_t cdw12;    // Offset 48
        uint32_t cdw13;    // Offset 52
        uint32_t cdw14;    // Offset 56
        uint32_t cdw15;    // Offset 60
    } __attribute__((packed));
    struct NVMeCqe {
        uint32_t dw0;
        uint32_t dw1;
        uint16_t sq_head;
        uint16_t sq_id;
        uint16_t cid;
        uint16_t status;  // [15:1] = Status, [0] = Phase
    } __attribute__((packed));
    volatile uint8_t* nvme_base;
    uint64_t queue_phys, queue_virt;  // 페이지 하나로 통합
    uint64_t asq_phys, acq_phys, iosq_phys, iocq_phys;
    uint64_t asq_virt, acq_virt, iosq_virt, iocq_virt;
    uint8_t sq_tail, cq_head, cq_phase;
    uint32_t namespace_id;
    uint32_t stride;
public:
    NVMeDisk(uint16_t bus, uint16_t slot, uint16_t func, uint32_t port_or_ns) : Disk(bus, slot, func, port_or_ns) {}
    ~NVMeDisk();
    void init() override;
    int read_sector(uint64_t lba, uint32_t count, void* buf) override;
    int write_sector(uint64_t lba, uint32_t count, const void* buf) override;
};
#endif // __NVME_H__