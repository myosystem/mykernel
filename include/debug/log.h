#ifndef __LOG_H__
#define __LOG_H__
#include "util/size.h"
void uart_init();

__attribute__((no_caller_saved_registers))
int uart_is_transmit_empty();

__attribute__((no_caller_saved_registers))
void uart_putc(char c);

__attribute__((no_caller_saved_registers))
void uart_print(const char* s);
__attribute__((no_caller_saved_registers))
void uart_print(int n);
__attribute__((no_caller_saved_registers))
void uart_print(uint64_t n);
__attribute__((no_caller_saved_registers))
void uart_print(unsigned int n);

__attribute__((no_caller_saved_registers))
void uart_print_hex(uint64_t n);

__attribute__((no_caller_saved_registers))
void uart_print_hex2(uint8_t n);

#endif // __LOG_H__