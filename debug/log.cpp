#include "debug/log.h"
#include "arch/io.h"
#include "kernel/kernel.h"
#include "kernel/console.h"
#define COM1 0x3F8
static uint8_t console[200 * 80] = { 0, }; // 디버깅용 콘솔 버퍼
uint64_t cursor_pos = 0;
void uart_init() {
    outb(COM1 + 1, 0x00);    // Disable interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB
    outb(COM1 + 0, 0x03);    // Baud divisor (38400 baud)
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop
    outb(COM1 + 2, 0xC7);    // Enable FIFO
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    for (unsigned int i = 0; i < bootinfo->framebufferPitch * bootinfo->framebufferHeight; i++) {
        uint32_t PixelColor = 0xFFFFFF;
        *((uint32_t*)(bootinfo->framebufferAddr) + i) = PixelColor;
    }
}

int uart_is_transmit_empty() {
    return inb(COM1 + 5) & 0x20;
}

void uart_putc(char c) {
    while (!uart_is_transmit_empty());
    outb(COM1, c);
	//console[cursor_pos++ % (200 * 80)] = c; // 콘솔 버퍼에 저장
    //for (unsigned int i = 0; i < bootinfo->framebufferPitch * bootinfo->framebufferHeight; i++) {
    //    uint32_t PixelColor = 0xFFFFFF;
    //    *((uint32_t*)(bootinfo->framebufferAddr) + i) = PixelColor;
    //}
    if (c == '\r') {
        cursor_pos -= (cursor_pos % 200); // 커서를 현재 줄의 시작으로 이동
    }
    else if (c == '\n') {
        cursor_pos += (200 - (cursor_pos % 200)); // 커서를 다음 줄의 시작으로 이동
    }
    putc(bootinfo, (cursor_pos % 200) * 1 * 8 + 4, (cursor_pos / 200) * 2 * 10 + 4, c, 0, 1);
    cursor_pos++;
	if ((cursor_pos / 200) * 20 + 14 > bootinfo->framebufferHeight) {
		for (uint64_t y = 0; y < bootinfo->framebufferHeight - 20; y++) {
			for (uint64_t x = 0; x < bootinfo->framebufferPitch; x++) {
				((uint32_t*)(bootinfo->framebufferAddr))[y * bootinfo->framebufferPitch + x] =
					((uint32_t*)(bootinfo->framebufferAddr))[(y + 20) * bootinfo->framebufferPitch + x];
			}
		}
		for (uint64_t y = bootinfo->framebufferHeight - 20; y < bootinfo->framebufferHeight; y++) {
			for (uint64_t x = 0; x < bootinfo->framebufferPitch; x++) {
				((uint32_t*)(bootinfo->framebufferAddr))[y * bootinfo->framebufferPitch + x] = 0xFFFFFF;
			}
		}
		cursor_pos = (bootinfo->framebufferHeight / 20 - 2) * 200; // 커서를 마지막 줄의 시작으로 이동
    }
}

void uart_print(const char* s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_print(int n) {
    if (n < 0) {
        uart_putc('-');
        n = -n;
    }
    if (n == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i--) {
        uart_putc(buf[i]);
    }
}
void uart_print(unsigned int n) {
    if (n == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i--) {
        uart_putc(buf[i]);
    }
}
void uart_print(uint64_t n) {
    if (n == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i--) {
        uart_putc(buf[i]);
    }
}
void uart_print_hex(uint64_t n) {
	if (n == 0) {
        uart_putc('0');
        return;
    }
    const char* hex_digits = "0123456789ABCDEF";
    char buf[16];
    int i = 0;
    while (n > 0) {
        buf[i++] = hex_digits[n % 16];
        n /= 16;
    }
    while (i--) {
        uart_putc(buf[i]);
    }
}
void uart_print_hex2(uint8_t n) {
    const char* hex_digits = "0123456789ABCDEF";
    uart_putc(hex_digits[n >> 4]);
    uart_putc(hex_digits[n & 0xF]);
}