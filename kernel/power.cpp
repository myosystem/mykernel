#include "kernel/power.h"
#include "arch/io.h"
#include "arch/acpi.h"
#include "driver/disk.h"
extern bool booting;

bool power_off() {
    // Proper ACPI shutdown: port + SLP_TYPa are read from THIS machine's tables
    // (PM1a_CNT via FADT, SLP_TYPa via _S5), so it works on real hardware too.
    if (g_s5_valid && g_pm1a_cnt) {
        // enable ACPI mode if the firmware left it off (SCI_EN = bit0 of PM1a_CNT)
        if (g_smi_cmd && !(inw((uint16_t)g_pm1a_cnt) & 1)) {
            outb((uint16_t)g_smi_cmd, g_acpi_enable);
            while (!(inw((uint16_t)g_pm1a_cnt) & 1));
        }
        // PM1a_CNT <- SLP_TYPa (bits 10-12) | SLP_EN (bit 13)
        outw((uint16_t)g_pm1a_cnt, (uint16_t)(((g_slp_typ_a & 7) << 10) | (1 << 13)));
        if (g_pm1b_cnt)
            outw((uint16_t)g_pm1b_cnt, (uint16_t)(((g_slp_typ_b & 7) << 10) | (1 << 13)));
    }

    // Fallbacks (emulator ports) if ACPI is unavailable or didn't take effect
    outw(0x604,  0x2000);  // QEMU
    outw(0xB004, 0x2000);  // Bochs / older QEMU
    return false;
}

bool shutdown() {
    booting = true;
    for (int i = 0; i < disks->size(); i++) {
        (*disks)[i]->cleanup();
        delete (*disks)[i];
    }
    return power_off();
}

bool reboot() {
	booting = true;
	for (int i = 0; i < disks->size(); i++) {
		(*disks)[i]->cleanup();
		delete (*disks)[i];
	}
    if (g_fadt) {
        FADT* f = (FADT*)g_fadt;
        // flags 비트10 = RESET_REG_SUP(지원여부), length>=129 = reset_value 필드 존재(ACPI 2.0+)
        if ((f->flags & (1u << 10)) && f->header.length >= 129) {
            uint64_t addr = f->reset_reg.address;
            uint8_t  val = f->reset_value;
            switch (f->reset_reg.address_space) {
            case 1: outb((uint16_t)addr, val); break;          // SystemIO
            case 0: // SystemMemory
            {
				uint64_t paddr = addr & ~0xFFFull;
                volatile uint8_t* base = (volatile uint8_t*)(paddr + MMIO_BASE);
                if (paddr > phy_page_allocator->get_total_pages() * PageSize) {
                    base = (volatile uint8_t*)mmio_bump;
                    mmio_bump += PageSize;
                }
				virt_page_allocator->alloc_virt_page((uint64_t)base, paddr,
					VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
				uint64_t offset = addr & 0xFFF;
				base[offset] = val;
                break;
            }
                // case 2 (PCIConfig) 필요하면 나중에
            }
        }
    }

    // ACPI 리셋 안 먹히거나 없을 때 폴백
    outb(0x64, 0xFE);   // 8042 키보드 컨트롤러 리셋
    outb(0xCF9, 0x06);  // PCI 리셋 레지스터 직접
	return false;
}