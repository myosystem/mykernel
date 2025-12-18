#ifndef __PCI_H__
#define __PCI_H__

#include "util/size.h"
uint8_t  pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t val);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);

#endif /* __PCI_H__ */