// SPDX-License-Identifier: MIT
// Observe pin 8 with a logic analyzer. The pulse includes digitalWrite overhead.
// Max Update us is a coarse micros() reading (4 us resolution on a classic Uno).
#include <LightYak.h>
uint32_t maxUpdateUs = 0;
uint32_t calls = 0;
int32_t setting = 0;
bool enabled = false;

void setup() {
  pinMode(8, OUTPUT);
  LightYak.Listen(Serial, 115200, SERIAL_8N1, F("LightYak Timing Probe"));
  LightYak.AddTag(maxUpdateUs, F("Max Update us"), lightyak::ReadWrite);
  LightYak.AddTag(calls, F("Calls"));
  LightYak.AddTag(setting, F("Setting"), lightyak::ReadWrite);
  LightYak.AddTag(enabled, F("Enabled"), lightyak::ReadWrite);
}
void loop() {
  digitalWrite(8, HIGH);
  const uint32_t start = micros();
  LightYak.Update();
  const uint32_t elapsed = micros() - start;
  digitalWrite(8, LOW);
  if (elapsed > maxUpdateUs) maxUpdateUs = elapsed;
  ++calls;
}
