#include "driver/xhci_p.h"
#include "arch/xhci_c.h"
#include "util/util.h"

template <typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
bool XHCI_BOT_Protocol<InputContext, DeviceContext, SlotContext, EndpointContext>::execute_transaction(void* cmd, size_t clen, void* data_pa, size_t dlen, Direction dir) {
    // 1단계: CBW 생성 및 전송 (Bulk-OUT)
    uint64_t phys = (uint64_t)cbw - MMIO_BASE; // 가상 주소를 물리 주소로 변환
    cbw->signature = 0x43425355; // "USBC"
    cbw->tag = 0xDEADBEEF;       // 명령 식별용 (나중에 CSW랑 비교)
    cbw->data_transfer_len = dlen;
    cbw->flags = (dir == Direction::In) ? 0x80 : 0x00;
    cbw->command_len = clen;
    memcpy(cbw->command, cmd, clen);
    TRB trb;
    memset(&trb, 0, sizeof(TRB));

    trb.parameter1 = (uint32_t)(phys & 0xFFFFFFFF);
    trb.parameter2 = (uint32_t)(phys >> 32);

    // Status: 전송 길이 설정 (최대 64KB, 하지만 보통 512B~16KB 단위)
    trb.status = sizeof(*cbw) & 0x1FFFF;

    // Control: 
    // TRB Type = 1 (Normal)
    // IOC (Interrupt On Completion) = 1이면 이벤트 링에 보고함
    uint32_t ctrl = (1 << 10) | (1 << 5); // TRB Type 1

    trb.control = ctrl;
    TRB* bulk_out_phy = (TRB*)bulk_out->push(trb); // IOC=1
    device->controller->doorbell_base[slot_id] = dci_out;
    device->controller->wait_command(bulk_out_phy, slot_id, device->get_port_id(), 32, dci_out);

    // 2단계: Data 전송 (있을 경우만)
    if (dlen > 0) {
        if (dir == Direction::In) {
            TRB data_trb = {};
			data_trb.parameter1 = (uint32_t)((uint64_t)data_pa & 0xFFFFFFFF);
			data_trb.parameter2 = (uint32_t)(((uint64_t)data_pa >> 32) & 0xFFFFFFFF);
			data_trb.status = dlen & 0x1FFFF; // 최대 128KB까지 설정 가능
			data_trb.control = (1 << 10) | (1 << 5); // Type=1(Normal), IOC=1, DIR=In

            TRB* bulk_in_phy = (TRB*)bulk_in->push(data_trb);
            device->controller->doorbell_base[slot_id] = dci_in;
            device->controller->wait_command(bulk_in_phy, slot_id, device->get_port_id(), 32, dci_in);
        }
        else {
            TRB data_trb = {};
			data_trb.parameter1 = (uint32_t)((uint64_t)data_pa & 0xFFFFFFFF);
			data_trb.parameter2 = (uint32_t)(((uint64_t)data_pa >> 32) & 0xFFFFFFFF);
			data_trb.status = dlen & 0x1FFFF; // 최대 128KB까지 설정 가능
			data_trb.control = (1 << 10) | (1 << 5); // Type=1(Normal), IOC=1, DIR=Out

            bulk_out_phy = (TRB*)bulk_out->push(data_trb);
            device->controller->doorbell_base[slot_id] = dci_out;
            device->controller->wait_command(bulk_out_phy, slot_id, device->get_port_id(), 32, dci_out);
        }
    }

    // 3단계: CSW 수신 (Bulk-IN)
	trb.parameter1 = (uint32_t)((uint64_t)csw - MMIO_BASE);
	trb.parameter2 = (uint32_t)(((uint64_t)csw - MMIO_BASE) >> 32);
	trb.status = sizeof(*csw) & 0x1FFFF; // 최대 128KB까지 설정 가능
	trb.control = (1 << 10) | (1 << 5); // Type=1(Normal), IOC=1, DIR=In
    TRB* bulk_in_phy = (TRB*)bulk_in->push(trb);
    device->controller->doorbell_base[slot_id] = dci_in;
    device->controller->wait_command(bulk_in_phy, slot_id, device->get_port_id(), 32, dci_in); // Transfer Event 이벤트 대기 (Type 32)

    // CSW 서명과 상태 체크 (0 = Success)
    return (csw->signature == 0x53425355 && csw->status == 0);
}
template <typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
bool XHCI_BOT_Protocol<InputContext, DeviceContext, SlotContext, EndpointContext>::initialize() {
	bulk_in = new (bulk_in_buff) XHCIRing(256);
	bulk_out = new (bulk_out_buff) XHCIRing(256);
    // 1. 메모리 할당 및 구조체 포인터 맵핑
    uint64_t input_ctx_phys = phy_page_allocator->alloc_phy_page();
    auto* input_ctx = (InputContext*)(input_ctx_phys + MMIO_BASE);
    virt_page_allocator->alloc_virt_page((uint64_t)input_ctx, input_ctx_phys,
		VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    memset(input_ctx, 0, 4096);

    // CBW/CSW용 DMA 버퍼 (이건 공통)
    uint64_t dma_page_phys = phy_page_allocator->alloc_phy_page();
	virt_page_allocator->alloc_virt_page((uint64_t)(dma_page_phys + MMIO_BASE), dma_page_phys,
		VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    this->cbw = reinterpret_cast<CommandBlockWrapper*>(dma_page_phys + MMIO_BASE);
    this->csw = reinterpret_cast<CommandStatusWrapper*>(reinterpret_cast<uint8_t*>(this->cbw) + 512);

    // 2. Control Context 설정 (어떤 EP를 활성화할지)
    // 형님의 InputContext 구조체에는 'control' 멤버가 있습니다.
    input_ctx->control.add_flags = (1 << 0) | (1 << dci_in) | (1 << dci_out);

    // 3. Slot Context 설정
    // InputContext -> ctx -> slot 순서로 접근
    uint8_t max_dci = (dci_in > dci_out) ? dci_in : dci_out;
    input_ctx->ctx.slot.info1 = (max_dci << 27);
    // ※ 주의: SlotContext32의 info1 비트 레이아웃에 맞춤. 
    // 만약 64비트 구조체에서 필드명이 다르면 여기서 템플릿 특수화나 다른 처리가 필요할 수 있음.

    // 4. Bulk-IN Endpoint Context 설정
    // dci는 1(EP0)부터 시작하므로 배열 인덱스는 dci - 1
    auto& ep_in = input_ctx->ctx.endpoints[dci_in - 1];
    ep_in.info1 = (3 << 1);            // Error Count: 3
    ep_in.info2 = (6 << 3) | (512 << 16); // Type: Bulk IN(6), MPS: 512
    ep_in.tr_ptr = bulk_in->get_phys() | 1; // DCS=1

    // 5. Bulk-OUT Endpoint Context 설정
    auto& ep_out = input_ctx->ctx.endpoints[dci_out - 1];
    ep_out.info1 = (3 << 1);            // Error Count: 3
    ep_out.info2 = (2 << 3) | (512 << 16); // Type: Bulk OUT(2), MPS: 512
    ep_out.tr_ptr = bulk_out->get_phys() | 1;

    // 6. xHCI 명령 전송 (물리 주소 전달)
    bool result = device->send_configure_endpoint_command(input_ctx_phys);
	virt_page_allocator->free_virt_page((uint64_t)input_ctx);
	phy_page_allocator->put_page(input_ctx_phys);
    return result;
}
template class XHCI_BOT_Protocol<InputContext32, DeviceContext32, SlotContext32, EndpointContext32>;
template class XHCI_BOT_Protocol<InputContext64, DeviceContext64, SlotContext64, EndpointContext64>;