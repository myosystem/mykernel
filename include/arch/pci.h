#ifndef __PCI_H__
#define __PCI_H__
#include "util/vector.h"
#include "util/size.h"
uint8_t  pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
struct pci_device_t {
    uint8_t bus, slot, func;
    uint8_t type;  // AHCI, NVMe 등
    uint64_t bar_addr;
};
typedef struct {
    uint64_t addr;  // BAR 주소
    uint64_t size;  // BAR 크기
    int is_mmio;    // 1 = MMIO, 0 = I/O
    int is_64;      // 1 = 64-bit BAR
} pci_bar_info_t;
//extern vector<pci_device_t> pci_devices;
pci_bar_info_t pci_get_bar_size(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index);
#endif /* __PCI_H__ */