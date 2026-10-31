#include "debug/log.h"
#include "arch/io.h"
#include "kernel/kernel.h"
#include "kernel/console.h"
//#define LOG_TO_FRAMEBUFFER
//#define LOG_SCROLL
#define COM1 0x3F8
#define LOG_CELL_W 8
#define LOG_CELL_H 20
#define LOG_MARGIN 4
#define LOG_MAX_COLS 256
#define LOG_MAX_ROWS 128
#define LOG_SCROLL_LINES 8
#define LOG_BG 0xFFFFFF

static char log_buf[LOG_MAX_ROWS][LOG_MAX_COLS];
static uint32_t log_cols = 0;
static uint32_t log_rows = 0;
static uint32_t cur_col = 0;
static uint32_t cur_row = 0;

static void log_fill_bg() {
    volatile uint32_t* fb = (volatile uint32_t*)bootinfo->framebufferAddr;
    uint64_t n = (uint64_t)bootinfo->framebufferPitch * bootinfo->framebufferHeight;
    for (uint64_t i = 0; i < n; i++) fb[i] = LOG_BG;
}

static void log_redraw() {
    log_fill_bg();
    for (uint32_t r = 0; r < log_rows; r++) {
        for (uint32_t c = 0; c < log_cols; c++) {
            char ch = log_buf[r][c];
            if (ch == ' ' || ch == 0) continue;
            putc(bootinfo, LOG_MARGIN + c * LOG_CELL_W, LOG_MARGIN + r * LOG_CELL_H, ch, 0, 1);
        }
    }
}

static void log_scroll() {
    uint32_t n = LOG_SCROLL_LINES;
    if (n > log_rows) n = log_rows;
    for (uint32_t r = 0; r + n < log_rows; r++) {
        for (uint32_t c = 0; c < log_cols; c++) log_buf[r][c] = log_buf[r + n][c];
    }
    for (uint32_t r = log_rows - n; r < log_rows; r++) {
        for (uint32_t c = 0; c < log_cols; c++) log_buf[r][c] = ' ';
    }
    log_redraw();
    cur_row = log_rows - n;
}

static void log_newline() {
    cur_col = 0;
    cur_row++;
    if (cur_row >= log_rows) {
#ifdef LOG_SCROLL
        log_scroll();
#else
        cur_row = log_rows - 1;
#endif
    }
}

void uart_init() {
    outb(COM1 + 1, 0x00);    // Disable interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB
    outb(COM1 + 0, 0x03);    // Baud divisor (38400 baud)
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop
    outb(COM1 + 2, 0xC7);    // Enable FIFO
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set

    log_cols = (bootinfo->framebufferWidth - LOG_MARGIN * 2) / LOG_CELL_W;
    log_rows = (bootinfo->framebufferHeight - LOG_MARGIN * 2) / LOG_CELL_H;
    if (log_cols > LOG_MAX_COLS) log_cols = LOG_MAX_COLS;
    if (log_rows > LOG_MAX_ROWS) log_rows = LOG_MAX_ROWS;
    if (log_cols == 0) log_cols = 1;
    if (log_rows == 0) log_rows = 1;
    for (uint32_t r = 0; r < log_rows; r++) {
        for (uint32_t c = 0; c < log_cols; c++) log_buf[r][c] = ' ';
    }
    cur_col = 0;
    cur_row = 0;
    log_fill_bg();
}

int uart_is_transmit_empty() {
    return inb(COM1 + 5) & 0x20;
}

void uart_putc(char c) {
    while (!uart_is_transmit_empty());
    outb(COM1, c);
#ifdef LOG_TO_FRAMEBUFFER
    if (log_cols == 0) return;
    if (c == '\r') { cur_col = 0; return; }
    if (c == '\n') { log_newline(); return; }
    if (c < 32 || c > 126) return;
    if (cur_col >= log_cols) log_newline();
    log_buf[cur_row][cur_col] = c;
    putc(bootinfo, LOG_MARGIN + cur_col * LOG_CELL_W, LOG_MARGIN + cur_row * LOG_CELL_H, c, 0, 1);
    cur_col++;
#endif
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