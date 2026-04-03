#ifndef __XHCI_H__
#define __XHCI_H__
#include "driver/disk.h"
#include "driver/xhci_p.h"
#include "arch/xhci_c.h"

class XHCIDisk : public Disk {
public:
	XHCIDisk(uint16_t bus, uint16_t slot, uint16_t func, uint32_t port_or_ns, XHCIProtocol* protocol)
		: Disk(bus, slot, func, port_or_ns), protocol(protocol) {}
	~XHCIDisk() override;
	void init() override;
	int read_sector(uint64_t lba, uint32_t count, void* buf) override;
	int write_sector(uint64_t lba, uint32_t count, const void* buf) override;
private:
	XHCIProtocol* protocol;
};

#endif // __XHCI_H__