// SPDX-License-Identifier: MIT
// Arduino-ESP32 3.x. Dedicated UART1, connected to a 3.3 V USB/UART adapter.
// Adapter TX -> GPIO4; adapter RX <- GPIO5; common GND.
// Choose safe, available pins for your particular board. Do not share this UART.
#include <LightYak.h>
#if !defined(ARDUINO_ARCH_ESP32)
#error "Select an ESP32 board for ESP32Serial; use BasicTags for AVR boards"
#endif

lightyak::Esp32Uart hmiPort(1, 4, 5); // UART number, RX pin, TX pin
bool enabled = false;
int16_t targetRPM = 1200;
float temperature = 22.5f;
uint32_t cycleCount = 0;

void setup() {
  LightYak.Listen(hmiPort, 115200, SERIAL_8N1, F("ESP32 Controller"));
  LightYak.AddTag(enabled, F("Enabled"), lightyak::ReadWrite);
  LightYak.AddTag(targetRPM, F("Target RPM"), lightyak::ReadWrite);
  LightYak.AddTag(temperature, F("Temperature"));
  LightYak.AddTag(cycleCount, F("Cycle Count"));
  // LightYak.Ready()/LastError() and hmiPort.LastError() report setup failures.
}

void loop() {
  LightYak.Update();
  const uint32_t now = millis();
  static uint32_t lastSample = 0;
  if (now - lastSample >= 1000UL) {
    lastSample = now;
    ++cycleCount;
    temperature = 22.5f + (cycleCount % 10) * 0.1f;
  }
  // These values belong to this loop. Other tasks/ISRs must use the documented
  // TryLockTags()/UnlockTags() guard when touching registered variables.
}
