#include "arch/xhci_c.h"
#include "mm/allocator"
#include "util/util.h"
#include "driver/xhci_p.h"
#include "driver/xhci.h"
#include "driver/hid.h"
#include "arch/handler.h"
#include "kernel/process.h"
#include "arch/lapic.h"
#include "arch/idt.h"
#define XHCI_DEVICE_BASE 0xFFFF850000000000ULL
void mouse_callback(void* event_info, uint64_t status, uint64_t control, void* ctx);
extern bool booting;
void basic_callback(void* event_info, uint64_t status, uint64_t control, void* ctx);
template <typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
void XHCIDevice<InputContext, DeviceContext, SlotContext, EndpointContext>::init() {
	input_ctx = (InputContext*)(phy_page_allocator->alloc_phy_page() + MMIO_BASE);
	output_ctx = (DeviceContext*)(phy_page_allocator->alloc_phy_page() + MMIO_BASE);
    virt_page_allocator->alloc_virt_page((uint64_t)input_ctx, (uint64_t)input_ctx - MMIO_BASE,
		VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
	virt_page_allocator->alloc_virt_page((uint64_t)output_ctx, (uint64_t)output_ctx - MMIO_BASE,
        VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    memset(input_ctx, 0, sizeof(InputContext));
    memset(output_ctx, 0, sizeof(DeviceContext));
    ep0_ring = new (ep0_ring_buff) XHCIRing(256);

    xhci_dcbaa[slot_id] = (uint64_t)output_ctx - MMIO_BASE;
    input_ctx->control.add_flags = 0x3; // Slot과 EP0을 추가/업데이트 대상으로 설정
    input_ctx->ctx.slot.info1 = 0; // Route String은 0으로 (Root Hub 직접 연결)
    input_ctx->ctx.slot.info2 = 0; // Root Port Number는 0으로 (나중에 포트 번호로 업데이트할 예정)
    input_ctx->ctx.slot.state = 0; // Slot Context는 하드웨어가 채울 것이므로 초기값은 0
    TRB addr_trb = { 0 };
    addr_trb.parameter1 = (uint32_t)((uint64_t)input_ctx - MMIO_BASE);
    addr_trb.parameter2 = (uint32_t)(((uint64_t)input_ctx - MMIO_BASE) >> 32);
    addr_trb.control = (11 << 10) | (slot_id << 24);

    uint8_t speed = (*portsc >> 10) & 0x0F;
    input_ctx->ctx.slot.info1 |= (speed << 20);
    input_ctx->ctx.slot.info2 |= (port_id << 16);
    uint32_t max_packet = 8;
	uart_print("Device Speed: ");
    switch (speed) {
        case 1: uart_print("Full Speed\n"); break;
        case 2: uart_print("Low Speed\n"); break;
        case 3: uart_print("High Speed\n"); break;
        case 4: uart_print("Super Speed\n"); break;
        case 5: uart_print("Super Speed Plus\n"); break;
        default: uart_print("Unknown Speed\n"); break;
	}
    if (speed == 4 || speed == 5) max_packet = 512; // SS 이상
    else if (speed == 3) max_packet = 64;           // HS
    else if (speed == 1) max_packet = 64;           // FS (일단 64로 시도)
    input_ctx->ctx.endpoints[0].tr_ptr = (ep0_ring->get_phys()) | 1;
    input_ctx->ctx.endpoints[0].avg_len = 8;
    input_ctx->ctx.endpoints[0].info2 = (4 << 3) | (max_packet << 16);
    input_ctx->ctx.endpoints[0].info1 = 0x3 << 1; // Error Count = 3
    input_ctx->ctx.slot.info1 |= (1 << 27);
    uart_print("portsc now="); uart_print_hex(*portsc); uart_print("\n");
    EventTRB res = controller->execute_command(addr_trb);
    uint8_t code = (res.status >> 24) & 0xFF; // Completion Code

    if (code == 1) {
        uart_print("Address Device Success!\n");
        // 여기서부터 드디어 ep0_ring을 사용하여 Get Descriptor를 날릴 수 있습니다.
        // 1. Setup Stage TRB
        TRB setup = { 0 };
        setup.parameter1 = 0x01000680; // bmRequestType(80), bRequest(06:GetDesc), wValue(0100:Device)
        setup.parameter2 = 0x00120000; // wIndex(0), wLength(18)
        setup.status = 8;              // TRB Transfer Length (항상 8)
        setup.control = (2 << 10) | (1 << 6); // IDT(6), Type(Setup:2)

        // 2. Data Stage TRB (데이터를 받을 버퍼)
        uint64_t desc_phys = phy_page_allocator->alloc_phy_page(); // 18바이트 담을 공간
        virt_page_allocator->alloc_virt_page((uint64_t)(desc_phys + MMIO_BASE), desc_phys,
			VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
        TRB data = { 0 };
        data.parameter1 = (uint32_t)desc_phys;
        data.parameter2 = (uint32_t)(desc_phys >> 32);
        data.status = 18;              // 받을 크기 (18바이트)
        data.control = (3 << 10) | (1 << 16); // Type(Data:3), DIR(In:1)

        // 3. Status Stage TRB
        TRB status = { 0 };
        status.control = (4 << 10) | (1 << 5); // Type(Status:4)

        // --- ep0_ring에 넣고 도어벨 울리기 ---
        ep0_ring->push(setup);
        ep0_ring->push(data);
        TRB* status_ptr = (TRB*)ep0_ring->push(status);

        // 장치 슬롯의 EP0(Target 1) 도어벨을 울립니다.
        __asm__ __volatile__("sfence" ::: "memory");
		controller->doorbell_base[slot_id] = 1;
        EventTRB ev1 = controller->wait_command(status_ptr, slot_id, port_id, 32); // Transfer Event 이벤트 대기 (Type 32)
        uart_print("1st code="); uart_print_hex((ev1.status >> 24) & 0xFF); uart_print("\n");
        uint8_t* desc = (uint8_t*)(desc_phys + MMIO_BASE);

        uart_print("USB Device Found!\n");
        uart_print("Vendor ID: ");
		uint64_t vender_id = desc[8] | (desc[9] << 8); // 8, 9번 바이트
        uart_print_hex(vender_id);
        uart_print("\nProduct ID: ");
		uint64_t product_id = desc[10] | (desc[11] << 8); // 10, 11번 바이트
        uart_print_hex(product_id);
        uart_print("USBSTS="); uart_print_hex(controller->get_usbsts()); uart_print("\n");
        uart_print("ep0 state=");
        uart_print_hex(output_ctx->endpoints[0].info1 & 0x7);
        uart_print("\n");
		setup.parameter1 = 0x02000680; // wValue(0200:Config)
        setup.parameter2 = 0x00090000; //0x00090000
		setup.status = 8;              // TRB Transfer Length (항상 8)
		setup.control = 0x00000840; // IDT(1), Type(Setup:2), Cycle(1)

		data.status = 0x09;              // 받을 크기 (9바이트)
		data.control = 0x00010C00; // Type(Data:3), DIR(In:1), Cycle(1)

        status.control = 0x00001060; // Type(Status:4), Cycle(1)
		ep0_ring->push(setup);
		ep0_ring->push(data);
        status_ptr = (TRB*)ep0_ring->push(status);
        memset((void*)(desc_phys + MMIO_BASE), 0, 4096);
        __asm__ __volatile__("sfence" ::: "memory");
        controller->doorbell_base[slot_id] = 0; // 한번 클리어
        __asm__ __volatile__("mfence" ::: "memory");
		controller->doorbell_base[slot_id] = 1;
        EventTRB ev2 = controller->wait_command(status_ptr, slot_id, port_id, 32); // Transfer Event 이벤트 대기 (Type 32)
        uart_print("2nd code="); uart_print_hex((ev2.status >> 24) & 0xFF); uart_print("\n");
        uart_print("total_length: ");
        uint16_t total_length = desc[2] | (desc[3] << 8);
        uart_print_hex(total_length);
        uart_print("\n");
        if (total_length > 4096) {
			return; // 이상한 데이터 방지
        }
        
        setup.parameter2 = ((uint32_t)total_length << 16);
		data.status = total_length; // 받을 크기 (전체 Configuration Descriptor 크기)
		//status도 동일
		ep0_ring->push(setup);
		ep0_ring->push(data);
        status_ptr = (TRB*)ep0_ring->push(status);
        memset((void*)(desc_phys + MMIO_BASE), 0, 4096);
        __asm__ __volatile__("sfence" ::: "memory");
		controller->doorbell_base[slot_id] = 1;
        controller->wait_command(status_ptr, slot_id, port_id, 32); // Transfer Event 이벤트 대기 (Type 32)
        
        uint8_t* ptr = desc;
        uint8_t* end = desc + total_length;

        uart_print("\n--- Parsing Configuration Descriptor ---\n");

        while (ptr < end) {
            uint8_t length = ptr[0];
            uint8_t type = ptr[1];

            if (length == 0) break; // 잘못된 데이터 방지

            switch (type) {
            case 0x02: { // Configuration
                ConfigurationDescriptor* cfg = (ConfigurationDescriptor*)ptr;
                if (device_type == DEVICE_MSC) {
                    device_info.msc.config_value = cfg->bConfigurationValue;
                }
                break;
            }
            case 0x04: { // Interface
                InterfaceDescriptor* iface = (InterfaceDescriptor*)ptr;
                uart_print("\nInterface Class: "); uart_print_hex(iface->bInterfaceClass);
                uart_print(" Protocol: "); uart_print_hex(iface->bInterfaceProtocol); // 추가
                if (iface->bInterfaceClass == 0x08 &&
                    iface->bInterfaceSubClass == 0x06 &&
                    iface->bInterfaceProtocol == 0x50) {
					device_type = DEVICE_MSC;
                    uart_print(" [MSC/SCSI/BOT Detected!]");
                }
                else if (iface->bInterfaceClass == 0x03) {
                    uart_print(" InterfaceNum: "); uart_print_hex(iface->bInterfaceNumber);
                    uart_print(" NumEndpoints: "); uart_print_hex(iface->bNumEndpoints);
                    if (device_type == DEVICE_HID) {
                        break; // 이미 감지됐으면 스킵
                    }
                    device_type = DEVICE_HID;   //정확한 분류는 나중에
					uart_print(" [HID Detected!]");
					device_info.hid.interface_number = iface->bInterfaceNumber;
                }
                break;
            }
            case 0x05: { // Endpoint
                EndpointDescriptor* ep = (EndpointDescriptor*)ptr;
                if (device_type == DEVICE_MSC) {
                    if ((ep->bmAttributes & 0x03) == 0x02) {
                        if (ep->bEndpointAddress & 0x80) {
                            device_info.msc.bulk_in_addr = ep->bEndpointAddress;
                            max_packet_size = ep->wMaxPacketSize;
                        }
                        else {
                            device_info.msc.bulk_out_addr = ep->bEndpointAddress;
                        }
                    }
                }
                else if (device_type == DEVICE_HID) {
                    if ((ep->bmAttributes & 0x03) == 0x03 && (ep->bEndpointAddress & 0x80)) {
                        device_info.hid.ep_addr = ep->bEndpointAddress;
                        device_info.hid.interval = ep->bInterval;
                        max_packet_size = ep->wMaxPacketSize;
                    }
                }
                break;
            }
            case 0x21: { // HID Descriptor
                uint16_t rep_len = ptr[7] | (ptr[8] << 8); // wDescriptorLength
                device_info.hid.report_desc_len = rep_len;
				uart_print("\nHID Report Descriptor Length: "); uart_print_hex(rep_len);
                break;
            }
            }
            ptr += length; // 다음 디스크립터로 점프
        }
        uart_print("\n---------------------------------------\n");
        switch (device_type) {
        case DEVICE_MSC:
        {
            uint8_t in_ep_num = device_info.msc.bulk_in_addr & 0x0F; // 0x81 → 1
            uint8_t out_ep_num = device_info.msc.bulk_out_addr & 0x0F; // 0x02 → 2
            uint8_t dci_in = (in_ep_num * 2) + 1; // 3
            uint8_t dci_out = (out_ep_num * 2);      // 4

            auto* protocol = new XHCI_BOT_Protocol<InputContext, DeviceContext, SlotContext, EndpointContext>(
                this, slot_id, dci_in, dci_out);
            protocol->initialize();
            XHCIDisk* disk = new XHCIDisk(0, 0, 0, port_id, protocol);
            disk->init();

            if (controller->on_disk_found) {
                controller->on_disk_found(disk);
            }
            break;
        }
        case DEVICE_HID: {
            if (device_info.hid.report_desc_len == 0)
                device_info.hid.report_desc_len = 256; // fallback
            uint8_t ep_num = device_info.hid.ep_addr & 0x0F;
            uint8_t dci = (ep_num * 2) + 1;

            // GET_DESCRIPTOR (HID Report Descriptor)
            TRB setup = { 0 };
            setup.parameter1 = 0x22000681; // ← 이렇게 해야 함
            setup.parameter2 = ((uint32_t)device_info.hid.report_desc_len << 16)
                | device_info.hid.interface_number;
            setup.status = 8;
            setup.control = (2 << 10) | (1 << 6);

            uint64_t rep_phys = phy_page_allocator->alloc_phy_page();
            virt_page_allocator->alloc_virt_page(rep_phys + MMIO_BASE, rep_phys,
                VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
            memset((void*)(rep_phys + MMIO_BASE), 0, 4096);

            TRB data = { 0 };
            data.parameter1 = (uint32_t)rep_phys;
            data.parameter2 = (uint32_t)(rep_phys >> 32);
            data.status = device_info.hid.report_desc_len;
            data.control = (3 << 10) | (1 << 16);

            TRB status = { 0 };
            status.control = (4 << 10) | (1 << 5);

            ep0_ring->push(setup);
            ep0_ring->push(data);
            TRB* status_ptr = (TRB*)ep0_ring->push(status);
            __asm__ __volatile__("sfence" ::: "memory");
            controller->doorbell_base[slot_id] = 1;
            controller->wait_command(status_ptr, slot_id, port_id, 32, 1);

            auto* hid = new XHCIHIDDevice<InputContext, DeviceContext, SlotContext, EndpointContext>(this, dci);
            hid->max_packet_size = max_packet_size;
            hid->hid_init((uint8_t*)(rep_phys + MMIO_BASE), device_info.hid.report_desc_len);
            break;
        }
        default:
            break;
        }
    }
    else {
        uart_print("param1="); uart_print_hex(addr_trb.parameter1); uart_print("\n");
        uart_print("ep0 info2="); uart_print_hex(input_ctx->ctx.endpoints[0].info2); uart_print("\n");
        uart_print("slot info2="); uart_print_hex(input_ctx->ctx.slot.info2); uart_print("\n");
        uart_print("Failed with Code: ");
        uart_print_hex(code);
		uart_print("\n");
        __asm__ __volatile__("hlt");
    }
}

template <typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
bool XHCIDevice<InputContext, DeviceContext, SlotContext, EndpointContext>::send_configure_endpoint_command(uint64_t input_ctx_phys) {
    // 1. Configure Endpoint Command TRB 구성 (Type 12)
    TRB trb;
    memset(&trb, 0, sizeof(TRB));

    // Parameter: Input Context의 물리 주소
    trb.parameter1 = (uint32_t)(input_ctx_phys & 0xFFFFFFFF);
    trb.parameter2 = (uint32_t)(input_ctx_phys >> 32);

    // Control: TRB Type(12) | Slot ID
    // Slot ID는 비트 24~31에 들어갑니다.
    trb.control = (12 << 10) | (static_cast<uint32_t>(slot_id) << 24);

    // 2. Command Ring에 넣고 벨 누르기
	EventTRB result = controller->execute_command(trb);

    // 3. 결과 확인 (Completion Code가 1이면 성공)
    uint8_t completion_code = (result.status >> 24) & 0xFF;
	return completion_code == 1;
}
template <typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
void* XHCIDevice<InputContext, DeviceContext, SlotContext, EndpointContext>::operator new(size_t size) {
    XHCIDevice* result = (XHCIDevice*)XHCI_DEVICE_BASE;
    uint64_t index = 0;
    while (result->state == 1) {
        result++;
        index++;
    }
    result->state = 1;
    result->device_id = index;
    return result;
}
template <typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
void XHCIDevice<InputContext,DeviceContext, SlotContext, EndpointContext>::operator delete(void* ptr) {
    XHCIDevice* dev = (XHCIDevice*)ptr;
    dev->state = 0;
}

template class XHCIDevice<InputContext32, DeviceContext32, SlotContext32, EndpointContext32>;
template class XHCIDevice<InputContext64, DeviceContext64, SlotContext64, EndpointContext64>;

XHCIController::XHCIController(uint16_t bus, uint16_t slot, uint16_t func)
    : Controller(bus, slot, func), mmio_base(nullptr), bar_size(0) {
}

XHCIController::~XHCIController() {
    if (!mmio_base) return;
    for (uint64_t off = 0; off < bar_size; off += 4096)
        virt_page_allocator->free_virt_page((uint64_t)mmio_base + off);
}
void XHCIController::init() {
    uart_print("[XHCI] ===== init start =====\n");

    // 1. BAR0 매핑
    pci_bar_info_t bar = pci_get_bar_size(pci_bus, pci_slot, pci_func, 0x10);
    uart_print("[XHCI] BAR0 addr="); uart_print_hex(bar.addr);
    uart_print(" size=");            uart_print_hex(bar.size);
    uart_print("\n");

    uint16_t pci_cmd = pci_read16(pci_bus, pci_slot, pci_func, 0x04);
    uart_print("[XHCI] PCI CMD before="); uart_print_hex(pci_cmd);
    uart_print("\n");
    pci_cmd |= (1u << 2) | (1u << 1);  // Bus Master + Memory Space
    pci_write16(pci_bus, pci_slot, pci_func, 0x04, pci_cmd);
    uart_print("[XHCI] PCI CMD after="); uart_print_hex(pci_read16(pci_bus, pci_slot, pci_func, 0x04));
    uart_print("\n");

    mmio_base = (volatile uint8_t*)(bar.addr + MMIO_BASE);
    if (bar.addr > phy_page_allocator->get_total_pages() * PageSize) {
        mmio_base = (volatile uint8_t*)mmio_bump;
        mmio_bump += bar.size;
    }
    uart_print("[XHCI] MMIO base="); uart_print_hex((uint64_t)mmio_base);
    uart_print("\n");
    for (uint64_t off = 0; off < bar.size; off += 4096) {
        uint64_t result = virt_page_allocator->alloc_virt_page((uint64_t)mmio_base + off,
            bar.addr + off,
            VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    }
    uint32_t hccparams1 = *(volatile uint32_t*)(mmio_base + 0x10);
    uint32_t xecp_offset = ((hccparams1 >> 16) & 0xFFFF) << 2;

    if (xecp_offset) {
        volatile uint32_t* xecp = (volatile uint32_t*)(mmio_base + xecp_offset);
        while (true) {
            uint32_t cap = *xecp;
            if ((cap & 0xFF) == 1) { // USBLEGSUP capability
                uart_print("[XHCI] BIOS Handoff start\n");
                *xecp |= (1 << 24); // OS Owned Semaphore = 1

                // BIOS Owned(bit16)=0, OS Owned(bit24)=1 될 때까지 대기
                while ((*xecp & (1 << 16)) || !(*xecp & (1 << 24))) {
                    __asm__ __volatile__("pause");
                }
                uart_print("[XHCI] BIOS Handoff done\n");
                break;
            }
            uint32_t next = (cap >> 8) & 0xFF;
            if (!next) break;
            xecp += next; // uint32_t* 이므로 4바이트씩 증가 = 스펙상 DWORD 단위 오프셋
        }
    }
    uint8_t cap_length = *(volatile uint8_t*)mmio_base;
    uint64_t op_base = (uint64_t)mmio_base + cap_length;
    volatile uint32_t* usbcmd = (volatile uint32_t*)(op_base + 0x00);
    usbsts = (volatile uint32_t*)(op_base + 0x04);

    *usbcmd &= ~0x01; // RS(Run/Stop) 비트 클리어

    // 2. 하드웨어가 실제로 멈췄는지 확인 (HCHalted 비트가 1이 될 때까지)
    while (!(*usbsts & 0x01)) {
        // 하드웨어가 멈추길 기다림 (timeout 로직을 넣으면 더 견고함)
        __asm__ __volatile__("pause");
    }

    // 3. 리셋 명령 투하 (HCRST 비트 세팅)
    *usbcmd |= 0x02; // HCRST(Host Controller Reset) 비트 세팅

    // 4. 하드웨어가 리셋을 마치고 비트를 스스로 0으로 돌릴 때까지 대기
    while (*usbcmd & 0x02) {
        __asm__ __volatile__("pause");
    }

    // 5. [중요] 리셋 직후에는 컨트롤러가 내부 정리를 마칠 때까지 아주 잠깐 더 기다려야 함
    // 규격상 CNR(Controller Not Ready) 비트가 0이 될 때까지 확인하는 것이 정석
    while (*usbsts & (1 << 11)) { // Bit 11: CNR
        __asm__ __volatile__("pause");
    }

    uint32_t hcsparams2 = *(volatile uint32_t*)(mmio_base + 0x08);

    uint32_t max_sp_low = (hcsparams2 >> 21) & 0x1F;
    uint32_t max_sp_high = (hcsparams2 >> 27) & 0x1F;
    uint32_t max_sp_buffers = (max_sp_high << 5) | max_sp_low;
    uart_print("[XHCI] HCSParams2="); uart_print_hex(hcsparams2);
    uart_print(" Max Scratchpad Buffers="); uart_print_hex(max_sp_buffers);
    uart_print("\n");

    uint64_t* scratchpad = (uint64_t*)MMIO_BASE;
    if (max_sp_buffers != 0) {
        scratchpad = (uint64_t*)phy_page_allocator->alloc_phy_pages(((uint64_t)max_sp_buffers * sizeof(uint64_t*) + PageSize - 1) / PageSize);
        virt_page_allocator->alloc_virt_page((uint64_t)scratchpad + MMIO_BASE, (uint64_t)scratchpad,
            VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
        scratchpad = (uint64_t*)((uint64_t)scratchpad + MMIO_BASE);
        for (uint32_t i = 0; i < max_sp_buffers; i++) {
            scratchpad[i] = phy_page_allocator->alloc_phy_page();
            memset((void*)(scratchpad[i] + HHDM_BASE), 0, 4096);
        }
    }

    uint32_t hcsparams1 = *(volatile uint32_t*)(mmio_base + 0x04);
    uint32_t max_slots = hcsparams1 & 0xFF;
    uart_print("[XHCI] HCSParams1="); uart_print_hex(hcsparams1);
    uart_print(" Max Slots="); uart_print_hex(max_slots);
    uart_print("\n");

    volatile uint32_t* config_reg = (volatile uint32_t*)(op_base + 0x38);
    *config_reg = (max_slots & 0xFF);

    size_t dcbaa_size = (uint64_t)(max_slots + 1) * sizeof(uint64_t);
    uint64_t* dcbaa = (uint64_t*)(phy_page_allocator->alloc_phy_pages((dcbaa_size + PageSize - 1) / PageSize));
    virt_page_allocator->alloc_virt_page((uint64_t)dcbaa + MMIO_BASE, (uint64_t)dcbaa,
        VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    dcbaa = (uint64_t*)((uint64_t)dcbaa + MMIO_BASE);
    memset(dcbaa, 0, dcbaa_size);
    dcbaa[0] = (uint64_t)scratchpad - MMIO_BASE;
    __asm__ __volatile__("sfence" ::: "memory");

    volatile uint64_t* dcbaap_reg = (volatile uint64_t*)(op_base + 0x30);
    *dcbaap_reg = (uint64_t)dcbaa - MMIO_BASE;

    cmd_ring = new (cmd_ring_buff) XHCIRing(256);
    // 2. CRCR (Command Ring Control Register) 등록
// op_base + 0x18 위치에 물리 주소와 초기 Cycle Bit(1)를 써줍니다.
    volatile uint64_t* crcr_reg = (volatile uint64_t*)(op_base + 0x18);
    *crcr_reg = cmd_ring->get_phys() | 0x01; // Bit 0은 RCS (Ring Cycle State)
    uart_print("[XHCI] Command Ring Phys="); uart_print_hex(cmd_ring->get_phys());
    uart_print("\n");
    uint32_t* cap_regs = (uint32_t*)mmio_base;

    // HCCPARAMS1은 오프셋 0x10 (4바이트 단위로는 index 4)
    //uint32_t hccparams1 = cap_regs[4];

    // Bit 2: CSZ (Context Size)
    // 0 = 32-byte Contexts
    // 1 = 64-byte Contexts
    bool is_64byte_context = (hccparams1 >> 2) & 0x1;

    if (is_64byte_context) {
        uart_print("Hardware uses 64-byte Contexts (CSZ=1)\n");
    }
    else {
        uart_print("Hardware uses 32-byte Contexts (CSZ=0)\n");
    }
    uart_print("xHCI VID="); uart_print_hex(pci_read16(pci_bus, pci_slot, pci_func, 0x00)); uart_print("\n");
    uart_print("xHCI DID="); uart_print_hex(pci_read16(pci_bus, pci_slot, pci_func, 0x02)); uart_print("\n");
    uint64_t* event_seg_virt = (uint64_t*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
    uint64_t event_seg_phys = (uint64_t)event_seg_virt - HHDM_BASE;
    memset(event_seg_virt, 0, 4096);

    ERSTEntry* erst = (ERSTEntry*)(phy_page_allocator->alloc_phy_page() + HHDM_BASE);
    uint64_t erst_phys = (uint64_t)erst - HHDM_BASE;
    memset(erst, 0, 4096);

    erst[0].seg_addr = (uint64_t)event_seg_virt - HHDM_BASE;
    erst[0].seg_size = 256;

    uint32_t rtsoff = *(volatile uint32_t*)(mmio_base + 0x18);
    uint64_t rt_base = (uint64_t)mmio_base + rtsoff;

    intr_base = rt_base + 0x20;

    // ERST 크기 설정 (우리는 1개만 썼으니 1)
    *(volatile uint32_t*)(intr_base + 0x08) = 1;

    // ERST 주소 설정 (64비트 물리 주소)
    *(volatile uint64_t*)(intr_base + 0x10) = erst_phys;

    // ERDP (Dequeuer Pointer) 설정: 
    // 하드웨어에게 "나 여기서부터 읽을 거야"라고 알려주는 포인터.
    // 처음에는 첫 번째 Segment의 물리 주소를 넣어줍니다.
    *(volatile uint64_t*)(intr_base + 0x18) = event_seg_phys;

    event_ring = new (event_ring_buff) EventRing(event_seg_virt, event_seg_phys, 256);

    uint32_t dboff = *(volatile uint32_t*)(mmio_base + 0x14);
    doorbell_base = (volatile uint32_t*)(mmio_base + dboff);

    volatile uint32_t* iman = (volatile uint32_t*)intr_base;

    // 컨트롤러 시작
    *usbcmd |= 0x01; // RS = 1

    // 3. 엔진이 안정화될 때까지 대기 (HCHalted 비트가 0이 되어야 함)
    while (*usbsts & 0x01) {
        __asm__ __volatile__("pause");
    }
    *iman |= 0x02;
    TRB enable_slot = { 0 };
    //noop.control = (23 << 10); // TRB Type 23: No-Op
    enable_slot.control = (9 << 10) | 1; // Enable Slot Command, Cycle Bit 1
    // 아까 만드신 CommandRing 클래스에 push!

    uint32_t status = *usbsts;
    volatile uint32_t* portscs = (volatile uint32_t*)(op_base + 0x400); // 0번 포트
    //uart_print("PORTSC: "); uart_print_hex(portsc);
    uart_print("\n[XHCI] USBSTS: ");
    uart_print_hex(status);
    uart_print("\n");
    __asm__ __volatile__("sfence" ::: "memory");

    bool ok = setup_msix(pci_bus, pci_slot, pci_func, { 0x35, 0 });
    // xHCI 자체 인터럽트 Enable은 컨트롤러마다 다르니까 여기서
    *(volatile uint32_t*)(intr_base) |= 0x03; // IMAN
    *usbcmd |= (1 << 2); // INTE
    uart_print("[MSIX] IMAN=");     uart_print_hex(*(volatile uint32_t*)(intr_base)); uart_print("\n");
    uart_print("[MSIX] USBCMD=");   uart_print_hex(*usbcmd); uart_print("\n");
    if (!ok) {
        uart_print("[XHCI] MSI-X not supported!\n");
        return;
    }

    uint32_t max_ports = (hcsparams1 >> 24) & 0xFF;
    while (event_ring->has_event()) {
		event_ring->pop(); // 초기 이벤트 클리어
    }
    for (uint32_t i = 1; i <= max_ports; i++) {
        //uint32_t portsc = portscs[i - 1];
        volatile uint32_t* portsc_reg = (volatile uint32_t*)(op_base + 0x400 + (i - 1) * 0x10);
        volatile uint32_t portsc = *portsc_reg;
        if (!(*portsc_reg & (1 << 9))) continue; // PP=0이면 스킵
        if (!(*portsc_reg & 0x1)) {
            // CCS=0일 때만 리셋 시도
            bool is_usb3 = (*portsc_reg & (1 << 19)); // WRC 비트로 구분
            if (is_usb3) {
                *portsc_reg = (*portsc_reg & ~0x1F) | (1u << 31);
                while (*portsc_reg & (1u << 31)) { __asm__ __volatile__("pause"); }
                while (*portsc_reg & (1u << 4)) { __asm__ __volatile__("pause"); }
            }
            else {
                *portsc_reg = (*portsc_reg & ~0x1F) | (1 << 4);
                while (*portsc_reg & (1 << 4)) { __asm__ __volatile__("pause"); }
            }
            portsc = *portsc_reg;
        }
        while (*portsc_reg & (1u << 4)) { __asm__ __volatile__("pause"); }
        // W1C 비트만 클리어 (하위 5비트 건드리지 않음)
        uint32_t cur = *portsc_reg;
        // RW 비트 보존, W1C 비트만 1로 세팅
        *portsc_reg = (cur & ~0x1F) | (cur & ((1 << 17) | (1 << 18) | (1 << 20) | (1 << 21) | (1 << 22) | (1 << 23)));

        // PED=1 될 때까지 대기
        //while (!(*portsc_reg & (1 << 1))) { __asm__ __volatile__("pause"); }
        portsc = *portsc_reg;
        uint32_t pls = (*portsc_reg >> 5) & 0xF;
		//uart_print("[XHCI] Port "); uart_print(i);
        //uart_print(" PORTSC="); uart_print_hex(portsc); uart_print("\n");
        if (pls == 5) continue; // RxDetect = 장치 없음

        if (portsc & 0x1) { // 장치 발견!
            // 1. Enable Slot 던지기
            cmd_ring->push(enable_slot);
            doorbell_base[0] = 0;

            // 2. 이 명령에 대한 응답이 올 때까지 대기
			EventTRB ev;
            do {
                while (!event_ring->has_event()) { __asm__ __volatile__("pause"); }
				ev = event_ring->pop();
                event_ring->erdp(intr_base);
            } while (((ev.control >> 10) & 0x3F) != 0x21);

            // 3. 이벤트 처리 및 Slot ID 저장
			uint8_t comp_code = (ev.status >> 24) & 0xFF;
            uint32_t slot_id = (ev.control >> 24) & 0xFF;
			//uart_print("Enable Slot Event: Slot ID="); uart_print_hex(slot_id);
			//uart_print("\n");
            if (((ev.control >> 10) & 0x3F) == 0x21 && comp_code == 1) {
                if (is_64byte_context) {
                    auto dev = new XHCIDevice<InputContext64, DeviceContext64, SlotContext64, EndpointContext64>(slot_id, i, portsc_reg, dcbaa, this);
                    devices64.push_back(dev);
					dev->init();
                }
                else {
                    auto dev = new XHCIDevice<InputContext32, DeviceContext32, SlotContext32, EndpointContext32>(slot_id, i, portsc_reg, dcbaa, this);
					devices32.push_back(dev);
					dev->init();
                }
            }
        }
    }
}
void XHCIController::eoi() {
    event_ring->erdp(intr_base);
    *usbsts = 0x18;
    *(volatile uint32_t*)(intr_base) |= 0x01;
}
EventTRB XHCIController::execute_command(TRB cmd) {
    cmd_ring->push(cmd);
    doorbell_base[0] = 0;
    EventTRB ev;
    do {
        while (!event_ring->has_event()) { __asm__ __volatile__("pause"); }
        ev = event_ring->pop();
        event_ring->erdp(intr_base);
    } while (((ev.control >> 10) & 0x3F) != 0x21);
    return ev;
}
EventTRB XHCIController::wait_command(TRB* ptr, uint32_t slot_id, uint32_t port_id, uint32_t expected_type, uint32_t expected_ep) {
    EventTRB ev = {0,};
    int count = 0;
    uint8_t cap_length = *(volatile uint8_t*)mmio_base;
    uint64_t op_base = (uint64_t)mmio_base + cap_length;
    volatile uint32_t* portsc_reg = (volatile uint32_t*)(op_base + 0x400 + (port_id - 1) * 0x10);
    volatile uint32_t portsc = *portsc_reg;
	//uart_print("[XHCI] portsc="); uart_print_hex(portsc); uart_print("\n");
    if (booting) {
        while(1) {
            while (!event_ring->has_event()) { 
                uart_print("\r[XHCI] USBTS="); uart_print_hex(*usbsts); uart_print("             ");
            }
            ev = event_ring->pop();
            event_ring->erdp(intr_base);
            *(volatile uint32_t*)(op_base + 0x04) = 0x08; // EINT 클리어
            *(volatile uint32_t*)(intr_base) |= 0x01;      // IMAN IP 클리어
            count++;
			uart_print("[XHCI] Waiting for Command Completion... count="); uart_print_hex(count); uart_print("\n");
            uart_print("[XHCI] Event: Type="); uart_print_hex((ev.control >> 10) & 0x3F);
            uart_print(" Completion="); uart_print_hex((ev.status >> 24) & 0xFF);
            uart_print("\n");
            if (ev.ptr != (uint64_t)ptr ||
                ((ev.control >> 10) & 0x3F) != expected_type ||
                ((ev.control >> 24) & 0xFF) != slot_id ||
                (expected_ep != 0 && ((ev.control >> 16) & 0xFF) != expected_ep)) {
                for (int i = 0; i < xhci_event->size(); i++) {
                    KEvent event = (*xhci_event)[i];
                    if (event.arg[0] == ev.ptr && event.arg[1] == ((ev.control >> 24) & 0xFF) && event.arg[2] == ((ev.control >> 16) & 0xFF)) {
                        if (event.callback != nullptr && event.callback != basic_callback)
                            event.callback(&event, ev.status, ev.control, event.callback_ctx);
                        uart_print("[XHCI] KEvent arg0="); uart_print_hex(event.arg[0]);
                        uart_print(" arg1="); uart_print_hex(event.arg[1]);
                        uart_print(" arg2="); uart_print_hex(event.arg[2]);
                        uart_print("\n");
                        xhci_event->erase(i);
                        i--;
                    }
                }
            }
            else {
				uart_print("[XHCI] Command Completed! ptr="); uart_print_hex(ev.ptr);
                uart_print("\n");
                break;
            }
        }
    }
    else {
        uint64_t result = call_xhci(ptr, (uint64_t)slot_id, (uint64_t)expected_ep);
        ev.ptr = (uint64_t)ptr;
        ev.status = result >> 32;
        ev.control = result & 0xFFFFFFFF;
        event_ring->erdp(intr_base);
    }
    //uart_print("transfer length remaining=");
    //uart_print_hex(ev.status & 0xFFFFFF);  // 못 받은 바이트 수
    //uart_print("\n");
    return ev;
}
uint32_t XHCIController::get_usbsts() {
    return *usbsts;
}
void XHCIController::port_status_change(EventTRB ev) {

}
void XHCIController::command_completion(EventTRB ev) {

}
static DeviceType devices[256];
extern vector<Controller*>* controllers;
__attribute__((interrupt))
void xhci_handler(interrupt_frame_t* frame) {
    //uart_print("[XHCI] handler!\n"); // 불리는지 확인
    for (int i = 0; i < controllers->size(); i++) {
        Controller* controller = (*controllers)[i];
        if (controller->get_type() != 2) continue;
        XHCIController* contr = (XHCIController*)controller;
        while (contr->event_ring->has_event()) {
            EventTRB ev = contr->event_ring->pop();
            uint8_t type = (ev.control >> 10) & 0x3F;
            /*
            uart_print("[XHCI] Event TRB: Type="); uart_print_hex(type);
            uart_print(" SlotID="); uart_print_hex((ev.control >> 24) & 0xFF);
            uart_print(" EPID="); uart_print_hex((ev.control >> 16) & 0xFF);
            uart_print("\n");
            */
            if (type == 34) {
                contr->port_status_change(ev);
                continue;
            }
            else if (type == 33) {
                contr->command_completion(ev);
            }
            else if (type == 32) {
                uint8_t slot_id = (uint8_t)(ev.control >> 24);         // 상위 8비트
                uint8_t endpoint_id = (uint8_t)((ev.control >> 16) & 0x1F); // 16~20비트
                /*
                uart_print("[XHCI] Transfer Event ptr="); uart_print_hex(ev.ptr);
                uart_print(" slot="); uart_print_hex(slot_id);
                uart_print(" ep="); uart_print_hex(endpoint_id);
                uart_print("\n");
                */
                for (int i = 0; i < xhci_event->size(); i++) {
                    KEvent event = (*xhci_event)[i];
                    /*
                    uart_print("[XHCI] KEvent arg0="); uart_print_hex(event.arg[0]);
                    uart_print(" arg1="); uart_print_hex(event.arg[1]);
                    uart_print(" arg2="); uart_print_hex(event.arg[2]);
                    uart_print("\n");
                    */
                    if (event.arg[0] == ev.ptr && event.arg[1] == slot_id && event.arg[2] == endpoint_id) {
                        event.callback(&event, ev.status, ev.control, event.callback_ctx);
                        xhci_event->erase(i);
                        i--;
                    }
                }
            }
        }
        contr->eoi();
    }
    lapic_eoi();
}
void test_xhci() {
    for (int i = 0; i < controllers->size(); i++) {
        Controller* controller = (*controllers)[i];
        if (controller->get_type() != 2) continue;
        XHCIController* contr = (XHCIController*)controller;
        while (contr->event_ring->has_event()) {
            EventTRB ev = contr->event_ring->pop();
            uint8_t type = (ev.control >> 10) & 0x3F;
            uart_print("\n");
            if (type == 34) {
                contr->port_status_change(ev);
                continue;
            }
            else if (type == 33) {
                contr->command_completion(ev);
            }
            else if (type == 32) {
                uint8_t slot_id = (uint8_t)(ev.control >> 24);         // 상위 8비트
                uint8_t endpoint_id = (uint8_t)((ev.control >> 16) & 0x1F); // 16~20비트
                for (int i = 0; i < xhci_event->size(); i++) {
                    KEvent event = (*xhci_event)[i];
                    if (event.arg[0] == ev.ptr && event.arg[1] == slot_id && event.arg[2] == endpoint_id) {
                        event.callback(&event, ev.status, ev.control, event.callback_ctx);
                        xhci_event->erase(i);
                        i--;
                    }
                }
            }
        }
        contr->eoi();
    }
}