#include "pci.h"
#include "kernel.h"
#include "io.h"
#include "size.h"
#include "allocator"
#include "log.h"
#include "memory.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

uint32_t pci_config_read(uint16_t bus, uint16_t slot, uint16_t func, uint16_t offset) {
	uint32_t address;
	// Create configuration address as per Figure 1
	address = (uint32_t)((bus << 16) | (slot << 11) |
						(func << 8) | (offset & 0xfc) | (1 << 31));
	// Write out the address
	outl(PCI_CONFIG_ADDRESS, address);
	// Read in the data
	// (offset & 2) * 8) = 0 will choose the first word of the 32 bits register
	return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
	uint32_t address;
	// Create configuration address as per Figure 1
	address = (uint32_t)((bus << 16) | (slot << 11) |
						(func << 8) | (offset & 0xfc) | (1 << 31));
	// Write out the address
	outl(PCI_CONFIG_ADDRESS, address);
	// Write the data
	// (offset & 2) * 8) = 0 will choose the first word of the 32 bits register
	outl(PCI_CONFIG_DATA, value);
}
typedef struct {
    uint64_t addr;  // BAR 주소
    uint64_t size;  // BAR 크기
    int is_mmio;    // 1 = MMIO, 0 = I/O
    int is_64;      // 1 = 64-bit BAR
} pci_bar_info_t;


static inline uint16_t pci_cmd_read(uint8_t b, uint8_t s, uint8_t f) {
    return (uint16_t)(pci_config_read(b, s, f, 0x04) & 0xFFFF);
}
static inline void pci_cmd_write(uint8_t b, uint8_t s, uint8_t f, uint16_t v) {
    uint32_t val = (pci_config_read(b, s, f, 0x04) & 0xFFFF0000) | v;
    pci_config_write32(b, s, f, 0x04, val);
}

pci_bar_info_t pci_get_bar_size(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    pci_bar_info_t info = { 0 };
    uint32_t old_low = pci_config_read(bus, slot, func, offset);
    uint32_t old_high = 0;

    int is_io = old_low & 0x1;
    int is_64 = (!is_io && (old_low & 0x4));  // <-- 여기에 조건 추가
    if (offset == 0x24) is_64 = 0; // AHCI BAR는 32bit 고정

    if (is_64) old_high = pci_config_read(bus, slot, func, offset + 4);

    // 1) 디코딩 잠시 끄기 (I/O, MEM)
    uint16_t cmd_old = pci_cmd_read(bus, slot, func);
    uint16_t cmd_off = cmd_old & ~(uint16_t)(0x1 /*IO*/ | 0x2 /*MEM*/);
    pci_cmd_write(bus, slot, func, cmd_off);

    // 2) 사이징 프로브
    pci_config_write32(bus, slot, func, offset, 0xFFFFFFFF);
    if (is_64) pci_config_write32(bus, slot, func, offset + 4, 0xFFFFFFFF);

    uint32_t val_low = pci_config_read(bus, slot, func, offset);
    uint32_t val_high = is_64 ? pci_config_read(bus, slot, func, offset + 4) : 0;

    // 3) BAR 원복
    pci_config_write32(bus, slot, func, offset, old_low);
    if (is_64) pci_config_write32(bus, slot, func, offset + 4, old_high);

    // 4) 커맨드 원복 + 이후 사용 위해 MEM/버스마스터 켜기
    uint16_t cmd_on = (uint16_t)(cmd_old | 0x2 /*MEM*/ | 0x4 /*BusMaster*/);
    pci_cmd_write(bus, slot, func, cmd_on);

    // 주소 계산
    if (is_io) {
        info.addr = old_low & ~0x3ULL;
    }
    else {
        uint64_t addr = old_low & ~0xFULL;
        if (is_64) addr |= ((uint64_t)old_high << 32);
        info.addr = addr;
    }

    // 크기 계산
    if (is_io) {
        uint32_t mask = val_low & ~0x3U;
		uart_print("PCI IO BAR size mask=");
        info.size = (~mask) + 1ull;
    }
    else {
        uint64_t mask = ((uint64_t)val_high << 32) | (val_low & ~0xFULL);
        if (is_64) {
            info.size = (~mask) + 1ull;
        }
        else {
            info.size = (uint32_t)((~mask) + 1ull);
        }
    }

    info.is_mmio = !is_io;
    info.is_64 = is_64;
    return info;
}
HBA_MEM* pci_init() {
    /*
    for (int bus = 0; bus < 8; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t vendor_dev = pci_config_read(bus, slot, 0, 0x00);
            if (vendor_dev != 0xFFFFFFFF) {
                uint16_t vendor_id = vendor_dev & 0xFFFF;
                uint16_t device_id = (vendor_dev >> 16) & 0xFFFF;
                uart_print("PCI device found: bus=");
                uart_print_hex(bus);
                uart_print(", slot=");
                uart_print_hex(slot);
                uart_print(", vendor_id=");
                uart_print_hex(vendor_id);
                uart_print(", device_id=");
                uart_print_hex(device_id);
				uart_print("\n");
            }
        }
    }
    */
    auto abar = pci_get_bar_size(
        bootinfo->bootdev.pci_bus,
        bootinfo->bootdev.pci_slot,
        bootinfo->bootdev.pci_func,
        0x24);
	uart_print("AHCI BAR addr=");
	uart_print_hex(abar.addr);
	uart_print(", size=");
    uart_print_hex((abar.size + 0xFFF) & ~0xFFFULL);
	uart_print("\n");
    //__asm__ __volatile__("hlt;");
    virt_page_allocator->alloc_virt_pages_range(
        abar.addr + MMIO_BASE,   // 가상 시작
        abar.addr,               // 물리 시작 (그대로)
        (abar.size + 0xFFF) & ~0xFFFULL,
        VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD
    );
	probe_ports((HBA_MEM*)(abar.addr + MMIO_BASE));
    return (HBA_MEM*)(abar.addr + MMIO_BASE);
}