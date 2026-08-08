#include "driver/e1000.h"
#include "mm/allocator"
#include "arch/pci.h"
#include "arch/handler.h"
#include "arch/lapic.h"
E1000::E1000(uint16_t bus, uint16_t slot, uint16_t func, uint32_t ip) : Controller(bus, slot, func), Ethernet(ip) {

}
E1000::~E1000() {
	if (!mmio_base) return;
	for (uint64_t off = 0; off < bar_size; off += 4096)
		virt_page_allocator->free_virt_page((uint64_t)mmio_base + off);
}
void E1000::init() {
	pci_bar_info_t bar = pci_get_bar_size(pci_bus, pci_slot, pci_func, 0x10);
	uint16_t cmd = pci_read16(pci_bus, pci_slot, pci_func, 0x04);
	cmd |= (1u << 2) | (1u << 1);
	pci_write16(pci_bus, pci_slot, pci_func, 0x04, cmd);
    mmio_base = (volatile uint8_t*)(bar.addr + MMIO_BASE);
    bar_size = bar.size;
    if (bar.addr > phy_page_allocator->get_total_pages() * PageSize) {
        mmio_base = (volatile uint8_t*)mmio_bump;
        mmio_bump += bar.size;
    }
    for (uint64_t off = 0; off < bar.size; off += 4096) {
        uint64_t result = virt_page_allocator->alloc_virt_page((uint64_t)mmio_base + off,
            bar.addr + off,
            VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    }
    volatile uint32_t* imc      = (volatile uint32_t*)(mmio_base + E1000_IMC);
    volatile uint32_t* ctrl     = (volatile uint32_t*)(mmio_base + E1000_CTRL);
    volatile uint32_t* status   = (volatile uint32_t*)(mmio_base + E1000_STATUS);
    volatile uint32_t* mta      = (volatile uint32_t*)(mmio_base + E1000_MTA);
    volatile uint32_t* ral      = (volatile uint32_t*)(mmio_base + E1000_RAL0);
    volatile uint32_t* rah      = (volatile uint32_t*)(mmio_base + E1000_RAH0);
    *imc = 0xFFFFFFFF;
    *ctrl |= CTRL_RESET;
    while (*ctrl & CTRL_RESET);
    *imc = 0xFFFFFFFF;
    *ctrl |= CTRL_SLU;
    while (!(*status & STATUS_LU));
    volatile uint32_t* eerd = (volatile uint32_t*)(mmio_base + E1000_EERD);
    uint8_t mac[6];
    uint32_t E1000_EERD_ADDR_SHIFT, E1000_EERD_DONE;
    *eerd = 0x01;
    while (1) {
        if (*eerd & 0x10) { E1000_EERD_DONE = 0x10; E1000_EERD_ADDR_SHIFT = 8; break; }
        if (*eerd & 0x02) { E1000_EERD_DONE = 0x02; E1000_EERD_ADDR_SHIFT = 2; break; }
    }
    for (int i = 0; i < 3; i++) {
        *eerd = (i << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START;
        while (!(*eerd & E1000_EERD_DONE));
        uint16_t data = *eerd >> E1000_EERD_DATA_SHIFT;
        mac[i * 2] = data & 0xFF;
        mac[i * 2 + 1] = (data >> 8) & 0xFF;
    }
    memcpy(src_mac, mac, 6);
    uart_print("E1000 MAC Address: ");
    for (int i = 0; i < 6; i++) {
        uart_print_hex(mac[i]);
        if (i < 5) uart_print(":");
    }
    uart_print("\n");
    *ral = mac[0] | mac[1] << 8 | mac[2] << 16 | mac[3] << 24;
    *rah = mac[4] | mac[5] << 8 | RAH_AV;
    for (int i = 0; i < 128; i++) {
        mta[i] = 0;
    }
    ring = new (ring_buf) TXRing(mmio_base);
    rx_ring = new (rx_ring_buf) RXRing(mmio_base);
    bool ok = setup_msix(pci_bus, pci_slot, pci_func, { 0x36, 0 });
    if (ok) {
        volatile uint32_t* ivar = (volatile uint32_t*)(mmio_base + E1000_IVAR);
        *ivar = (E1000_IVAR_INT_ALLOC_VALID << E1000_IVAR_RXQ0_SHIFT)   // RXQ0 -> 벡터 0
            | (E1000_IVAR_INT_ALLOC_VALID << E1000_IVAR_TXQ0_SHIFT)   // TXQ0 -> 벡터 0
            | (E1000_IVAR_INT_ALLOC_VALID << E1000_IVAR_OTHER_SHIFT); // Other -> 벡터 0
        volatile uint32_t* ims = (volatile uint32_t*)(mmio_base + E1000_IMS);
        *ims = 0x00100000 /*RXQ0*/ | 0x00400000 /*TXQ0*/ | 0x01000000 /*OTHER*/;
    }
    else {
        ok = setup_msi(pci_bus, pci_slot, pci_func, { 0x36, 0 });
        if (ok) {
            volatile uint32_t* ims = (volatile uint32_t*)(mmio_base + E1000_IMS);
            *ims = INT_RXT0 | INT_RXDMT0 | INT_RXO;
        }
    }
}

void E1000::hw_send(string& packet) {
    if (packet.size() > max_packet_size()) return;
    Page data;
    packet.copy_out(0, data.data(), packet.size());
    ring->enqueue(data,packet.size());
}
void E1000::handle_irq() {
    volatile uint32_t* icr = (volatile uint32_t*)(mmio_base + E1000_ICR);
    uint32_t cause = *icr;         // 읽으면 자동 클리어
    if (cause == 0) return;        // 내 인터럽트 아님 (공유 벡터일 수도 있으니)

    string packet;
    while (rx_ring->poll(packet)) {
        recv(packet);               // Ethernet::recv
    }
}

extern vector<NetDevice*>* netdevices;

__attribute__((interrupt))
void e1000_handler(interrupt_frame_t* frame) {
    for (int i = 0; i < netdevices->size(); i++) {
        ((E1000*)(*netdevices)[i])->handle_irq();
    }
    lapic_eoi();
}