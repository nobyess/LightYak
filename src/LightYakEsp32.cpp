// SPDX-License-Identifier: MIT
#include "LightYak.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <limits.h>
#include <esp_idf_version.h>
#include <soc/soc_caps.h>
#include <hal/uart_ll.h>

#if ESP_IDF_VERSION_MAJOR < 5
#error "LightYak ESP32 support requires Arduino-ESP32 3.x (ESP-IDF 5.x or newer)"
#endif

namespace lightyak {
Esp32Uart::Esp32Uart(int number, int rxPin, int txPin, size_t rxBufferBytes)
  : number_(number), rxPin_(rxPin), txPin_(txPin), rxBufferBytes_(rxBufferBytes),
    started_(false), error_(ESP_ERR_INVALID_STATE) {}

Esp32Uart::~Esp32Uart() { End(); }

bool Esp32Uart::Begin(unsigned long baud, uint32_t config) {
  // Validate before touching any peripheral; preserve the FULL ESP32 config.
  const uint32_t allowed = 0x0800003fUL;
  const uint32_t parity = config & 3;
  const uint32_t stop = (config >> 4) & 3;
  if (number_ < 0 || number_ >= SOC_UART_HP_NUM || rxPin_ < 0 || txPin_ < 0 ||
      rxPin_ == txPin_ || !baud || baud > INT_MAX ||
      rxBufferBytes_ < 256 || rxBufferBytes_ > INT_MAX ||
      (config & ~allowed) || !(config & 0x08000000UL) ||
      ((config >> 2) & 3) != UART_DATA_8_BITS ||
      (parity != UART_PARITY_DISABLE && parity != UART_PARITY_EVEN && parity != UART_PARITY_ODD) ||
      (stop != UART_STOP_BITS_1 && stop != UART_STOP_BITS_2)) {
    error_ = ESP_ERR_INVALID_ARG; return false;
  }
  const uart_port_t port = static_cast<uart_port_t>(number_);
  if (started_ || uart_is_driver_installed(port)) {
    error_ = ESP_ERR_INVALID_STATE; return false;
  }
  // All driver allocations happen here, not in Update(). No event task/queue
  // and no TX ring/ISR; exclusive FIFO writes below never wait on tx_mux.
  error_ = uart_driver_install(port, static_cast<int>(rxBufferBytes_), 0, 0, 0, 0);
  if (error_ != ESP_OK) return false;
  uart_config_t settings = {};
  settings.baud_rate = static_cast<int>(baud);
  settings.data_bits = UART_DATA_8_BITS;
  settings.parity = static_cast<uart_parity_t>(parity);
  settings.stop_bits = static_cast<uart_stop_bits_t>(stop);
  settings.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  settings.source_clk = UART_SCLK_DEFAULT;
  error_ = uart_param_config(port, &settings);
  if (error_ == ESP_OK) error_ = uart_set_pin(port, txPin_, rxPin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (error_ == ESP_OK) error_ = uart_set_rx_full_threshold(port, 8);
  if (error_ == ESP_OK) error_ = uart_set_rx_timeout(port, 2);
  if (error_ != ESP_OK) { uart_driver_delete(port); return false; }
  started_ = true;
  return true;
}

void Esp32Uart::End() {
  if (started_) {
    uart_driver_delete(static_cast<uart_port_t>(number_));
    started_ = false;
  }
}

int Esp32Uart::read(void* context) {
  Esp32Uart& self = *static_cast<Esp32Uart*>(context);
  if (!self.started_) return -1;
  uint8_t value;
  // One byte, zero RTOS ticks: neither the RX mutex nor data are waited for.
  const int count = uart_read_bytes(static_cast<uart_port_t>(self.number_), &value, 1, 0);
  return count == 1 ? value : -1;
}

bool Esp32Uart::write(void* context, uint8_t value) {
  Esp32Uart& self = *static_cast<Esp32Uart*>(context);
  if (!self.started_) return false;
  // Do NOT use HardwareSerial::write(), uart_write_bytes() or uart_tx_chars():
  // they take blocking TX mutexes. With no TX ring/ISR and no other writer,
  // the FIFO can only gain free space between this test and this one-byte write.
  uart_dev_t* hardware = UART_LL_GET_HW(self.number_);
  if (uart_ll_get_txfifo_len(hardware) == 0) return false;
  uart_ll_write_txfifo(hardware, &value, 1);
  return true;
}

uint32_t Esp32Uart::clock(void*) { return ::millis(); }

Transport Esp32Uart::Adapter() {
  if (!started_) return Transport{0,0,0,0};
  return Transport{this, read, write, clock};
}
} // namespace lightyak
#endif
