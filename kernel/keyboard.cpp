#include "kernel/keyboard.h"
#include "kernel/process.h"
#include "kernel/power.h"
uint8_t keystate[512] = { 0, };
void key_press(uint32_t key) {
	
	if (key == KEY_ESC) {
		shutdown();
	}
	
	if (key < sizeof(keystate)) {
		if (!(keystate[key] & 0b1)) {
			(GetProcess(0))->msg_recv({ (-1ull), MSG_KEY_PRESS,0,{(uint64_t)key,0ull,0ull} }, false);
			keystate[key] |= 0b1;
		}
	}
}
void key_release(uint32_t key) {
	if (key < sizeof(keystate)) {
		if (keystate[key] & 0b1) {
			(GetProcess(0))->msg_recv({ (-1ull), MSG_KEY_RELEASE,0,{(uint64_t)key,0ull,0ull} }, false);
			keystate[key] &= ~0b1;
		}
	}
}
uint8_t get_key_state(uint32_t key) {
	if (key < sizeof(keystate)) {
		return keystate[key];
	}
	return 0;
}