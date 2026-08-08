#ifndef __E1000_H__
#define __E1000_H__
#include "util/size.h"
#include "net/ethernet.h"
#define E1000_CTRL     0x0000   // Device Control
#define E1000_STATUS   0x0008   // Device Status
#define E1000_EECD     0x0010   // EEPROM Control
#define E1000_EERD     0x0014   // EEPROM Read
#define E1000_CTRL_EXT 0x0018
#define E1000_ICR      0x00C0   // Interrupt Cause Read (읽으면 클리어)
#define E1000_ITR      0x00C4   // Interrupt Throttling
#define E1000_ICS      0x00C8   // Interrupt Cause Set
#define E1000_IMS      0x00D0   // Interrupt Mask Set/Read
#define E1000_IMC      0x00D8   // Interrupt Mask Clear
#define E1000_IVAR     0x00E4
#define E1000_RCTL     0x0100   // Receive Control
#define E1000_TCTL     0x0400   // Transmit Control
#define E1000_TIPG     0x0410   // Transmit IPG
#define E1000_RDBAL    0x2800   // RX Desc Base Low
#define E1000_RDBAH    0x2804
#define E1000_RDLEN    0x2808   // RX Desc Length (bytes)
#define E1000_RDH      0x2810   // RX Desc Head (HW)
#define E1000_RDT      0x2818   // RX Desc Tail (SW)
#define E1000_RDTR     0x2820   // RX Delay Timer
#define E1000_TDBAL    0x3800   // TX Desc Base Low
#define E1000_TDBAH    0x3804
#define E1000_TDLEN    0x3808
#define E1000_TDH      0x3810
#define E1000_TDT      0x3818
#define E1000_MTA      0x5200   // Multicast Table (128 dword: 0x5200~0x53FC)
#define E1000_RAL0     0x5400   // Receive Address Low 0
#define E1000_RAH0     0x5404   // Receive Address High 0

#define E1000_EERD_START      0x00000001
#define E1000_EERD_DATA_SHIFT 16

#define CTRL_FD    (1<<0)       // Full Duplex
#define CTRL_LRESET  (1<<3)       // Link Reset
#define CTRL_ASDE  (1<<5)       // Auto-Speed Detect Enable
#define CTRL_SLU   (1<<6)       // Set Link Up
#define CTRL_ILOS  (1<<7)
#define CTRL_RESET   (1<<26)      // Device Reset
#define CTRL_PHYRESET (1<<31)

#define STATUS_LU  (1<<1)       // Link Up

#define INT_TXDW   0x01         // TX desc written back
#define INT_TXQE   0x02
#define INT_LSC    0x04         // Link Status Change
#define INT_RXSEQ  0x08
#define INT_RXDMT0 0x10         // RX desc min threshold
#define INT_RXO    0x40         // RX overrun
#define INT_RXT0   0x80         // RX timer

#define RCTL_EN    (1<<1)       // Enable
#define RCTL_SBP   (1<<2)
#define RCTL_UPE   (1<<3)       // Unicast Promiscuous
#define RCTL_MPE   (1<<4)       // Multicast Promiscuous
#define RCTL_LPE   (1<<5)       // Long Packet Enable
#define RCTL_BAM   (1<<15)      // Broadcast Accept
#define RCTL_SECRC (1<<26)      // Strip Ethernet CRC

#define TCTL_EN    (1<<1)       // Enable
#define TCTL_PSP   (1<<3)       // Pad Short Packets
#define TCTL_CT    (0x0F<<4)    // Collision Threshold = 15
#define TCTL_COLD  (0x40<<12)   // Collision Distance = 0x40 (full duplex)

#define TIPG_DEFAULT 0x0060200A // IPGT=10, IPGR1=8, IPGR2=6

#define RAH_AV     (1u<<31)     // Address Valid

#define TXD_EOP    0x01         // End Of Packet
#define TXD_IFCS   0x02         // Insert FCS(CRC)
#define TXD_RS     0x08         // Report Status

#define STAT_DD    0x01         // Descriptor Done
#define STAT_EOP   0x02         // (RX) End Of Packet

#define E1000_IVAR_INT_ALLOC_VALID  0x8
#define E1000_IVAR_RXQ0_SHIFT       0
#define E1000_IVAR_TXQ0_SHIFT       8
#define E1000_IVAR_OTHER_SHIFT      16
struct tx_desc {
    uint64_t addr;      // 버퍼 물리주소
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;    // DD 등
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));
struct rx_desc {
    uint64_t addr;      // 패킷 버퍼 물리주소
    uint16_t length;    // NIC이 받은 길이
    uint16_t checksum;
    uint8_t  status;    // bit0=DD(완료), bit1=EOP
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));
class TXRing {
private:
    volatile tx_desc* ring;
    uint64_t phys_start;
    uint32_t size;       // descriptor 개수
    uint32_t tail;        // SW가 다음에 쓸 인덱스
    volatile uint32_t* tdt;
public:
    TXRing(volatile uint8_t* mmio_base) {
        phys_start = phy_page_allocator->alloc_phy_page();
        ring = (volatile tx_desc*)(phys_start + HHDM_BASE);
        size = PageSize / sizeof(tx_desc);
        memset((void*)ring, 0, PageSize);
        volatile uint32_t* tdbal = (volatile uint32_t*)(mmio_base + E1000_TDBAL);
        volatile uint32_t* tdbah = (volatile uint32_t*)(mmio_base + E1000_TDBAH);
        volatile uint32_t* tdlen = (volatile uint32_t*)(mmio_base + E1000_TDLEN);
        *tdbal = phys_start & 0xFFFFFFFF;
        *tdbah = (phys_start >> 32) & 0xFFFFFFFF;
        *tdlen = size * sizeof(tx_desc);
        volatile uint32_t* tdh = (volatile uint32_t*)(mmio_base + E1000_TDH);
        tdt = (volatile uint32_t*)(mmio_base + E1000_TDT);
        *tdh = 0;
        *tdt = 0;
        tail = 0;
        volatile uint32_t* tipg = (volatile uint32_t*)(mmio_base + E1000_TIPG);
        *tipg = TIPG_DEFAULT;
        volatile uint32_t* tctl = (volatile uint32_t*)(mmio_base + E1000_TCTL);
        *tctl = TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD;
        for (uint32_t i = 0; i < size; i++) {
            ring[i].status = STAT_DD;
            ring[i].addr = -1;
        }
    }
    bool enqueue(Page& data, uint16_t len) {
        if (!(ring[tail].status & STAT_DD)) {
            return false;
        }
        if (ring[tail].addr != -1) {
            phy_page_allocator->put_page(ring[tail].addr);
        }
        phy_page_allocator->get_page(data.phy());
        ring[tail].addr = data.phy();
        ring[tail].length = len;
        ring[tail].cso = 0;
        ring[tail].status = 0;
        ring[tail].cmd = TXD_EOP | TXD_IFCS | TXD_RS;
        ring[tail].css = 0;
        ring[tail].special = 0;
        __asm__ __volatile__("sfence" ::: "memory");

        tail = (tail + 1) % size;
        *tdt = tail;
        return true;
    }
};
class RXRing {
    volatile rx_desc* ring;
    uint64_t phys_start;
    uint32_t size;
    uint32_t cur;
    volatile uint32_t* rdt;
public:
    RXRing(volatile uint8_t* mmio_base) {
        phys_start = phy_page_allocator->alloc_phy_page();
        ring = (volatile rx_desc*)(phys_start + HHDM_BASE);
        size = PageSize / sizeof(rx_desc);
        memset((void*)ring, 0, PageSize);
        cur = 0;
        {
            uint64_t buf = 0;
            for (uint32_t i = 0; i < size; i++) {
                if (i % 2 == 0) buf = phy_page_allocator->alloc_phy_page();
                ring[i].addr = buf + (i & 0b1) * (PageSize / 2);
                ring[i].status = 0;
            }
        }
        volatile uint32_t* rdbal = (volatile uint32_t*)(mmio_base + E1000_RDBAL);
        volatile uint32_t* rdbah = (volatile uint32_t*)(mmio_base + E1000_RDBAH);
        volatile uint32_t* rdlen = (volatile uint32_t*)(mmio_base + E1000_RDLEN);
        *rdbal = phys_start & 0xFFFFFFFF;
        *rdbah = (phys_start >> 32) & 0xFFFFFFFF;
        *rdlen = size * sizeof(rx_desc);

        volatile uint32_t* rdh = (volatile uint32_t*)(mmio_base + E1000_RDH);
        rdt = (volatile uint32_t*)(mmio_base + E1000_RDT);
        *rdh = 0;
        *rdt = size - 1;

        volatile uint32_t* rctl = (volatile uint32_t*)(mmio_base + E1000_RCTL);
        *rctl = RCTL_EN | RCTL_BAM | RCTL_SECRC;
    }
    bool poll(string& out) {
        if (!(ring[cur].status & STAT_DD)) return false;

        uint16_t len = ring[cur].length;
        uint8_t* virt = (uint8_t*)(ring[cur].addr + HHDM_BASE);
        out = string(virt, len);

        ring[cur].status = 0;
        *rdt = cur;
        cur = (cur + 1) % size;
        return true;
    }
};
class E1000 : public Controller, public Ethernet {
private:
    volatile uint8_t* mmio_base;
    uint64_t bar_size;
    TXRing* ring;
    RXRing* rx_ring;
    alignas(TXRing) uint8_t ring_buf[sizeof(TXRing)];
    alignas(RXRing) uint8_t rx_ring_buf[sizeof(RXRing)];
public:
    E1000(uint16_t bus, uint16_t slot, uint16_t func, uint32_t ip);
    ~E1000() override;
    void init() override;
    void hw_send(string& packet) override;
    uint64_t max_packet_size() override { return 1518; }
    void handle_irq();
};

#endif // __E1000_H__