// SPDX-License-Identifier: MIT
#include <LightYak.h>

bool motorEnabled = false;
uint8_t operatingMode = 1;
int16_t targetRPM = 1200;
uint32_t cycleCount = 0;
float temperature = 22.5f;
bool ledOn = false;

void setup() {
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(LED_BUILTIN, OUTPUT);
  // F() keeps metadata in flash on the Uno. Plain "text" is also supported.
  LightYak.Listen(Serial, 115200, SERIAL_8N1, F("Pump Controller"));
  LightYak.AddTag(motorEnabled, F("Motor Enabled"), lightyak::ReadWrite);
  LightYak.AddTag(operatingMode, F("Operating Mode"), lightyak::ReadWrite);
  LightYak.AddTag(targetRPM, F("Target RPM"), lightyak::ReadWrite);
  LightYak.AddTag(cycleCount, F("Cycle Count"));
  LightYak.AddTag(temperature, F("Temperature"));
  LightYak.AddTag(ledOn, F("LED"), lightyak::ReadWrite);
}

void loop() {
  LightYak.Update();
  // Apply HMI changes only when needed; the Uno LED is active-high.
  static bool appliedLedState = false;
  if (ledOn != appliedLedState) {
    digitalWrite(LED_BUILTIN, ledOn ? HIGH : LOW);
    appliedLedState = ledOn;
  }
  const uint32_t now = millis();
  static uint32_t lastSample = 0;
  if (now - lastSample >= 1000UL) {
    lastSample = now;
    ++cycleCount;
    temperature = 22.5f + (cycleCount % 10) * 0.1f;
  }
  // Consume writable settings here; do not overwrite them every loop.
  // Keep this serial port exclusive to LightYak (no Serial.print()).
}
