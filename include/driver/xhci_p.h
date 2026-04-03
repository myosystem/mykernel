#ifndef __XHCI_P_H__
#define __XHCI_P_H__
#include "driver/protocol.h"
#include "arch/xhci_c.h"
enum class Direction { In, Out, None };
class XHCIProtocol : public Protocol {
public:
    virtual ~XHCIProtocol() {}

    // 실제 디스크가 사용하게 될 핵심 통로
    virtual bool execute_transaction(void* cmd, size_t clen, void* data, size_t dlen, Direction dir) = 0;

    // 장치 초기화 및 에러 복구
    virtual bool initialize() = 0;
    virtual void reset() = 0;
};

struct InputControlContext {
    uint32_t drop_context_flags;
    uint32_t add_context_flags;   // Bit 0: Slot, Bit 1: EP0, Bit 2~31: EP1~30
    uint32_t reserved[5];
    uint8_t  configuration_value;
    uint8_t  interface_number;
    uint8_t  alternate_setting;
    uint8_t  reserved2;
} __attribute__((packed));

template <typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
class XHCI_BOT_Protocol : public XHCIProtocol {
private:
    XHCIDevice<InputContext, DeviceContext, SlotContext, EndpointContext>* device;    // 부모 xHCI 장치 참조
    XHCIRing* bulk_in;
    XHCIRing* bulk_out;
	uint8_t bulk_in_buff[sizeof(XHCIRing)];
    uint8_t bulk_out_buff[sizeof(XHCIRing)];
    uint8_t slot_id;
    uint8_t dci_in;
    uint8_t dci_out;
    CommandBlockWrapper* cbw;
    CommandStatusWrapper* csw;

public:
    XHCI_BOT_Protocol(XHCIDevice<InputContext, DeviceContext, SlotContext, EndpointContext>* dev,
        uint8_t slot, uint8_t in_dci, uint8_t out_dci)
        : device(dev), slot_id(slot), dci_in(in_dci), dci_out(out_dci),
        bulk_in(nullptr), bulk_out(nullptr), cbw(nullptr), csw(nullptr) { }

    // 드디어 구현하게 될 핵심 로직
    virtual bool execute_transaction(void* cmd, size_t clen, void* data, size_t dlen, Direction dir) override;

    virtual bool initialize() override;

    virtual void reset() override {
        // 에러 발생 시 파이프 청소
    }
};
class XHCI_UASP_Protocol : public XHCIProtocol {

};
#endif // __XHCI_P_H__