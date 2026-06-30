#include "kernel/keyboard.h"
#include "kernel/process.h"
#include "kernel/power.h"
#include "arch/lapic.h"

static const uint64_t REPEAT_DELAY_MS    = 400;
static const uint64_t REPEAT_INTERVAL_MS = 50;

uint8_t  keystate[512]      = { 0, };
uint64_t keystate_time[512] = { 0, };
uint32_t repeat_key         = 0;

void key_press(uint32_t key) {
	if (key == KEY_ESC) {
		shutdown();
	}
	if (key < sizeof(keystate)) {
		if (!(keystate[key] & 0b1)) {
			(GetProcess(0))->msg_recv({ (-1ull), MSG_KEY_PRESS,0,{(uint64_t)key,0ull,0ull} }, false);
			keystate[key] |= 0b1;
			repeat_key = key;
			keystate_time[key] = tsc_get() + ms_to_ticks(REPEAT_DELAY_MS);
		}
	}
}
void key_held(uint32_t key) {
	if (key != repeat_key) return;
	if (key < sizeof(keystate)) {
		if ((keystate[key] & 0b1) && tsc_get() >= keystate_time[key]) {
			(GetProcess(0))->msg_recv({ (-1ull), MSG_KEY_PRESS,0,{(uint64_t)key,0ull,0ull} }, false);
			keystate_time[key] = tsc_get() + ms_to_ticks(REPEAT_INTERVAL_MS);
		}
	}
}
void key_release(uint32_t key) {
	if (key < sizeof(keystate)) {
		if (keystate[key] & 0b1) {
			(GetProcess(0))->msg_recv({ (-1ull), MSG_KEY_RELEASE,0,{(uint64_t)key,0ull,0ull} }, false);
			keystate[key] &= ~0b1;
			if (repeat_key == key) repeat_key = 0;
		}
	}
}
uint8_t get_key_state(uint32_t key) {
	if (key < sizeof(keystate)) {
		return keystate[key];
	}
	return 0;
}