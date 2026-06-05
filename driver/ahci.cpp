#include "driver/ahci.h"
#include "mm/allocator"
#include "util/memory.h"
#include "debug/log.h"

// ═══════════════════════════════════════════════════════════════
//  생성자 / 소멸자
// ═══════════════════════════════════════════════════════════════
AHCIDisk::AHCIDisk(volatile HBA_PORT* port,
    uint16_t bus, uint16_t slot, uint16_t func, uint32_t port_idx)
    : Disk(bus, slot, func, port_idx),
    port(port), dma_phys_base(0), dma_virt_base(0)
{
    type = 1;  // Disk::type: AHCI/SATA
}

AHCIDisk::~AHCIDisk() {
    cleanup();
    stop_cmd();
    if (dma_phys_base) {
        virt_page_allocator->free_virt_page(dma_virt_base);
        phy_page_allocator->put_page(dma_phys_base);
    }
}

// ═══════════════════════════════════════════════════════════════
//  엔진 시작 / 정지
// ═══════════════════════════════════════════════════════════════
void AHCIDisk::start_cmd() {
    if (port->cmd & (1u << 0)) return;          // 이미 실행 중
    while (port->cmd & (1u << 15));             // CR=0 대기
    port->cmd |= (1u << 4);                     // FRE=1
    port->cmd |= (1u << 0);                     // ST=1
}

void AHCIDisk::stop_cmd() {
    port->cmd &= ~(1u << 0);                    // ST=0
    while (port->cmd & (1u << 15));             // CR=0 대기
    port->cmd &= ~(1u << 4);                    // FRE=0
    while (port->cmd & (1u << 14));             // FR=0 대기
}

// ═══════════════════════════════════════════════════════════════
//  init
// ═══════════════════════════════════════════════════════════════
void AHCIDisk::init() {
    stop_cmd();

    // DMA 페이지 할당 (4KiB)
    // 레이아웃:
    //   [0x000 ~ 0x3FF] Command List  (1KB, 32개 * 32B)
    //   [0x400 ~ 0x4FF] FIS Buffer    (256B)
    //   [0x500 ~      ] Command Table (Command FIS 64B + PRDT 1개)
    dma_phys_base = phy_page_allocator->alloc_phy_page();
    dma_virt_base = dma_phys_base + MMIO_BASE;
    virt_page_allocator->alloc_virt_page(dma_virt_base, dma_phys_base,
        VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    memset((void*)dma_virt_base, 0, 4096);

    uint64_t cl_phys = dma_phys_base;
    uint64_t fb_phys = dma_phys_base + 0x400;
    uint64_t ct_phys = dma_phys_base + 0x500;

    // 포트에 Command List / FIS 버퍼 등록
    port->clb = (uint32_t)(cl_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(cl_phys >> 32);
    port->fb = (uint32_t)(fb_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(fb_phys >> 32);

    // Command Header 0번: Command Table 주소 고정
    HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)(dma_virt_base);
    hdr[0].ctba = (uint32_t)(ct_phys & 0xFFFFFFFF);
    hdr[0].ctbau = (uint32_t)(ct_phys >> 32);
    hdr[0].prdtl = 1;

    start_cmd();

    uart_print("[AHCI] Disk init on port ");
    uart_print_hex(port_or_ns);
    uart_print("\n");
    uart_print("[AHCI] port is on 0x");
    uart_print_hex((uint64_t)port);
    uart_print("\n");

    switch (port->sig) {
    case SATA_SIG_SATA:
    {
        uart_print("[AHCI] SATA Disk\n");
        break;
    }
    case SATA_SIG_ATAPI:
    case SATA_SIG_SEMB:
    case SATA_SIG_PM:
    default:
    {
        delete this;
        return;
    }
    }
}
// ═══════════════════════════════════════════════════════════════
//  read/write 공통 내부 구현
// ═══════════════════════════════════════════════════════════════
int AHCIDisk::do_rw(uint64_t lba, uint32_t count, uint64_t phys_buf, int write) {

    uart_print("[AHCI] do_rw port: ");
    uart_print_hex((uint64_t)port);
    uart_print("\n");
    if (!port) return -1;

    port->is = (uint32_t)-1;    // 인터럽트 상태 클리어

    HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)(dma_virt_base);
    HBA_CMD_TBL* tbl = (HBA_CMD_TBL*)(dma_virt_base + 0x500);

    hdr[0].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);   // 5 DWORDs
    hdr[0].w = write ? 1 : 0;
    hdr[0].c = 1;
    hdr[0].prdtl = 1;

    memset(tbl, 0, sizeof(HBA_CMD_TBL));
    tbl->prdt_entry[0].dba = (uint32_t)(phys_buf & 0xFFFFFFFF);
    tbl->prdt_entry[0].dbau = (uint32_t)(phys_buf >> 32);
    tbl->prdt_entry[0].dbc = (count * 512) - 1;   // 0-based
    tbl->prdt_entry[0].i = 1;

    FIS_REG_H2D* cfis = (FIS_REG_H2D*)tbl->cfis;
    memset(cfis, 0, sizeof(FIS_REG_H2D));
    cfis->fis_type = 0x27;
    cfis->c = 1;
    cfis->command = write ? 0x35 : 0x25;  // WRITE DMA EXT / READ DMA EXT
    cfis->device = 1u << 6;              // LBA mode

    cfis->lba0 = (uint8_t)(lba);
    cfis->lba1 = (uint8_t)(lba >> 8);
    cfis->lba2 = (uint8_t)(lba >> 16);
    cfis->lba3 = (uint8_t)(lba >> 24);
    cfis->lba4 = (uint8_t)(lba >> 32);
    cfis->lba5 = (uint8_t)(lba >> 40);

    cfis->countl = (uint8_t)(count & 0xFF);
    cfis->counth = (uint8_t)(count >> 8);

    // BSY / DRQ 대기
	while ((port->tfd & (0x80 | 0x08))) __asm__ __volatile__("pause");

    port->ci = 1u;      // Command Slot 0 발행

    // 완료 대기
    while (true) {
        if ((port->ci & 1u) == 0) break;
        if (port->is & (1u << 30)) return -3;  // TFES (Task File Error)
    }

    return 0;
}

// ═══════════════════════════════════════════════════════════════
//  Disk 인터페이스 구현
// ═══════════════════════════════════════════════════════════════
int AHCIDisk::read_sector(uint64_t lba, uint32_t count, void* phys_buf) {
    return do_rw(lba, count, (uint64_t)phys_buf, 0);
}

int AHCIDisk::write_sector(uint64_t lba, uint32_t count, const void* phys_buf) {
    return do_rw(lba, count, (uint64_t)phys_buf, 1);
}