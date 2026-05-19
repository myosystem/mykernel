#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__
#include "util/size.h"
enum {
	KEY_LSHIFT = 0x101,
	KEY_RSHIFT = 0x102,
	KEY_LCTRL = 0x103,
	KEY_RCTRL = 0x104,
	KEY_LALT = 0x105,
	KEY_RALT = 0x106,

	KEY_RIGHT = 0x110,
	KEY_LEFT = 0x111,
	KEY_DOWN = 0x112,
	KEY_UP = 0x113,

	KEY_ESC = 0x120,

};
extern uint8_t keystate[512];
void key_press(uint32_t key);
void key_release(uint32_t key);
uint8_t get_key_state(uint32_t key);
#endif // __KEYBOARD_H__