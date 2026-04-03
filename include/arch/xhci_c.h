#ifndef __XHCI_C_H__
#define __XHCI_C_H__

#include "arch/controller.h"
#include "arch/pci.h"
#include "util/size.h"
#include "util/memory.h"
#include "util/vector.h"
struct TRB {
    uint32_t parameter1;   // 데이터 주소 등
    uint32_t parameter2;   // 데이터 주소 상위 32비트 등
    uint32_t status;       // 전송 길이, 인터럽트 설정 등
    uint32_t control;      // TRB 타입, Cycle Bit 등 (가장 중요!)
};
struct EventTRB {
    uint64_t ptr;        // 명령을 내렸던 TRB의 물리 주소 (어떤 명령에 대한 답인지 확인용)
    uint32_t status;     // [24:31] Completion Code (성공 여부)
    // [0:23] 명령마다 다른 추가 정보
    uint32_t control;    // [10:15] TRB Type (32번이면 Command Completion)
    // [0] Cycle Bit (하드웨어가 쓴 새 소식인지 확인용)
} __attribute__((packed));
struct ERSTEntry {
    uint64_t seg_addr;    // Segment의 물리 주소
    uint32_t seg_size;    // 이 Segment에 들어가는 TRB 개수 (4KB면 256)
    uint32_t reserved;
} __attribute__((packed));
class XHCIRing {
private:
    TRB* ring;
    uint64_t phys_start;
    uint32_t size;         // TRB 개수 (예: 256)
    uint32_t enqueue_idx;  // 다음에 쓸 인덱스
    uint8_t cycle_bit;     // 현재 써야 할 Cycle 비트 (초기값 1)

public:
    XHCIRing(uint32_t trb_count = 256) {
        size = trb_count;
        phys_start = phy_page_allocator->alloc_phy_pages((size * sizeof(TRB) + 4095) / 4096);
        ring = (TRB*)(phys_start + MMIO_BASE);
		virt_page_allocator->alloc_virt_pages_range((uint64_t)ring, phys_start, (size * sizeof(TRB) + 4095) & ~0xFFF, VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
        memset(ring, 0, size * sizeof(TRB));

        enqueue_idx = 0;
        cycle_bit = 1;

        // 링의 가장 마지막 칸을 Link TRB로 설정 (워프 게이트)
        TRB* link = &ring[size - 1];
        link->parameter1 = (uint32_t)(phys_start & 0xFFFFFFFF);
        link->parameter2 = (uint32_t)(phys_start >> 32);

        // Control 필드 구성:
        // TRB Type = 6 (Link), TC (Toggle Cycle) = 1
        link->control = (6 << 10) | (1 << 1);
    }
    ~XHCIRing() {
		virt_page_allocator->free_virt_pages((uint64_t)ring, (size * sizeof(TRB) + 4095) / 4096);
	}

    uint64_t push(TRB trb) {
        uint64_t target_phys = (uint64_t)(ring + enqueue_idx) - MMIO_BASE;
        // 1. 현재 주기에 맞는 Cycle 비트 강제 주입
        trb.control = (trb.control & ~1) | cycle_bit;
        ring[enqueue_idx] = trb;
        __asm__ __volatile__("sfence" ::: "memory");
        enqueue_idx++;

        // 2. Link TRB(마지막 칸)에 도달했는지 확인
        if (enqueue_idx == size - 1) {
            // Link TRB 자체도 현재 주기의 Cycle 비트를 가져야 하드웨어가 읽음
            ring[enqueue_idx].control = (ring[enqueue_idx].control & ~1) | cycle_bit;

            // 처음으로 되돌아가기
            enqueue_idx = 0;
            // 중요: 한 바퀴 돌았으므로 다음 주기엔 비트를 반전시킴
            cycle_bit ^= 1;
        }
        return target_phys;
    }

    uint64_t get_phys() { return phys_start; }
};
struct PendingCommand {
    uint64_t trb_ptr;    // 명령 링에 들어간 TRB의 실제 물리 주소 (Key)
    uint32_t type;       // 명령 종류 (Enable Slot, Address Device 등)
    uint32_t port_id;    // 연관된 포트 번호
    void* data;          // 필요시 추가 데이터 (예: 생성된 Device 객체 포인터)
    bool active;         // 현재 추적 중인지 여부
};
class EventRing {
private:
    EventTRB* ring;      // 가상 주소
    uint64_t phys_start; // 물리 주소
    uint32_t size;       // TRB 개수 (예: 256)
    uint32_t dequeue_idx; // 내가 읽을 위치
    uint8_t cycle_bit;    // 내가 기대하는 Cycle Bit (초기값 1)

public:
    EventRing(uint64_t* seg_virt, uint64_t seg_phys, uint32_t trb_count) {
        ring = (EventTRB*)seg_virt;
        phys_start = seg_phys;
        size = trb_count;
        dequeue_idx = 0;
        cycle_bit = 1;
    }

    bool has_event() {
        // 현재 칸의 Cycle Bit가 내가 기다리는 값과 같은지 확인
        return (ring[dequeue_idx].control & 0x1) == cycle_bit;
    }

    EventTRB pop() {
        EventTRB event = ring[dequeue_idx];

        dequeue_idx++;
        if (dequeue_idx == size) {
            dequeue_idx = 0;
            cycle_bit ^= 1; // 끝까지 읽었으니 다음 바퀴 비트 반전
        }

        return event;
    }

    uint64_t get_current_dequeue_phys() {
        return phys_start + (dequeue_idx * sizeof(EventTRB));
    }
    void erdp(uint64_t intr_base) {
        // 1. 현재 OS가 읽어야 할 다음 TRB의 물리 주소를 가져옵니다.
        uint64_t next_phys = get_current_dequeue_phys();

        // 2. 물리 주소에 Bit 3(EHB)을 1로 세팅합니다 (0x08).
        // 하위 3비트는 원래 0이어야 하므로 그대로 더해주면 됩니다.
        uint64_t erdp_val = next_phys | 0x08;

        // 3. Runtime Register 구역의 ERDP(0x18 오프셋)에 씁니다.
        *(volatile uint64_t*)(intr_base + 0x18) = erdp_val;
    }
};
// 1. Command Block Wrapper (CBW) - 호스트가 장치에 명령을 보낼 때 사용
struct CommandBlockWrapper {
    uint32_t signature;          // 반드시 0x43425355 ("USBC"를 리틀 엔디안으로 읽은 값)
    uint32_t tag;                // 명령 식별자 (나중에 CSW와 짝을 맞추기 위함)
    uint32_t data_transfer_len;  // 전송될 데이터의 총 바이트 수
    uint8_t  flags;              // Bit 7: 0=Out(Write), 1=In(Read). 나머지는 0.
    uint8_t  lun;                // Logical Unit Number (보통 0)
    uint8_t  command_len;        // 실제 SCSI 명령(command[])의 유효 길이 (1~16)
    uint8_t  command[16];        // 실제 SCSI 명령 본체 (Read10, Write10, Inquiry 등)
} __attribute__((packed));

// 2. Command Status Wrapper (CSW) - 장치가 호스트에 결과를 보고할 때 사용
struct CommandStatusWrapper {
    uint32_t signature;          // 반드시 0x53425355 ("USBS")
    uint32_t tag;                // CBW에서 보냈던 태그와 동일해야 함
    uint32_t data_residue;       // 실제로 전송되지 않고 남은 데이터 양
    uint8_t  status;             // 0: 성공, 1: 실패, 2: Phase Error (심각)
} __attribute__((packed));
struct SlotContext32 {
    uint32_t info1;    // Route String(0:19), Context Entries(27:31)
    uint32_t info2;    // Root Port Number(16:23), Max Exit Latency(0:15)
    uint32_t tt_info;  // TT Hub Slot ID, TT Port Number 등 (Hub 사용 시)
    uint32_t state;    // Slot State (하드웨어가 기록: Enabled, Addressed, Configured 등)
    uint32_t reserved[4]; // 32바이트 정렬용
} __attribute__((packed));
struct EndpointContext32 {
    uint32_t info1;    // Interval, Max Error Count, EP State
    uint32_t info2;    // EP Type(3:5), Max Packet Size(16:31), Mult(8:9)
    uint64_t tr_ptr;   // Transfer Ring 물리 주소 | Dequeue Cycle State (Bit 0)
    uint32_t avg_len;  // Average TRB Length, Max Payload Hi
    uint32_t reserved[3]; // 32바이트 정렬용
} __attribute__((packed));
struct DeviceContext32 {
    SlotContext32 slot;             // Index 1 (항상 존재)
    EndpointContext32 endpoints[31]; // Index 2 ~ 32
};
struct InputContext32 {
    // Index 0: Input Control Context (어떤 정보를 업데이트할지 체크)
    struct {
        uint32_t drop_flags;     // 삭제할 엔드포인트 비트맵
        uint32_t add_flags;      // 추가/수정할 엔드포인트 비트맵 (Slot=bit 0, EP0=bit 1)
        uint32_t reserved[5];    // 32바이트를 맞추기 위한 패딩
        uint32_t configuration_value : 8;
        uint32_t interface_number : 8;
        uint32_t alternate_setting : 8;
        uint32_t reserved2 : 8;
    } __attribute__((packed)) control;

    // Index 1: Slot Context
    // Index 2~31: Endpoint Contexts
    // (아래 DeviceContext 구조와 동일한 포맷을 사용함)
    DeviceContext32 ctx;
};
struct SlotContext64 {
    uint32_t info1;    // Route String(0:19), Context Entries(27:31)
    uint32_t info2;    // Root Port Number(16:23), Max Exit Latency(0:15)
    uint32_t tt_info;  // TT Hub Slot ID, TT Port Number 등 (Hub 사용 시)
    uint32_t state;    // Slot State (하드웨어가 기록: Enabled, Addressed, Configured 등)
    uint32_t reserved[12]; // 64바이트 정렬용
} __attribute__((packed));
struct EndpointContext64 {
    uint32_t info1;    // Interval, Max Error Count, EP State
    uint32_t info2;    // EP Type(3:5), Max Packet Size(16:31), Mult(8:9)
    uint64_t tr_ptr;   // Transfer Ring 물리 주소 | Dequeue Cycle State (Bit 0)
    uint32_t avg_len;  // Average TRB Length, Max Payload Hi
    uint32_t reserved[11]; // 64바이트 정렬용
} __attribute__((packed));
struct DeviceContext64 {
    SlotContext64 slot;             // Index 1 (항상 존재)
    EndpointContext64 endpoints[31]; // Index 2 ~ 32
};
struct InputContext64 {
    // Index 0: Input Control Context (어떤 정보를 업데이트할지 체크)
    struct {
        uint32_t drop_flags;     // 삭제할 엔드포인트 비트맵
        uint32_t add_flags;      // 추가/수정할 엔드포인트 비트맵 (Slot=bit 0, EP0=bit 1)
        uint32_t reserved[13];    // 64바이트를 맞추기 위한 패딩
        uint32_t configuration_value : 8;
        uint32_t interface_number : 8;
        uint32_t alternate_setting : 8;
        uint32_t reserved2 : 8;
    } __attribute__((packed)) control;

    // Index 1: Slot Context
    // Index 2~31: Endpoint Contexts
    // (아래 DeviceContext 구조와 동일한 포맷을 사용함)
    DeviceContext64 ctx;
};
struct ConfigurationDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType; // 0x02
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed));

struct InterfaceDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType; // 0x04
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;    // 0x08 (Mass Storage)
    uint8_t  bInterfaceSubClass; // 0x06 (SCSI)
    uint8_t  bInterfaceProtocol; // 0x50 (BOT)
    uint8_t  iInterface;
} __attribute__((packed));

struct EndpointDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType; // 0x05
    uint8_t  bEndpointAddress; // Bit 7: 0=Out, 1=In
    uint8_t  bmAttributes;     // Bit 0-1: 02=Bulk
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed));
class XHCIController;
template <typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
class XHCIDevice {
public:
    XHCIDevice(uint32_t slot_id, uint32_t port_id, volatile uint32_t* portsc, uint64_t* xhci_dcbaa, XHCIController* controller)
        : controller(controller), slot_id(slot_id), port_id(port_id), portsc(portsc), xhci_dcbaa(xhci_dcbaa) {}
    void init();
    uint32_t get_slot_id() const { return slot_id; }
	uint32_t get_port_id() const { return port_id; }
    bool send_configure_endpoint_command(uint64_t input_ctx_phys);
    void* operator new(size_t size);
	void operator delete(void* ptr);
    XHCIController* controller;
private:
    uint32_t slot_id;
    uint32_t port_id;
    InputContext* input_ctx;
    DeviceContext* output_ctx;
    volatile uint32_t* portsc;
    uint64_t* xhci_dcbaa;
    uint8_t state;
    uint64_t device_id;
    XHCIRing* ep0_ring;
    uint8_t ep0_ring_buff[sizeof(XHCIRing)];
    struct {
        bool is_msc = false;
        uint8_t config_value = 0;
        uint8_t bulk_in_addr = 0;
        uint8_t bulk_out_addr = 0;
        uint16_t max_packet_size = 0;
    } msc_info;
};
class XHCIController : public Controller {
public:
    XHCIController(uint16_t bus, uint16_t slot, uint16_t func);
    ~XHCIController();
	EventTRB execute_command(TRB cmd);
	EventTRB wait_command(TRB* ptr, uint32_t slot_id, uint32_t port_id, uint32_t expected_type, uint32_t expected_ep = 0);
    uint32_t get_usbsts();
    void init() override;
    uint64_t get_type() override { return 2; }
    void port_status_change(EventTRB ev);
    void command_completion(EventTRB ev);
    volatile uint32_t* doorbell_base;
    EventRing* event_ring;
private:
    volatile uint8_t* mmio_base;         // BAR5 가상 주소
    uint64_t  bar_size;
    XHCIRing* cmd_ring;
    uint8_t cmd_ring_buff[sizeof(XHCIRing)];
	uint8_t event_ring_buff[sizeof(EventRing)];
    vector<XHCIDevice<InputContext32, DeviceContext32, SlotContext32, EndpointContext32>*> devices32;
    vector<XHCIDevice<InputContext64, DeviceContext64, SlotContext64, EndpointContext64>*> devices64;
	vector<PendingCommand> pending_cmds;
    uint64_t intr_base;
    volatile uint32_t* usbsts;
	friend class XHCIDevice<InputContext32, DeviceContext32, SlotContext32, EndpointContext32>;
	friend class XHCIDevice<InputContext64, DeviceContext64, SlotContext64, EndpointContext64>;
};

#endif // __XHCI_C_H__