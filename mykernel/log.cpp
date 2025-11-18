#include "log.h"
#include "io.h"
#define COM1 0x3F8
void uart_init() {
    outb(COM1 + 1, 0x00);    // Disable interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB
    outb(COM1 + 0, 0x03);    // Baud divisor (38400 baud)
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop
    outb(COM1 + 2, 0xC7);    // Enable FIFO
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int uart_is_transmit_empty() {
    return inb(COM1 + 5) & 0x20;
}

void uart_putc(char c) {
    while (!uart_is_transmit_empty());
    outb(COM1, c);
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