// SPDX-License-Identifier: MIT
// Same dedicated UART wiring as ESP32Serial. Observe GPIO2 with a logic analyzer;
// choose another safe measurement pin if GPIO2 is unavailable on your board.
#include <LightYak.h>
#if !defined(ARDUINO_ARCH_ESP32)
#error "Select an ESP32 board for ESP32Timing; use TimingProbe for AVR boards"
#endif
lightyak::Esp32Uart hmiPort(1, 4, 5);
uint32_t maxUpdateUs = 0;
int32_t setting = 0;
bool enabled = false;

void setup() {
  pinMode(2, OUTPUT);
  LightYak.Listen(hmiPort, 115200, SERIAL_8N1, F("ESP32 Timing Probe"));
  LightYak.AddTag(maxUpdateUs, F("Max Update us"), lightyak::ReadWrite);
  LightYak.AddTag(setting, F("Setting"), lightyak::ReadWrite);
  LightYak.AddTag(enabled, F("Enabled"), lightyak::ReadWrite);
}

void loop() {
  digitalWrite(2, HIGH);
  const uint32_t start = micros();
  LightYak.Update();
  const uint32_t elapsed = micros() - start;
  digitalWrite(2, LOW);
  if (elapsed > maxUpdateUs) maxUpdateUs = elapsed;
}
