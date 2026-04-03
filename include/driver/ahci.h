#ifndef __AHCI_H__
#define __AHCI_H__
#include "util/size.h"
#include "driver/disk.h"
#include "arch/ahci_c.h"

// ─────────────────────────────────────────────────────────────
//  FIS / Command 구조체
// ─────────────────────────────────────────────────────────────
struct __attribute__((packed)) FIS_REG_H2D {
    uint8_t  fis_type;      // 0x27
    uint8_t  pmport : 4;
    uint8_t  rsv0 : 3;
    uint8_t  c : 1;    // 1=Command, 0=Control
    uint8_t  command;
    uint8_t  featurel;

    uint8_t  lba0, lba1, lba2;
    uint8_t  device;        // bit6 = LBA mode

    uint8_t  lba3, lba4, lba5;
    uint8_t  featureh;

    uint8_t  countl, counth;
    uint8_t  icc;
    uint8_t  control;

    uint8_t  rsv1[4];
};

struct __attribute__((packed)) HBA_PRDT_ENTRY {
    uint32_t dba;           // Data Base Address (물리)
    uint32_t dbau;          // Data Base Address Upper
    uint32_t rsv0;
    uint32_t dbc : 22;    // Byte count (0-based)
    uint32_t rsv1 : 9;
    uint32_t i : 1;     // Interrupt on Completion
};

struct __attribute__((packed)) HBA_CMD_HEADER {
    uint8_t  cfl : 5;     // Command FIS Length (DWORDs)
    uint8_t  a : 1;     // ATAPI
    uint8_t  w : 1;     // Write (1=H2D)
    uint8_t  p : 1;     // Prefetchable

    uint8_t  r : 1;
    uint8_t  b : 1;
    uint8_t  c : 1;     // Clear Busy on R_OK
    uint8_t  rsv0 : 1;
    uint8_t  pmp : 4;

    uint16_t prdtl;         // PRDT 엔트리 수
    uint32_t prdbc;         // 전송된 바이트 수 (HW가 채움)

    uint32_t ctba;          // Command Table Base (물리)
    uint32_t ctbau;

    uint32_t rsv1[4];
};

struct __attribute__((packed)) HBA_CMD_TBL {
    uint8_t        cfis[64];        // Command FIS
    uint8_t        acmd[16];        // ATAPI Command
    uint8_t        rsv[48];
    HBA_PRDT_ENTRY prdt_entry[1];   // 가변 길이
};

// ─────────────────────────────────────────────────────────────
//  AHCIDisk : Disk 상속, 포트 하나를 전담
// ─────────────────────────────────────────────────────────────
class AHCIDisk : public Disk {
public:
    // AHCIController가 포트를 발견하고 직접 생성
    AHCIDisk(volatile HBA_PORT* port,
        uint16_t bus, uint16_t slot, uint16_t func, uint32_t port_idx);
    ~AHCIDisk() override;

    void init()        override;
    int  read_sector(uint64_t lba, uint32_t count, void* phys_buf)       override;
    int  write_sector(uint64_t lba, uint32_t count, const void* phys_buf) override;

private:
    volatile HBA_PORT* port;

    // DMA 메모리 (4KiB 페이지 1개)
    // 레이아웃: [Command List 1KB][FIS 256B][Command Table 256B+]
    uint64_t dma_phys_base;
    uint64_t dma_virt_base;

    void start_cmd();
    void stop_cmd();

    // read/write 공통 로직 (w=0 읽기, w=1 쓰기)
    int  do_rw(uint64_t lba, uint32_t count, uint64_t phys_buf, int write);
};
#endif /*__AHCI_H__*/