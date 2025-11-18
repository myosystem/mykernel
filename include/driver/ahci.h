#ifndef __AHCI_H__
#define __AHCI_H__
#include "util/size.h"
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

void probe_ports(HBA_MEM* abar);
int ahci_read(volatile HBA_PORT* port, uint64_t lba, uint32_t count, void* mmio_based_buf);
int ahci_identify(volatile HBA_PORT* port, void* mmio_based_buf);
#endif /*__AHCI_H__*/