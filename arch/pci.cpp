#include "arch/pci.h"
#include "kernel/kernel.h"
#include "arch/io.h"
#include "util/size.h"
#include "mm/allocator"
#include "debug/log.h"
#include "util/memory.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static void pci_write_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1 << 31) | // Enable Bit
        (bus << 16) |
        (slot << 11) |
        (func << 8) |
        (offset & 0xFC)); // 4바이트 정렬
    outl(PCI_CONFIG_ADDRESS, address);
}

// ================= READ 함수들 =================

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    pci_write_address(bus, slot, func, offset);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    pci_write_address(bus, slot, func, offset);
    // 0xCFC + (offset % 4) 주소에서 16비트 읽기
    return inw(PCI_CONFIG_DATA + (offset & 2));
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    pci_write_address(bus, slot, func, offset);
    // 0xCFC + (offset % 4) 주소에서 8비트 읽기
    return inb(PCI_CONFIG_DATA + (offset & 3));
}

// ================= WRITE 함수들 =================

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    pci_write_address(bus, slot, func, offset);
    outl(PCI_CONFIG_DATA, value);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    pci_write_address(bus, slot, func, offset);
    // 정확히 해당 위치의 16비트만 씀 (나머지 16비트는 건드리지 않음)
    outw(PCI_CONFIG_DATA + (offset & 2), value);
}

void pci_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    pci_write_address(bus, slot, func, offset);
    // 정확히 해당 위치의 8비트만 씀
    outb(PCI_CONFIG_DATA + (offset & 3), value);
}


static inline uint16_t pci_cmd_read(uint8_t b, uint8_t s, uint8_t f) {
    return pci_read16(b, s, f, 0x04);
}
static inline void pci_cmd_write(uint8_t b, uint8_t s, uint8_t f, uint16_t v) {
    pci_write16(b, s, f, 0x04, v);
}

pci_bar_info_t pci_get_bar_size(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    pci_bar_info_t info = { 0 };

    // 1. 기존 값 백업
    uint32_t old_low = pci_read32(bus, slot, func, offset);
    uint32_t old_high = 0;

    int is_io = old_low & 0x1;
    int is_64 = (!is_io && (old_low & 0x4));

    // AHCI BAR(0x24)는 스펙상 32비트 레지스터로 취급하는 경우가 많음
    if (offset == 0x24) is_64 = 0;

    if (is_64) old_high = pci_read32(bus, slot, func, offset + 4);

    // 2. 디코딩 잠시 끄기 (Size Probing 안전하게 수행)
    uint16_t cmd_old = pci_cmd_read(bus, slot, func);
    uint16_t cmd_off = cmd_old & ~(uint16_t)(0x1 /*IO*/ | 0x2 /*MEM*/);
    pci_cmd_write(bus, slot, func, cmd_off);

    // 3. 0xFFFFFFFF 써서 사이즈 확인 (Sizing)
    pci_write32(bus, slot, func, offset, 0xFFFFFFFF);
    if (is_64) pci_write32(bus, slot, func, offset + 4, 0xFFFFFFFF);

    uint32_t val_low = pci_read32(bus, slot, func, offset);
    uint32_t val_high = is_64 ? pci_read32(bus, slot, func, offset + 4) : 0;

    // 4. BAR 값 원상복구
    pci_write32(bus, slot, func, offset, old_low);
    if (is_64) pci_write32(bus, slot, func, offset + 4, old_high);

    // 5. 커맨드 복구 및 BusMaster/Memory Enable (AHCI 동작 필수)
    uint16_t cmd_on = (uint16_t)(cmd_old | 0x2 /*MEM*/ | 0x4 /*BusMaster*/);
    pci_cmd_write(bus, slot, func, cmd_on);

    // 6. 주소 계산
    if (is_io) {
        info.addr = old_low & ~0x3ULL;
    }
    else {
        uint64_t addr = old_low & ~0xFULL;
        if (is_64) addr |= ((uint64_t)old_high << 32);
        info.addr = addr;
    }

    // 7. 크기 계산 (Critical Fix 적용됨)
    if (is_io) {
        uint32_t mask = val_low & ~0x3U;
        info.size = (~mask) + 1; // 32bit inversion
    }
    else {
        if (is_64) {
            uint64_t mask = ((uint64_t)val_high << 32) | (val_low & ~0xFULL);
            info.size = (~mask) + 1ull;
        }
        else {
            // [중요] 32비트 BAR는 반드시 uint32_t로 반전 후 64비트로 확장해야 함
            // 안 그러면 상위 32비트가 1로 채워져서 엄청난 크기가 됨
            uint32_t mask = val_low & ~0xFULL;
            info.size = (~mask) + 1ull;
        }
    }

    info.is_mmio = !is_io;
    info.is_64 = is_64;
    return info;
}
//vector<pci_device_t> pci_devices;
void* pci_init() {
    for (int bus = 0; bus < 255; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t vendor_dev = pci_read32(bus, slot, 0, 0x00);
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
    return (void*)(abar.addr + MMIO_BASE);
}
bool setup_msix(uint16_t bus, uint16_t slot, uint16_t func, MSIXConfig cfg) {
    uint16_t status = pci_read16(bus, slot, func, 0x06);
    if (!(status & (1 << 4))) {
		uart_print("Device at bus=");
		uart_print_hex(bus);
		uart_print(", slot=");
		uart_print_hex(slot);
		uart_print(", func=");
		uart_print_hex(func);
		uart_print(" does not have MSI-X capability\n");
		return false;
    }
    // 1. MSI-X Capability 찾기
    uint8_t cap_ptr = pci_read8(bus, slot, func, 0x34);
    uart_print("[CAP] initial cap_ptr="); uart_print_hex(cap_ptr); uart_print("\n");

    while (cap_ptr) {
        uint8_t cap_id = pci_read8(bus, slot, func, cap_ptr);
        uint8_t next = pci_read8(bus, slot, func, cap_ptr + 1);
        uart_print("[CAP] ptr="); uart_print_hex(cap_ptr);
        uart_print(" id=");       uart_print_hex(cap_id);
        uart_print(" next=");     uart_print_hex(next);
        uart_print("\n");
        if (cap_id == 0x11) break;
        cap_ptr = next;
    }
    if (!cap_ptr) {
		uart_print("MSI-X Capability not found for device at bus=");
		uart_print_hex(bus);
		uart_print(", slot=");
		uart_print_hex(slot);
		uart_print(", func=");
		uart_print_hex(func);
		uart_print("\n");
        return false;
    }

    // 2. 테이블 위치 파악
    uint32_t table_info = pci_read32(bus, slot, func, cap_ptr + 0x04);
    uint8_t  bir = table_info & 0x7;
    uint32_t tbl_offset = table_info & ~0x7u;

    // 3. 테이블 주소 계산
    pci_bar_info_t tbl_bar = pci_get_bar_size(bus, slot, func, 0x10 + bir * 4);
    volatile uint32_t* msix_table = (volatile uint32_t*)(tbl_bar.addr + MMIO_BASE);
    if (tbl_bar.addr > phy_page_allocator->get_total_pages() * PageSize) {
        msix_table = (volatile uint32_t*)mmio_bump;
        mmio_bump += tbl_bar.size;
    }
    for (uint64_t off = 0; off < tbl_bar.size; off += 4096) {
        uint64_t result = virt_page_allocator->alloc_virt_page((uint64_t)msix_table + off,
            tbl_bar.addr + off,
            VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    }
	msix_table = (volatile uint32_t*)((uint64_t)msix_table + tbl_offset);
    // 4. Function Mask 켜기
    uint16_t msgctl = pci_read16(bus, slot, func, cap_ptr + 0x02);
    msgctl |= (1 << 15) | (1 << 14); // Enable + Function Mask
    pci_write16(bus, slot, func, cap_ptr + 0x02, msgctl);

    // 5. Entry 0 세팅
    uart_print("[MSIX] before write table[0]="); uart_print_hex(msix_table[0]); uart_print("\n");
    msix_table[0] = 0xFEE00000 | (cfg.lapic_id << 12);
    msix_table[1] = 0x00000000;
    msix_table[2] = cfg.vector;
    msix_table[3] = 0x00000000; // Unmask

    // 6. Function Mask 해제
    msgctl &= ~(1 << 14);
    pci_write16(bus, slot, func, cap_ptr + 0x02, msgctl);
    uart_print("[MSIX] table_info="); uart_print_hex(table_info); uart_print("\n");
    uart_print("[MSIX] bir="); uart_print_hex(bir); uart_print("\n");
    uart_print("[MSIX] tbl_offset="); uart_print_hex(tbl_offset); uart_print("\n");
    uart_print("[MSIX] msix_table addr="); uart_print_hex((uint64_t)msix_table); uart_print("\n");
    uart_print("[MSIX] table[0]="); uart_print_hex(msix_table[0]); uart_print("\n");
    uart_print("[MSIX] table[2]="); uart_print_hex(msix_table[2]); uart_print("\n");
    uart_print("[MSIX] msgctl=");   uart_print_hex(pci_read16(bus, slot, func, cap_ptr + 0x02)); uart_print("\n");
    return true;
}
bool setup_msi(uint16_t bus, uint16_t slot, uint16_t func, MSIConfig cfg) {
    // 1. Capability List 있는지 확인
    uint16_t status = pci_read16(bus, slot, func, 0x06);
    if (!(status & (1 << 4))) {
        uart_print("[MSI] No Capability List\n");
        return false;
    }

    // 2. MSI Capability (0x05) 찾기
    uint8_t cap_ptr = pci_read8(bus, slot, func, 0x34);
    while (cap_ptr) {
        uint8_t cap_id = pci_read8(bus, slot, func, cap_ptr);
        uint8_t next = pci_read8(bus, slot, func, cap_ptr + 1);
        uart_print("[CAP] ptr="); uart_print_hex(cap_ptr);
        uart_print(" id=");       uart_print_hex(cap_id);
        uart_print(" next=");     uart_print_hex(next);
        uart_print("\n");
        if (cap_id == 0x05) break;
        cap_ptr = next;
    }
    if (!cap_ptr) {
        uart_print("[MSI] MSI Capability not found\n");
        return false;
    }

    // 3. Message Control 읽기
    uint16_t msgctl = pci_read16(bus, slot, func, cap_ptr + 0x02);
    bool is64 = (msgctl >> 7) & 0x1;
    uart_print("[MSI] msgctl="); uart_print_hex(msgctl);
    uart_print(" is64=");        uart_print_hex(is64);
    uart_print("\n");

    // 4. Address / Data 세팅
    uint32_t addr_low = 0xFEE00000 | (cfg.lapic_id << 12);
    pci_write32(bus, slot, func, cap_ptr + 0x04, addr_low);

    if (is64) {
        pci_write32(bus, slot, func, cap_ptr + 0x08, 0x00000000); // addr high
        pci_write16(bus, slot, func, cap_ptr + 0x0C, cfg.vector); // data
    }
    else {
        pci_write16(bus, slot, func, cap_ptr + 0x08, cfg.vector); // data
    }

    // 5. Multiple Message Enable=0 (벡터 1개), MSI Enable=1
    msgctl &= ~(0x7 << 4); // MME 클리어
    msgctl |= (0x1 << 0); // MSI Enable
    pci_write16(bus, slot, func, cap_ptr + 0x02, msgctl);

    uart_print("[MSI] addr_low="); uart_print_hex(addr_low);  uart_print("\n");
    uart_print("[MSI] vector=");   uart_print_hex(cfg.vector); uart_print("\n");
    uart_print("[MSI] msgctl=");   uart_print_hex(pci_read16(bus, slot, func, cap_ptr + 0x02)); uart_print("\n");
    return true;
}