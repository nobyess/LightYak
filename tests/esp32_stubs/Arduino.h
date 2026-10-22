// SPDX-License-Identifier: MIT
#ifndef LIGHTYAK_MOCK_ARDUINO_H
#define LIGHTYAK_MOCK_ARDUINO_H
#include "Esp32Mock.h"
class __FlashStringHelper;
#define IRAM_ATTR
#define F(text) (reinterpret_cast<const __FlashStringHelper*>(text))
static const uint32_t SERIAL_8N1 = 0x0800001cUL;
static const uint32_t SERIAL_8E2 = 0x0800003eUL;
inline unsigned long millis() { return esp32mock::state.now; }
struct HardwareSerial {};
extern HardwareSerial Serial;
#endif
