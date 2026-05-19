#include "driver/xhci.h"
#include "driver/xhci_p.h"

XHCIDisk::~XHCIDisk() {
	delete protocol;
}
void XHCIDisk::init() {
	//protocol->initialize();
}

int XHCIDisk::read_sector(uint64_t lba, uint32_t count, void* buf) {
    uart_print("func name ");
    uart_print(__PRETTY_FUNCTION__);
    uart_print("\n");
    uint8_t cmd[10] = { 0 };
    cmd[0] = 0x28;
    cmd[2] = (lba >> 24) & 0xFF;
    cmd[3] = (lba >> 16) & 0xFF;
    cmd[4] = (lba >> 8) & 0xFF;
    cmd[5] = (lba) & 0xFF;
    cmd[7] = (count >> 8) & 0xFF;
    cmd[8] = (count) & 0xFF;

    return protocol->execute_transaction(
        cmd, 10,
        buf, count * 512,
        Direction::In
    ) ? 0 : -1;
}

int XHCIDisk::write_sector(uint64_t lba, uint32_t count, const void* buf) {
    uint8_t cmd[10] = { 0 };
    cmd[0] = 0x2A;
    cmd[2] = (lba >> 24) & 0xFF;
    cmd[3] = (lba >> 16) & 0xFF;
    cmd[4] = (lba >> 8) & 0xFF;
    cmd[5] = (lba) & 0xFF;
    cmd[7] = (count >> 8) & 0xFF;
    cmd[8] = (count) & 0xFF;
    
    return protocol->execute_transaction(
        cmd, 10,
        const_cast<void*>(buf), count * 512,
        Direction::Out
    ) ? 0 : -1;
}