// SPDX-License-Identifier: MIT
#ifndef LIGHTYAK_ESP32_H
#define LIGHTYAK_ESP32_H

#if defined(ARDUINO_ARCH_ESP32)
#include <stdint.h>
#include <stddef.h>
#include <driver/uart.h>

namespace lightyak {
struct Transport;

// Exclusive hardware UART: no HardwareSerial, USB CDC, logging or other tasks
// may access this UART after Begin(). RX uses the IDF ring, TX the hardware FIFO.
class Esp32Uart {
public:
  Esp32Uart(int uartNumber, int rxPin, int txPin, size_t rxBufferBytes = 256);
  ~Esp32Uart();
  Esp32Uart(const Esp32Uart&) = delete;
  Esp32Uart& operator=(const Esp32Uart&) = delete;
  bool Begin(unsigned long baud, uint32_t serialConfig);
  void End(); // Stop calling Device::Update() before ending/changing its port.
  bool Ready() const { return started_; }
  esp_err_t LastError() const { return error_; }
  Transport Adapter();

private:
  static int read(void*);
  static bool write(void*, uint8_t);
  static uint32_t clock(void*);
  int number_;
  int rxPin_;
  int txPin_;
  size_t rxBufferBytes_;
  bool started_;
  esp_err_t error_;
};
} // namespace lightyak
#endif
#endif
