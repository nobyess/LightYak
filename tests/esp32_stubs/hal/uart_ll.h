// SPDX-License-Identifier: MIT
#ifndef LIGHTYAK_MOCK_UART_LL_H
#define LIGHTYAK_MOCK_UART_LL_H
#include "Esp32Mock.h"
struct uart_dev_t { int number; };
extern uart_dev_t mock_uart_devices[3];
#define UART_LL_GET_HW(number) (&mock_uart_devices[number])
uint32_t uart_ll_get_txfifo_len(uart_dev_t*);
void uart_ll_write_txfifo(uart_dev_t*,const uint8_t*,uint32_t);
#endif
