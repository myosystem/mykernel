#ifndef __AHCI_H__
#define __AHCI_H__
#include "util/size.h"
#include "driver/disk.h"

#define HBA_PORT_DET_MASK 0x0F
#define HBA_PORT_DET_PRESENT 0x03
#define HBA_PORT_IPM_MASK (0x0F << 8)
#define HBA_PORT_IPM_ACTIVE (0x01 << 8)

#define SATA_SIG_ATAPI 0xEB140101
#define SATA_SIG_SEMB  0xC33C0101
#define SATA_SIG_PM    0x96690101
#define SATA_SIG_SATA  0x00000101

struct HBA_PORT {
    volatile uint32_t clb;
    volatile uint32_t clbu;
    volatile uint32_t fb;
    volatile uint32_t fbu;
    volatile uint32_t is;
    volatile uint32_t ie;
    volatile uint32_t cmd;
    volatile uint32_t rsv0;
    volatile uint32_t tfd;
    volatile uint32_t sig;
    volatile uint32_t ssts;
    volatile uint32_t sctl;
    volatile uint32_t serr;
    volatile uint32_t sact;
    volatile uint32_t ci;
    volatile uint32_t sntf;
    volatile uint32_t fbs;
    volatile uint32_t rsv1[11];
    volatile uint32_t vendor[4];
} __attribute__((packed));

struct HBA_MEM {
    volatile uint32_t cap;
    volatile uint32_t ghc;
    volatile uint32_t is;
    volatile uint32_t pi;
    volatile uint32_t vs;
    volatile uint32_t ccc_ctl;
    volatile uint32_t ccc_pts;
    volatile uint32_t em_loc;
    volatile uint32_t em_ctl;
    volatile uint32_t cap2;
    volatile uint32_t bohc;
    volatile uint8_t  rsv[0xA0 - 0x2C];
    volatile uint8_t  vendor[0x100 - 0xA0];
    volatile HBA_PORT ports[32];
} __attribute__((packed));
class AHCIDisk : public Disk{
private:
    volatile HBA_PORT* port;
    uint64_t dma_phys_base;
    uint64_t dma_virt_base;
    void start_cmd();
    void stop_cmd();
public:
	AHCIDisk(uint8_t bus, uint8_t slot, uint8_t func) : Disk(bus, slot, func) {}
    ~AHCIDisk();
    void init() override;
    int read_sector(uint64_t lba, uint32_t count, void* buf) override;
};
#endif /*__AHCI_H__*/