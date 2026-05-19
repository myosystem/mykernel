// driver/hid.cpp
#include "driver/hid.h"
#include "kernel/process.h"
#include "kernel/kernel.h"
#include "util/util.h"
#include "kernel/keyboard.h"

extern int cursor_x, cursor_y;
extern bool booting;
extern uint8_t mouse_state;

void hid_callback(void* ei, uint64_t st, uint64_t ctrl, void* ctx) {
    HIDDevice* hid = (HIDDevice*)ctx;
    hid->hid_event(ei, st, ctrl);
}
struct HIDGlobalState {
    uint32_t usage_page;
    int32_t  logical_min;
    int32_t  logical_max;
    uint8_t  report_size;
    uint8_t  report_count;
    uint8_t  report_id;
};

struct HIDLocalState {
    uint32_t usage_stack[16];
    uint8_t  usage_count;
    uint32_t usage_min;
    uint32_t usage_max;

    void reset() {
        usage_count = 0;
        memset(usage_stack, 0, sizeof(usage_stack)); // 배열 전체를 0으로 밀어버림
        usage_min = usage_max = 0;
    }
};

struct HIDCollection {
    uint32_t usage_page;
    uint32_t usage;
    uint8_t  type;
};
void HIDDevice::hid_init(uint8_t* desc, uint16_t len) {
    HIDGlobalState global = { 0, };
    HIDLocalState  local = { 0, };
    vector<HIDCollection> col_stack;
    uint32_t cur_app_usage = 0;
    uint16_t bit_offset = 0;
    functions = 0;
    has_report_id = false;

    // Report ID 없는 경우 기본 Report
    reports.push_back({ 0, 0, {} });
    Report* cur_report = &reports[0];

    uint8_t* ptr = desc;
    uint8_t* end = desc + len;

    while (ptr < end) {
        uint8_t prefix = *ptr++;
        uint8_t size = prefix & 0x03;
        uint8_t tag = prefix & 0xFC;

        uint32_t val = 0; // int32_t 대신 uint32_t 사용 권장
        for (int i = 0; i < size; i++)
            val |= ((uint32_t)*ptr++ << (i * 8));

        // Usage 관련 태그(0x08, 0x18, 0x28)는 부호 확장을 하지 않도록 분기 처리하거나,
        // Logical Minimum/Maximum처럼 부호가 필요한 경우에만 아래 로직을 적용하세요.
        if (tag != 0x08 && tag != 0x18 && tag != 0x28) {
            if (size == 1 && (val & 0x80)) val |= 0xFFFFFF00;
            else if (size == 2 && (val & 0x8000)) val |= 0xFFFF0000;
        }

        switch (tag) {
            // Global
        case 0x04: global.usage_page = (uint32_t)val; break;
        case 0x14: global.logical_min = val;            break;
        case 0x24: global.logical_max = val;            break;
        case 0x74: global.report_size = (uint8_t)val;  break;
        case 0x94: global.report_count = (uint8_t)val;  break;
        case 0x84: {
            has_report_id = true;
            global.report_id = (uint8_t)val;
            cur_report = nullptr;
            for (int i = 0; i < reports.size(); i++)
                if (reports[i].id == global.report_id) { cur_report = &reports[i]; break; }
            if (!cur_report) {
                reports.push_back({ global.report_id, cur_app_usage, {} });
                cur_report = &reports[reports.size() - 1];
                bit_offset = 0;
            }
            break;
        }

                 // Local
        case 0x08:
            if (local.usage_count < 16)
                local.usage_stack[local.usage_count++] = (uint32_t)val;
            break;
        case 0x18: local.usage_min = (uint32_t)val; break;
        case 0x28: local.usage_max = (uint32_t)val; break;

            // Main
        case 0xA0: { // Collection
            uint32_t usage = local.usage_count > 0 ? local.usage_stack[0] : 0;
            uint8_t  type = (uint8_t)val;
            col_stack.push_back({ global.usage_page, usage, type });
            if (type == 1) { // Application
                cur_app_usage = usage;
                if (global.usage_page == 0x01) {
                    if (usage == 0x02) functions |= HID_FUNC_MOUSE;
                    if (usage == 0x06) functions |= HID_FUNC_KEYBOARD;
                    if (usage == 0x04) functions |= HID_FUNC_JOYSTICK;
                }
                if (global.usage_page == 0x0C) functions |= HID_FUNC_CONSUMER;
                if (cur_report) cur_report->app_usage = cur_app_usage;
            }
            local.reset();
            break;
        }
        case 0xC0: // End Collection
            if (col_stack.size()) col_stack.erase(col_stack.size() - 1);
            cur_app_usage = 0;
            for (int i = col_stack.size() - 1; i >= 0; i--) {
                if (col_stack[i].type == 1) {
                    cur_app_usage = col_stack[i].usage;
                    break;
                }
            }
            break;

        case 0x80: { // Input
            bool is_constant = (val & 0x01);
            bool is_absolute = !(val & 0x04);
            if (!is_constant && cur_report) {
                for (int i = 0; i < global.report_count; i++) {
                    Usage u = { 0, };
                    u.usage_page = global.usage_page;
                    u.usage = (local.usage_count > 0)
                        ? (i < local.usage_count
                            ? local.usage_stack[i]
                            : local.usage_stack[local.usage_count - 1])
                        : local.usage_min + i;
                    u.logical_min = global.logical_min;
                    u.logical_max = global.logical_max;
                    u.report_size = global.report_size;
                    u.bit_offset = bit_offset + (global.report_size * i);
                    u.is_absolute = is_absolute;
                    cur_report->usages.push_back(u);
                }
            }
            bit_offset += global.report_size * global.report_count;
            local.reset();
            break;
        }
        }
    }

    // Report ID 있으면 처음에 만든 기본 Report 제거
    if (has_report_id && reports[0].id == 0 && reports[0].usages.size() == 0)
        reports.erase(0);

    init();
    for (int j = 0; j < reports.size(); j++) {
        Report* report = &reports[j];
        for (int i = 0; i < report->usages.size(); i++) {
            Usage& u = report->usages[i];
            uart_print("usage_page="); uart_print_hex(u.usage_page);
            uart_print(" usage="); uart_print_hex(u.usage);
            uart_print(" bit_offset="); uart_print_hex(u.bit_offset);
            uart_print(" report_size="); uart_print_hex(u.report_size);
            uart_print(" logical_max="); uart_print_hex(u.logical_max);
            uart_print("\n");
        }
    }
}

uint32_t extract_bits(uint8_t* buf, uint16_t bit_offset, uint8_t size) {
    uint32_t val = 0;
    for (int i = 0; i < size; i++) {
        uint16_t byte_idx = (bit_offset + i) / 8;
        uint8_t  bit_idx = (bit_offset + i) % 8;
        if (buf[byte_idx] & (1 << bit_idx))
            val |= (1 << i);
    }
    return val;
}
uint32_t hid_keycode_to_ascii(uint32_t keycode) {
    switch (keycode) {
    case 0x4F: return KEY_RIGHT;
    case 0x50: return KEY_LEFT;
    case 0x51: return KEY_DOWN;
    case 0x52: return KEY_UP;

    case 0x29: return KEY_ESC;
    default: break;
    }
    if (keycode >= sizeof(hid_keycode_table)) return 0;
    return hid_keycode_table[keycode];
}
bool HIDDevice::is_keycode_unpressed(uint32_t keycode, vector<uint32_t>* now_keycodes) {
    for (int i = 0; i < now_keycodes->size(); i++) {
        if ((*now_keycodes)[i] == keycode) return false;
    }
    return true;
}
bool HIDDevice::is_keycode_pressed(uint32_t keycode) {
    for (int i = 0; i < prev_keycodes.size(); i++) {
        if (prev_keycodes[i] == keycode) return false;
    }
    return true;
}
void HIDDevice::hid_event(void* ei, uint64_t st, uint64_t ctrl) {
    uint8_t* buf = hid_buf;

    // Report ID 확인
    uint8_t report_id = has_report_id ? buf[0] : 0;
    uint8_t* data = has_report_id ? buf + 1 : buf;

    // 해당 Report 찾기
    Report* report = nullptr;
    for (int i = 0; i < reports.size(); i++) {
        if (reports[i].id == report_id) { report = &reports[i]; break; }
    }
    if (!report) return;

    // Mouse 처리
    if (functions & HID_FUNC_MOUSE) {
        int32_t dx = 0, dy = 0;
        bool    is_abs = false;
        int32_t abs_x = 0, abs_y = 0;
        int32_t abs_max_x = 0x7FFF, abs_max_y = 0x7FFF;
        uint8_t buttons = 0;
        for (int i = 0; i < report->usages.size(); i++) {
            Usage u = report->usages[i];
            uint32_t val = extract_bits(data, u.bit_offset, u.report_size);

            // sign extension
            if (!u.is_absolute && u.logical_min < 0 && (val & (1 << (u.report_size - 1))))
                val |= ~((1u << u.report_size) - 1);
            int32_t sval = (int32_t)val;

            if (u.usage_page == 0x09) { // Button
                uint8_t btn_idx = u.usage - 1; // Button1=0, Button2=1, ...
                if (btn_idx < 8 && sval) buttons |= (1 << btn_idx);
            }
            else if (u.usage_page == 0x01) {
                if (u.usage == 0x30) { // X
                    if (u.is_absolute) { is_abs = true; abs_x = sval; abs_max_x = u.logical_max; }
                    else dx = sval;
                }
                else if (u.usage == 0x31) { // Y
                    if (u.is_absolute) { abs_y = sval; abs_max_y = u.logical_max; }
                    else dy = sval;
                }
            }
        }

        // 커서 업데이트
        if (is_abs) {
            cursor_x = (int)((int64_t)abs_x * bootinfo->framebufferWidth / abs_max_x);
            cursor_y = (int)((int64_t)abs_y * bootinfo->framebufferHeight / abs_max_y);
			//uart_print("Mouse Event: abs_x="); uart_print_hex(abs_x); uart_print(" abs_y="); uart_print_hex(abs_y); uart_print("\n");
        }
        else {
            cursor_x -= dx;
            cursor_y -= dy;
            //uart_print("Mouse Event: dx="); uart_print_hex(dx); uart_print(" dy="); uart_print_hex(dy); uart_print("\n");
        }

        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x >= (int)bootinfo->framebufferWidth)  cursor_x = bootinfo->framebufferWidth - 1;
        if (cursor_y >= (int)bootinfo->framebufferHeight) cursor_y = bootinfo->framebufferHeight - 1;

        // 클릭 처리
        extern uint8_t mouse_state;
        uint8_t pressed = buttons & ~mouse_state;
        uint8_t released = mouse_state & ~buttons;
        mouse_state = buttons;

        extern bool booting;
        if (!booting) {
            if (is_abs || dx != 0 || dy != 0)
                ((Process*)PROCESS_QUEUE_BASE)->msg_recv({ (-1ull), MSG_MOUSE_MOVE,     0, {(uint64_t)cursor_x, (uint64_t)cursor_y, 0} }, false);
            if (pressed & 0b001) ((Process*)PROCESS_QUEUE_BASE)->msg_recv({ (-1ull), MSG_MOUSE_LCLICK,   0, {(uint64_t)cursor_x, (uint64_t)cursor_y, 0} }, false);
            if (pressed & 0b010) ((Process*)PROCESS_QUEUE_BASE)->msg_recv({ (-1ull), MSG_MOUSE_RCLICK,   0, {(uint64_t)cursor_x, (uint64_t)cursor_y, 0} }, false);
            if (released & 0b001) ((Process*)PROCESS_QUEUE_BASE)->msg_recv({ (-1ull), MSG_MOUSE_LRELEASE, 0, {(uint64_t)cursor_x, (uint64_t)cursor_y, 0} }, false);
            if (released & 0b010) ((Process*)PROCESS_QUEUE_BASE)->msg_recv({ (-1ull), MSG_MOUSE_RRELEASE, 0, {(uint64_t)cursor_x, (uint64_t)cursor_y, 0} }, false);
        }
    }

    if (functions & HID_FUNC_KEYBOARD) {
        bool lshift = false;
        bool rshift = false;
        bool lctrl = false;
        bool rctrl = false;
        bool lalt = false;
        bool ralt = false;
		vector<uint32_t> now_keycodes;
        for (int i = 0; i < report->usages.size(); i++) {
            Usage& u = report->usages[i];
            uint32_t val = extract_bits(data, u.bit_offset, u.report_size);
            if (!val) continue;

            if (u.usage_page == 0x07) {
                // Modifier
                if (u.usage >= 0xE0 && u.usage <= 0xE7) {
                    switch (u.usage) {
					case 0xE0: lctrl = val & 0x01; break; // Left Ctrl
					case 0xE1: lshift = val & 0x01; break; // Left Shift
					case 0xE2: lalt = val & 0x01; break; // Left Alt
					case 0xE4: rctrl = val & 0x01; break; // Right Ctrl
					case 0xE5: rshift = val & 0x01; break; // Right Shift
					case 0xE6: ralt = val & 0x01; break; // Right Alt

                    default: break;
                    };
                }
                // Keycode
                else if (val != 0) {
					now_keycodes.push_back((uint32_t)val);
                    uint32_t c = hid_keycode_to_ascii(val);
                    if (c != 0 && !booting) {
                        /*
                        ((Process*)PROCESS_QUEUE_BASE)->msg_recv({
                            (-1ull), MSG_KEY_PRESS, 0, {(uint64_t)c, 0, 0}
                            });
                            */
                        if (is_keycode_pressed(val)) {
							key_press(c);
                        }
                    }
                }
            }
        }
        for (uint32_t i = 0; i < prev_keycodes.size(); i++) {
            if (prev_keycodes[i] != 0) {
                if (is_keycode_unpressed(prev_keycodes[i], &now_keycodes)) {
                    uint32_t c = hid_keycode_to_ascii(prev_keycodes[i]);
                    if (c != 0 && !booting) {
						key_release(c);
                    }
                }
            }
        }
		if (lshift) key_press(KEY_LSHIFT); else key_release(KEY_LSHIFT);
		if (rshift) key_press(KEY_RSHIFT); else key_release(KEY_RSHIFT);
		if (lctrl) key_press(KEY_LCTRL); else key_release(KEY_LCTRL);
		if (rctrl) key_press(KEY_RCTRL); else key_release(KEY_RCTRL);
		if (lalt) key_press(KEY_LALT); else key_release(KEY_LALT);
		if (ralt) key_press(KEY_RALT); else key_release(KEY_RALT);
        prev_keycodes = now_keycodes;
    }

    event(ei, st, ctrl);
}
void* HIDDevice::operator new(size_t size) {
    uint64_t mem = HID_QUEUE_BASE;
    uint64_t index = 0;
    while (((HIDDevice*)(mem))->state == 1) {
        mem += HID_QUEUE_SIZE;
        index++;
    }
    ((HIDDevice*)(mem))->state = 1;
    return (void*)mem;
}
void HIDDevice::operator delete(void* ptr) {
    HIDDevice* p = (HIDDevice*)ptr;
    p->state = 0;
}
template<typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
XHCIHIDDevice<InputContext, DeviceContext, SlotContext, EndpointContext>::XHCIHIDDevice(
    XHCIDevice<InputContext, DeviceContext, SlotContext, EndpointContext>* dev, uint8_t dci)
    : xhci_dev(dev), dci(dci), ring(nullptr) {}

template<typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
void XHCIHIDDevice<InputContext, DeviceContext, SlotContext, EndpointContext>::init() {
    InputContext* input_ctx = xhci_dev->input_ctx;
    DeviceContext* output_ctx = xhci_dev->output_ctx;

    // 1. Input Context 준비
    memset(input_ctx, 0, sizeof(InputContext));
    input_ctx->control.add_flags = (1 << 0) | (1 << dci);
    input_ctx->ctx.slot.info1 = output_ctx->slot.info1;
    input_ctx->ctx.slot.info2 = output_ctx->slot.info2;
    input_ctx->ctx.slot.state = output_ctx->slot.state;
    input_ctx->ctx.slot.info1 = (input_ctx->ctx.slot.info1 & ~(0x1F << 27)) | (dci << 27);

    // 2. Transfer Ring 생성
    ring = new (ring_buff) XHCIRing(64);

    // 3. Interval 변환
    uint8_t speed = (*xhci_dev->portsc >> 10) & 0x0F;
    uint8_t interval = xhci_dev->device_info.hid.interval;
    if (speed == 1 || speed == 2) {
        uint8_t converted = 0;
        while ((1u << converted) < interval) converted++;
        interval = converted;
    }
    if (interval > 0) interval--;
    
    // 4. EP Context 채우기
    input_ctx->ctx.endpoints[dci - 1].info2 = (7 << 3) | ((uint32_t)max_packet_size << 16);
    input_ctx->ctx.endpoints[dci - 1].info1 = (0x3 << 1) | ((uint32_t)interval << 16);
    input_ctx->ctx.endpoints[dci - 1].tr_ptr = ring->get_phys() | 1;
    input_ctx->ctx.endpoints[dci - 1].avg_len = 8;

    // 5. Configure Endpoint
    uint64_t input_phys = (uint64_t)input_ctx - MMIO_BASE;
    if (!xhci_dev->send_configure_endpoint_command(input_phys)) return;

    // 6. HID 버퍼 할당
    uint64_t buf_phys = phy_page_allocator->alloc_phy_page();
    virt_page_allocator->alloc_virt_page(buf_phys + MMIO_BASE, buf_phys,
        VirtPageAllocator::P | VirtPageAllocator::RW | VirtPageAllocator::PCD);
    memset((void*)(buf_phys + MMIO_BASE), 0, 4096);
    hid_buf = (uint8_t*)(buf_phys + MMIO_BASE);

    // 7. 첫 TRB 걸기
    uint64_t trb_phys = _queue_trb();

    // 8. KEvent 등록
    _push_kevent(trb_phys);
}

template<typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
void XHCIHIDDevice<InputContext, DeviceContext, SlotContext, EndpointContext>::event(void* ei, uint64_t st, uint64_t ctrl) {
    // erdp 업데이트
    XHCIController* controller = xhci_dev->controller;
    controller->event_ring->erdp(controller->intr_base);

    // 다음 TRB + KEvent 재등록
    uint64_t trb_phys = _queue_trb();
    _push_kevent(trb_phys);
}

// 중복 제거용 내부 헬퍼
template<typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
uint64_t XHCIHIDDevice<InputContext, DeviceContext, SlotContext, EndpointContext>::_queue_trb() {
    uint64_t buf_phys = (uint64_t)hid_buf - MMIO_BASE;
    TRB in_trb = { 0 };
    in_trb.parameter1 = (uint32_t)buf_phys;
    in_trb.parameter2 = (uint32_t)(buf_phys >> 32);
    in_trb.status = max_packet_size;
    in_trb.control = (1 << 10) | (1 << 5);
    uint64_t trb_phys = ring->push(in_trb);
    __asm__ __volatile__("sfence" ::: "memory");
    xhci_dev->controller->doorbell_base[xhci_dev->slot_id] = dci;
    return trb_phys;
}

template<typename InputContext, typename DeviceContext, typename SlotContext, typename EndpointContext>
void XHCIHIDDevice<InputContext, DeviceContext, SlotContext, EndpointContext>::_push_kevent(uint64_t trb_phys) {
    KEvent ev;
    ev.arg[0] = trb_phys;
    ev.arg[1] = xhci_dev->slot_id;
    ev.arg[2] = dci;
    ev.time = 0;
    ev.interval = 0;
    ev.type = 0x35;
    ev.callback = hid_callback;
    ev.callback_ctx = this;
    xhci_event->push_back(ev);
}

template class XHCIHIDDevice<InputContext32, DeviceContext32, SlotContext32, EndpointContext32>;
template class XHCIHIDDevice<InputContext64, DeviceContext64, SlotContext64, EndpointContext64>;