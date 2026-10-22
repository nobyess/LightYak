// SPDX-License-Identifier: MIT
// Compile-only SRAM check; also usable on-board. All slots have distinct storage.
// Names intentionally repeat: tags are addressed by their unique references.
#include <LightYak.h>

uint32_t values[LIGHTYAK_MAX_TAGS] = {};

void setup() {
  LightYak.Listen(Serial, 115200, SERIAL_8N1, F("Full Capacity"));
  for (uint16_t i = 0; i < LIGHTYAK_MAX_TAGS; ++i) {
    LightYak.AddTag(values[i], F("Value"), lightyak::ReadWrite);
  }
  digitalWrite(LED_BUILTIN,
    LightYak.Ready() && LightYak.TagCount() == LIGHTYAK_MAX_TAGS ? HIGH : LOW);
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  LightYak.Update();
}
