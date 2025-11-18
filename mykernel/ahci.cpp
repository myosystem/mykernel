#include "kernel.h"
#include "ahci.h"
#include "log.h"
#include "memory.h"
#include "allocator"
// SATA device types (sig 레지스터로 판별)
#define SATA_SIG_ATAPI 0xEB140101
#define SATA_SIG_SEMB  0xC33C0101
#define SATA_SIG_PM    0x96690101
#define SATA_SIG_SATA  0x00000101

// SSTS 상태 비트
#define HBA_PORT_DET_MASK 0x0F
#define HBA_PORT_DET_PRESENT 0x03
#define HBA_PORT_IPM_MASK (0x0F << 8)
#define HBA_PORT_IPM_ACTIVE (0x01 << 8)

// 디바이스 타입 판별 함수
static inline int check_type(volatile HBA_PORT* port) {
    uint32_t ssts = port->ssts;

    uint8_t det = ssts & HBA_PORT_DET_MASK;       // Device detection
    uint8_t ipm = (ssts & HBA_PORT_IPM_MASK) >> 8; // Interface power management

    if (det != HBA_PORT_DET_PRESENT) return 0; // 장치 없음
    if (ipm != 1) return 0;                     // 링크 활성 아님

    switch (port->sig) {
    case SATA_SIG_ATAPI: return 2; // ATAPI (CD/DVD)
    case SATA_SIG_SEMB:  return 3; // SEMB
    case SATA_SIG_PM:    return 4; // Port multiplier
    case SATA_SIG_SATA:  return 1; // SATA 디스크
    default:             return 0; // 알 수 없음
    }
}

// AHCI 컨트롤러에서 살아있는 포트 찾기
void probe_ports(HBA_MEM* abar) {
    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) { // 포트 구현됨
            int type = check_type(&abar->ports[i]);
            if (type == 0) continue; // 디바이스 없음
            // 여기서 type==1이면 SATA 디스크
            uart_print("port : ");
			uart_print(i);
            if (type == 1) uart_print("SATA disk\n");
            else if (type == 2) uart_print("ATAPI\n");
            else if (type == 3) uart_print("SEMB\n");
            else if (type == 4) uart_print("Port Multiplier\n");
        }
    }
}
struct FIS_REG_H2D {
    uint8_t  fis_type;  // 0x27
    uint8_t  pmport : 4;  // Port multiplier
    uint8_t  rsv0 : 3;    // Reserved
    uint8_t  c : 1;       // 1=command, 0=control
    uint8_t  command;   // ATA command
    uint8_t  featurel;  // Feature (low byte)

    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;    // [6]=LBA mode

    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  featureh;  // Feature (high byte)

    uint8_t  countl;    // Sector count (low)
    uint8_t  counth;    // Sector count (high)
    uint8_t  icc;       // Isochronous command completion
    uint8_t  control;   // Control

    uint8_t  rsv1[4];   // Reserved
} __attribute__((packed));

struct HBA_PRDT_ENTRY {
    uint32_t dba;       // Data base address
    uint32_t dbau;      // Data base address upper
    uint32_t rsv0;      // Reserved
    uint32_t dbc : 22;    // Byte count (0-based: 0=1 byte, 511=512 bytes)
    uint32_t rsv1 : 9;
    uint32_t i : 1;       // Interrupt on completion
} __attribute__((packed));

struct HBA_CMD_HEADER {
    uint8_t  cfl : 5;     // Command FIS length in DWORDS (2~16)
    uint8_t  a : 1;       // ATAPI
    uint8_t  w : 1;       // Write (1=H2D write, 0=read)
    uint8_t  p : 1;       // Prefetchable

    uint8_t  r : 1;       // Reset
    uint8_t  b : 1;       // BIST
    uint8_t  c : 1;       // Clear busy upon R_OK
    uint8_t  rsv0 : 1;    // Reserved
    uint8_t  pmp : 4;     // Port multiplier port

    uint16_t prdtl;     // Physical region descriptor table length

    uint32_t prdbc;     // Physical region descriptor byte count

    uint32_t ctba;      // Command table descriptor base address
    uint32_t ctbau;     // Command table descriptor base address upper

    uint32_t rsv1[4];   // Reserved
} __attribute__((packed));
struct HBA_CMD_TBL {
    uint8_t cfis[64];       // Command FIS (H2D FIS)
    uint8_t acmd[16];       // ATAPI command (안 쓰면 0)
    uint8_t rsv[48];        // Reserved
    HBA_PRDT_ENTRY prdt_entry[1]; // 실제 엔트리는 prdtl 개수만큼
} __attribute__((packed));

int ahci_read(volatile HBA_PORT* port, uint64_t lba, uint32_t count, void* mmio_based_buf) {
    // ====== 1. 포트 정지 ======
    port->cmd &= ~0x1;                  // ST=0
    while (port->cmd & (1 << 15));      // CR=1 → 클리어될 때까지 대기

    port->cmd &= ~(1 << 4);             // FRE=0
    while (port->cmd & (1 << 14));      // FR=1 → 클리어될 때까지 대기

    // ====== 2. Command List/FIS/CT 영역 확보 ======
    uint64_t mem = phy_page_allocator->alloc_phy_page();
    virt_page_allocator->alloc_virt_page(
        mem + MMIO_BASE, mem, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD
    );

    uint64_t cl_phys = mem;         // Command List Base (1KB aligned)
    uint64_t fb_phys = mem + 0x400; // FIS Base (256B aligned)
    uint64_t ct_phys = mem + 0x800; // Command Table Base (128B aligned)

    memset((void*)(cl_phys + MMIO_BASE), 0, 1024);  // CLB 전체 클리어
    memset((void*)(fb_phys + MMIO_BASE), 0, 256);   // FB 전체 클리어
    memset((void*)(ct_phys + MMIO_BASE), 0, 256);   // CT 초기화

    // ====== 3. 포트 레지스터에 주소 등록 ======
    port->clb = (uint32_t)(cl_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(cl_phys >> 32);
    port->fb = (uint32_t)(fb_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(fb_phys >> 32);

    // ====== 4. Command Header 세팅 ======
    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)(cl_phys + MMIO_BASE);
    cmdheader[0].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // 5 DWORDs
    cmdheader[0].w = 0;       // 읽기
    cmdheader[0].prdtl = 1;       // PRDT 엔트리 하나
    cmdheader[0].ctba = (uint32_t)(ct_phys & 0xFFFFFFFF);
    cmdheader[0].ctbau = (uint32_t)(ct_phys >> 32);

    // ====== 5. Command Table/PRDT ======
    HBA_CMD_TBL* cmdtbl = (HBA_CMD_TBL*)(ct_phys + MMIO_BASE);
    uint64_t buf_phys = (uint64_t)mmio_based_buf - MMIO_BASE;

    cmdtbl->prdt_entry[0].dba = (uint32_t)(buf_phys & 0xFFFFFFFF);
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
    cmdtbl->prdt_entry[0].dbc = ((count * 512u) - 1u) & 0x3FFFFF; // 22비트 마스킹
    cmdtbl->prdt_entry[0].i = 1;                 // 완료 시 인터럽트

    // ====== 6. CFIS 작성 (READ DMA EXT) ======
    FIS_REG_H2D* cfis = (FIS_REG_H2D*)(&cmdtbl->cfis);
    memset(cfis, 0, sizeof(FIS_REG_H2D));

    cfis->fis_type = 0x27;   // Host to Device FIS
    cfis->c = 1;      // Command
    cfis->command = 0x25;   // READ DMA EXT (48-bit)

    cfis->device = 0xE0;   // LBA 모드 (VMware 호환 안전값)

    // 48-bit LBA
    cfis->lba0 = (uint8_t)(lba);
    cfis->lba1 = (uint8_t)(lba >> 8);
    cfis->lba2 = (uint8_t)(lba >> 16);
    cfis->lba3 = (uint8_t)(lba >> 24);
    cfis->lba4 = (uint8_t)(lba >> 32);
    cfis->lba5 = (uint8_t)(lba >> 40);

    // 섹터 수
    cfis->countl = (uint8_t)(count & 0xFF);
    cfis->counth = (uint8_t)(count >> 8);

    // ====== 7. 포트 다시 시작 ======
    port->cmd |= (1 << 4);   // FRE=1
    port->cmd |= (1 << 0);   // ST=1

    // ====== 8. 명령 실행 ======
    port->ci = 1 << 0;       // slot 0 실행

    // ====== 9. 완료 대기 ======
    while (port->ci & 1);                     // CI 클리어 기다림
    while (port->tfd & (0x80 | 0x08));        // BSY/DRQ 클리어 기다림
    /*
    uart_print("\nis=");
    uart_print(port->is);
    uart_print(", tfd=");
    uart_print(port->tfd);
    uart_print(", serr=");
    uart_print_hex(port->serr);
    uart_print("\n");
    */

    return 0;
}
int ahci_identify(volatile HBA_PORT* port, void* mmio_based_buf) {
    // ====== 1. 포트 정지 ======
    port->cmd &= ~0x1;                  // ST=0
    while (port->cmd & (1 << 15));      // CR=1 → 클리어될 때까지 대기

    port->cmd &= ~(1 << 4);             // FRE=0
    while (port->cmd & (1 << 14));      // FR=1 → 클리어될 때까지 대기

    // ====== 2. Command List/FIS/CT 메모리 할당 ======
    uint64_t mem = phy_page_allocator->alloc_phy_page();
    virt_page_allocator->alloc_virt_page(mem + MMIO_BASE, mem,
        VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);

    uint64_t cl_phys = mem;
    uint64_t fb_phys = mem + 0x400;
    uint64_t ct_phys = mem + 0x800;

    memset((void*)(cl_phys + MMIO_BASE), 0, 1024);
    memset((void*)(fb_phys + MMIO_BASE), 0, 256);
    memset((void*)(ct_phys + MMIO_BASE), 0, 256);

    port->clb = (uint32_t)(cl_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(cl_phys >> 32);
    port->fb = (uint32_t)(fb_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(fb_phys >> 32);

    // ====== 3. Command Header ======
    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)(cl_phys + MMIO_BASE);
    cmdheader[0].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // 5 DWORDs
    cmdheader[0].w = 0;      // 읽기
    cmdheader[0].prdtl = 1;      // PRDT 하나
    cmdheader[0].ctba = (uint32_t)(ct_phys & 0xFFFFFFFF);
    cmdheader[0].ctbau = (uint32_t)(ct_phys >> 32);

    // ====== 4. PRDT 설정 ======
    HBA_CMD_TBL* cmdtbl = (HBA_CMD_TBL*)(ct_phys + MMIO_BASE);
    uint64_t buf_phys = (uint64_t)mmio_based_buf - MMIO_BASE;

    cmdtbl->prdt_entry[0].dba = (uint32_t)(buf_phys & 0xFFFFFFFF);
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
    cmdtbl->prdt_entry[0].dbc = 512 - 1; // IDENTIFY는 512바이트 전송
    cmdtbl->prdt_entry[0].i = 1;

    // ====== 5. CFIS 작성 ======
    FIS_REG_H2D* cfis = (FIS_REG_H2D*)(&cmdtbl->cfis);
    memset(cfis, 0, sizeof(FIS_REG_H2D));

    cfis->fis_type = 0x27;
    cfis->c = 1;
    cfis->command = 0xEC;   // IDENTIFY DEVICE
    cfis->device = 0xE0;   // VMware 호환 안전값

    // ====== 6. 포트 다시 시작 ======
    port->cmd |= (1 << 4);   // FRE=1
    port->cmd |= (1 << 0);   // ST=1

    // ====== 7. 명령 실행 ======
    port->ci = 1 << 0;

    // ====== 8. 완료 대기 ======
    while (port->ci & 1);
    while (port->tfd & (0x80 | 0x08));

    uart_print("IDENTIFY: is=");
    uart_print(port->is);
    uart_print(", tfd=");
    uart_print(port->tfd);
    uart_print(", serr=");
    uart_print_hex(port->serr);

    return 0;
}
