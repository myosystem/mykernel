#ifndef __AHCI_C_H__
#define __AHCI_C_H__

#include "arch/controller.h"
#include "arch/pci.h"

// ─────────────────────────────────────────────────────────────
//  AHCI HBA 레지스터 구조체
// ─────────────────────────────────────────────────────────────
#define HBA_PORT_DET_MASK   0x0F
#define HBA_PORT_IPM_MASK   0xF00
#define HBA_PORT_DET_PRESENT 3

#define SATA_SIG_SATA   0x00000101
#define SATA_SIG_ATAPI  0xEB140101
#define SATA_SIG_SEMB   0xC33C0101
#define SATA_SIG_PM     0x96690101

struct __attribute__((packed)) HBA_PORT {
    uint32_t clb;       // Command List Base Address
    uint32_t clbu;
    uint32_t fb;        // FIS Base Address
    uint32_t fbu;
    uint32_t is;        // Interrupt Status
    uint32_t ie;        // Interrupt Enable
    uint32_t cmd;       // Command and Status
    uint32_t rsv0;
    uint32_t tfd;       // Task File Data
    uint32_t sig;       // Signature
    uint32_t ssts;      // SATA Status
    uint32_t sctl;      // SATA Control
    uint32_t serr;      // SATA Error
    uint32_t sact;      // SATA Active
    uint32_t ci;        // Command Issue
    uint32_t sntf;
    uint32_t fbs;
    uint32_t rsv1[11];
    uint32_t vendor[4];
};

struct __attribute__((packed)) HBA_MEM {
    uint32_t cap;       // Host Capability
    uint32_t ghc;       // Global Host Control
    uint32_t is;        // Interrupt Status
    uint32_t pi;        // Ports Implemented (비트마스크)
    uint32_t vs;        // Version
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t  rsv[0xA0 - 0x2C];
    uint8_t  vendor[0x100 - 0xA0];
    HBA_PORT ports[32];
};

// ─────────────────────────────────────────────────────────────
//  AHCIController
// ─────────────────────────────────────────────────────────────
class AHCIDisk;  // 전방 선언

class AHCIController : public Controller {
public:
    AHCIController(uint16_t bus, uint16_t slot, uint16_t func);
    ~AHCIController();

    void init();

    // AHCI 스펙: Identify Device (디버그/초기화용)
    int identify(volatile HBA_PORT* port, void* buf);

    uint64_t get_type() override { return 1; }
private:
    HBA_MEM* abar;         // BAR5 가상 주소
    uint64_t  bar_size;
    void init_port(volatile HBA_PORT* port);
    // 포트 타입 판별 (0=없음, 1=SATA, 2=ATAPI, 3=SEMB, 4=PM)
    int  check_type(volatile HBA_PORT* port);

    // 포트 전체 순회하며 SATA 디스크 찾아 AHCIDisk 생성
    void scan_ports();
};

#endif  // __AHCI_C_H__