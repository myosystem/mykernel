#include "arch/ahci_c.h"
#include "driver/ahci.h"
#include "mm/allocator"
#include "util/memory.h"
#include "debug/log.h"

// ═══════════════════════════════════════════════════════════════
//  생성자 / 소멸자
// ═══════════════════════════════════════════════════════════════
AHCIController::AHCIController(uint16_t bus, uint16_t slot, uint16_t func)
    : Controller(bus,slot,func), abar(nullptr), bar_size(0) {
}

AHCIController::~AHCIController() {
    if (!abar) return;
    for (uint64_t off = 0; off < bar_size; off += 4096)
        virt_page_allocator->free_virt_page((uint64_t)abar + off);
}

// ═══════════════════════════════════════════════════════════════
//  포트 타입 판별
// ═══════════════════════════════════════════════════════════════
int AHCIController::check_type(volatile HBA_PORT* port) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & HBA_PORT_DET_MASK;
    uint8_t ipm = (ssts & HBA_PORT_IPM_MASK) >> 8;

    if (det != HBA_PORT_DET_PRESENT) return 0;
    if (ipm != 1) return 0;
    return 1;
}
void AHCIController::init_port(volatile HBA_PORT* port) {
    // 끄기만 함, 켜는건 AHCIDisk::init()에서
    port->cmd &= ~(1u << 0);   // ST  = 0
    port->cmd &= ~(1u << 4);   // FRE = 0

    while (port->cmd & (1u << 15));  // CR=0 대기
    while (port->cmd & (1u << 14));  // FR=0 대기

    // CLB, FB 초기화
    port->clb = 0;
    port->clbu = 0;
    port->fb = 0;
    port->fbu = 0;
}
// ═══════════════════════════════════════════════════════════════
//  포트 스캔 → AHCIDisk 생성
// ═══════════════════════════════════════════════════════════════
void AHCIController::scan_ports() {
    uint32_t pi = abar->pi;

    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) continue;   // 구현되지 않은 포트

        init_port(&abar->ports[i]);
        if (!check_type(&abar->ports[i])) continue;           // 디바이스 없음

        // SATA 디스크만 AHCIDisk로 생성
        // (ATAPI 등 지원 확장 시 여기서 분기)
        AHCIDisk* disk = new AHCIDisk(&abar->ports[i],
            pci_bus, pci_slot, pci_func,
            (uint32_t)i);
        disk->init();

        if (on_disk_found) on_disk_found(disk);
    }
}

// ═══════════════════════════════════════════════════════════════
//  init
// ═══════════════════════════════════════════════════════════════
void AHCIController::init() {
    // BAR5 매핑 (AHCI는 BAR5 = 0x24)
    pci_bar_info_t bar = pci_get_bar_size(pci_bus, pci_slot, pci_func, 0x24);
    bar_size = bar.size;
    abar = (HBA_MEM*)(bar.addr + MMIO_BASE);

    for (uint64_t off = 0; off < bar_size; off += 4096)
        virt_page_allocator->alloc_virt_page(
            (uint64_t)abar + off, bar.addr + off,
            VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);

    // AHCI Enable (GHC.AE = bit 31)
    abar->ghc |= (1u << 0);   // HR 먼저 (리셋)
    while (abar->ghc & (1u << 0)) {}  // 완료 대기
    abar->ghc |= (1u << 31);  // 그 다음 AE

    // 리셋 후엔 AE가 꺼질 수 있으니 다시 켜줌
    abar->ghc |= (1u << 31);
    scan_ports();
}

// ═══════════════════════════════════════════════════════════════
//  Identify (디버그용, 기존 코드 그대로 유지)
// ═══════════════════════════════════════════════════════════════
int AHCIController::identify(volatile HBA_PORT* port, void* buf) {
    port->cmd &= ~0x1;
    while (port->cmd & (1 << 15));
    port->cmd &= ~(1 << 4);
    while (port->cmd & (1 << 14));

    uint64_t mem = phy_page_allocator->alloc_phy_page();
    uint64_t mem_virt = mem + MMIO_BASE;
    virt_page_allocator->alloc_virt_page(mem_virt, mem,
        VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    memset((void*)mem_virt, 0, 4096);

    uint64_t cl_phys = mem;
    uint64_t fb_phys = mem + 0x400;
    uint64_t ct_phys = mem + 0x800;

    port->clb = (uint32_t)(cl_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(cl_phys >> 32);
    port->fb = (uint32_t)(fb_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(fb_phys >> 32);

    HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)(cl_phys + MMIO_BASE);
    hdr[0].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    hdr[0].w = 0;
    hdr[0].prdtl = 1;
    hdr[0].ctba = (uint32_t)(ct_phys & 0xFFFFFFFF);
    hdr[0].ctbau = (uint32_t)(ct_phys >> 32);

    HBA_CMD_TBL* tbl = (HBA_CMD_TBL*)(ct_phys + MMIO_BASE);
    uint64_t buf_phys = (uint64_t)buf - MMIO_BASE;
    tbl->prdt_entry[0].dba = (uint32_t)(buf_phys & 0xFFFFFFFF);
    tbl->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
    tbl->prdt_entry[0].dbc = 512 - 1;
    tbl->prdt_entry[0].i = 1;

    FIS_REG_H2D* cfis = (FIS_REG_H2D*)tbl->cfis;
    memset(cfis, 0, sizeof(FIS_REG_H2D));
    cfis->fis_type = 0x27;
    cfis->c = 1;
    cfis->command = 0xEC;  // IDENTIFY DEVICE
    cfis->device = 0xE0;

    port->cmd |= (1 << 4);
    port->cmd |= (1 << 0);
    port->ci = 1;

    while (port->ci & 1);
    while (port->tfd & (0x80 | 0x08));

    virt_page_allocator->free_virt_page(mem_virt);
    phy_page_allocator->put_page(mem);

    return 0;
}