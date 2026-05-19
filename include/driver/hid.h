#ifndef __HID_H__
#define __HID_H__
#include "util/size.h"
#include "util/vector.h"
#define HID_QUEUE_SIZE 0x100
#define HID_QUEUE_BASE 0xFFFF860000000000ULL
void hid_callback(void* event_info, uint64_t status, uint64_t control, void* ctx);
enum HIDFunction : uint8_t {
    HID_FUNC_MOUSE = 0b0001,
    HID_FUNC_KEYBOARD = 0b0010,
    HID_FUNC_JOYSTICK = 0b0100,
    HID_FUNC_CONSUMER = 0b1000,
};
struct Usage {
	uint32_t usage_page;    //종류 (예: Generic Desktop, Button 등)
	uint32_t usage;         //세부 항목 (예: X, Y, Button 1 등)
	int32_t  logical_min;   //값의 최소 범위
	int32_t  logical_max;   //값의 최대 범위
	uint16_t bit_offset;    //보고서 내에서의 비트 위치
    uint8_t  report_size;
    bool     is_absolute;
};
struct Report {
	uint8_t  id;            // Report ID (0이면 ID 없음)
	uint32_t app_usage;     //응용 프로그램에서의 용도 (예: Mouse, Keyboard 등)
    vector<Usage> usages;   // 개별 Usage 목록
};
class HIDDevice {
public:
    uint8_t* hid_buf;
    uint16_t  max_packet_size;
    uint8_t state;
    vector<Report>      reports;
    bool has_report_id;
    uint8_t   functions;
    void hid_init(uint8_t* desc, uint16_t len); // Report Descriptor 파싱 후 init() 호출
    void hid_event(void* ei, uint64_t st, uint64_t ctrl); // 커서/클릭 처리 후 event() 호출
	void* operator new(size_t size);
	void operator delete(void* ptr);
protected:
    virtual void init() = 0;
    virtual void event(void* ei, uint64_t st, uint64_t ctrl) = 0;
private:
    bool is_keycode_unpressed(uint32_t keycode, vector<uint32_t>* now_keycodes);
    bool is_keycode_pressed(uint32_t keycode);
    vector<uint32_t> prev_keycodes;
};
#include "arch/xhci_c.h"

template<typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
class XHCIHIDDevice : public HIDDevice {
public:
    XHCIHIDDevice(XHCIDevice<InputContext, DeviceContext, SlotContext, EndpointContext>* dev, uint8_t dci);
protected:
    void init() override;
    void event(void* ei, uint64_t st, uint64_t ctrl) override;
private:
    XHCIDevice<InputContext, DeviceContext, SlotContext, EndpointContext>* xhci_dev;
    XHCIRing* ring;
    uint8_t   ring_buff[sizeof(XHCIRing)];
    uint8_t   dci;
    uint64_t  _queue_trb();
	void      _push_kevent(uint64_t trb_phys);
};
static const char hid_keycode_table[] = {
    //  0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F
        0,    0,    0,    0,   'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
   'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '1', '2',    // 0x10
       '3', '4', '5', '6', '7', '8', '9', '0', '\n', 0,  '\b', '\t', ' ', '-', '=', '[',   // 0x20
       ']', '\\', 0,  ';', '\'', '`', ',', '.', '/',  0,    0,    0,    0,    0,    0,   0,  // 0x30
};
#endif /* __HID_H__ */