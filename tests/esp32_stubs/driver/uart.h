// SPDX-License-Identifier: MIT
#ifndef LIGHTYAK_MOCK_UART_H
#define LIGHTYAK_MOCK_UART_H
#include <stdint.h>
#include "freertos/FreeRTOS.h"
typedef int esp_err_t;
static const esp_err_t ESP_OK = 0;
static const esp_err_t ESP_FAIL = -1;
static const esp_err_t ESP_ERR_INVALID_ARG = 0x102;
static const esp_err_t ESP_ERR_INVALID_STATE = 0x103;
enum uart_port_t { UART_NUM_0, UART_NUM_1, UART_NUM_2 };
enum uart_word_length_t { UART_DATA_5_BITS, UART_DATA_6_BITS, UART_DATA_7_BITS, UART_DATA_8_BITS };
enum uart_parity_t { UART_PARITY_DISABLE=0, UART_PARITY_EVEN=2, UART_PARITY_ODD=3 };
enum uart_stop_bits_t { UART_STOP_BITS_1=1, UART_STOP_BITS_1_5=2, UART_STOP_BITS_2=3 };
static const int UART_HW_FLOWCTRL_DISABLE=0, UART_SCLK_DEFAULT=0, UART_PIN_NO_CHANGE=-1;
struct uart_config_t { int baud_rate;uart_word_length_t data_bits;uart_parity_t parity;uart_stop_bits_t stop_bits;int flow_ctrl;int source_clk; };
bool uart_is_driver_installed(uart_port_t);
esp_err_t uart_driver_install(uart_port_t,int,int,int,void*,int);
esp_err_t uart_driver_delete(uart_port_t);
esp_err_t uart_param_config(uart_port_t,const uart_config_t*);
esp_err_t uart_set_pin(uart_port_t,int,int,int,int);
esp_err_t uart_set_rx_full_threshold(uart_port_t,int);
esp_err_t uart_set_rx_timeout(uart_port_t,int);
int uart_read_bytes(uart_port_t,void*,uint32_t,TickType_t);
// Deliberately no uart_write_bytes/uart_tx_chars declarations: using a blocking
// convenience TX API is a compile-time failure in the production adapter tests.
#endif
