#include "kernel/kernel.h"
#include "driver/ahci.h"
#include "debug/log.h"
#include "util/memory.h"
#include "mm/allocator"
#include "arch/pci.h"

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
void AHCIDisk::start_cmd() {
    // 이미 실행 중이면 리턴
    if (port->cmd & (1 << 0)) return;

    while (port->cmd & (1 << 15)); // CR(Command List Running)이 꺼질 때까지 대기

    port->cmd |= (1 << 4);  // FRE=1 (FIS Receive Enable)
    port->cmd |= (1 << 0);  // ST=1  (Start)
}

void AHCIDisk::stop_cmd() {
    port->cmd &= ~(1 << 0); // ST=0
    while (port->cmd & (1 << 15)); // CR=0 대기

    port->cmd &= ~(1 << 4); // FRE=0
    while (port->cmd & (1 << 14)); // FR=0 대기
}
void AHCIDisk::init() {
    // 1. 부모 클래스(Disk) 로직에 따라 BAR5 주소를 가져옴
    uint32_t bar5 = pci_read32(pci_bus, pci_slot, pci_func, 0x24);
    HBA_MEM* abar = (HBA_MEM*)((uint64_t)(bar5 & 0xFFFFFFF0) + MMIO_BASE);
    virt_page_allocator->alloc_virt_page((uint64_t)abar, (uint64_t)abar - MMIO_BASE,
		VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);

    // 2. 사용 가능한 포트 찾기 (작성하신 probe_ports 로직 통합)
    uint32_t pi = abar->pi;
    int port_idx = -1;

    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            uint32_t ssts = abar->ports[i].ssts;
            uint8_t det = ssts & 0x0F;
            uint8_t ipm = (ssts >> 8) & 0x0F;

            // 장치가 있고 활성화 상태이며, SATA 디스크(SIG)인 경우
            if (det == 3 && ipm == 1 && abar->ports[i].sig == SATA_SIG_SATA) {
                port_idx = i;
                break; // 첫 번째 디스크만 사용 (멀티 디스크 지원 시 수정 필요)
            }
        }
    }

    if (port_idx == -1) return; // SATA 디스크를 못 찾음
    this->port = &abar->ports[port_idx];

    // 3. 엔진 정지 (초기화를 위해)
    stop_cmd();

    // 4. DMA 메모리 할당 (딱 1번만!)
    // 구조: [Command List 1KB] + [FIS 256B] + [Command Table 256B * 1]
    // 총 4KB 페이지 하나면 충분합니다.
    dma_phys_base = phy_page_allocator->alloc_phy_page();
    dma_virt_base = dma_phys_base + MMIO_BASE; // 커널 매핑 주소라고 가정

    // 페이지 매핑 (PCD=1: Cache Disable 필수!)
    virt_page_allocator->alloc_virt_page(dma_virt_base, dma_phys_base,
        VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);

    memset((void*)dma_virt_base, 0, 4096);

    // 5. 주소 계산
    uint64_t cl_phys = dma_phys_base;           // 0 ~ 1024
    uint64_t fb_phys = dma_phys_base + 1024;    // 1024 ~ 1280
    uint64_t ct_phys = dma_phys_base + 1280;    // 1280 ~ (Command Table)

    // 6. 포트 레지스터 등록
    port->clb = (uint32_t)(cl_phys & 0xFFFFFFFF);
    port->clbu = (uint32_t)(cl_phys >> 32);
    port->fb = (uint32_t)(fb_phys & 0xFFFFFFFF);
    port->fbu = (uint32_t)(fb_phys >> 32);

    // 7. Command Header 미리 세팅 (Slot 0만 사용)
    HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)(dma_virt_base);
    hdr[0].ctba = (uint32_t)(ct_phys & 0xFFFFFFFF);
    hdr[0].ctbau = (uint32_t)(ct_phys >> 32);
    hdr[0].prdtl = 1; // 기본적으로 1개의 PRDT 사용 (필요시 늘림)

    // 8. 엔진 시작! (이제 끄지 않음)
    start_cmd();
}
int AHCIDisk::read_sector(uint64_t lba, uint32_t count, void* phys_buf) {
    if (!port) return -1;

    // 포트 에러 클리어 (필요 시)
    port->is = (uint32_t)-1;

    // 1. 메모리 주소 재계산 (init에서 할당한 영역 사용)
    // dma_virt_base는 Command List의 시작점
    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)(dma_virt_base);

    // Command Table 위치 (Header 0번이 가리키는 곳)
    // 구조상 dma_virt_base + 1280 위치임
    HBA_CMD_TBL* cmdtbl = (HBA_CMD_TBL*)(dma_virt_base + 1280);

    // 2. Command Header 설정 업데이트
    cmdheader[0].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // 5
    cmdheader[0].w = 0; // Read
    cmdheader[0].c = 1; // Clear Busy on OK
    cmdheader[0].prdtl = 1; // PRDT 1개만 쓴다고 가정

    // 3. Command Table 설정 (PRDT)
    memset(cmdtbl, 0, sizeof(HBA_CMD_TBL));

    cmdtbl->prdt_entry[0].dba = (uint32_t)((uint64_t)phys_buf & 0xFFFFFFFF);
    cmdtbl->prdt_entry[0].dbau = (uint32_t)((uint64_t)phys_buf >> 32);
    cmdtbl->prdt_entry[0].dbc = ((count * 512) - 1); // 4MB 이하 전송 시 1개로 충분
    cmdtbl->prdt_entry[0].i = 1; // 인터럽트

    // 4. FIS 설정 (Command Table 안에 있음)
    FIS_REG_H2D* cmdfis = (FIS_REG_H2D*)(&cmdtbl->cfis);

    cmdfis->fis_type = 0x27; // H2D
    cmdfis->c = 1;           // Command
    cmdfis->command = 0x25;  // READ DMA EXT (48bit)
    cmdfis->device = 1 << 6; // LBA Mode

    cmdfis->lba0 = (uint8_t)lba;
    cmdfis->lba1 = (uint8_t)(lba >> 8);
    cmdfis->lba2 = (uint8_t)(lba >> 16);
    cmdfis->lba3 = (uint8_t)(lba >> 24);
    cmdfis->lba4 = (uint8_t)(lba >> 32);
    cmdfis->lba5 = (uint8_t)(lba >> 40);

    cmdfis->countl = (uint8_t)(count & 0xFF);
    cmdfis->counth = (uint8_t)(count >> 8);

    // 5. 명령 발행 (Slot 0 비트 설정)
    // 엔진을 껐다 킬 필요 없이 CI 비트만 올리면 하드웨어가 낚아채감
    // BSY 확인
    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) { spin++; }
    if (spin == 1000000) return -2; // 타임아웃

    port->ci = 1; // Slot 0 Issue

    // 6. 완료 대기 (Polling)
    while (true) {
        if ((port->ci & 1) == 0) break; // 비트가 0이 되면 완료
        if (port->is & (1 << 30)) return -3; // 에러 발생 (TFES)
    }

    return 0; // 성공
}
AHCIDisk::~AHCIDisk() {
    stop_cmd();
    // 할당한 DMA 페이지 해제
    if (dma_phys_base) {
        virt_page_allocator->free_virt_page(dma_virt_base);
        phy_page_allocator->free_phy_page(dma_phys_base);
    }
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
