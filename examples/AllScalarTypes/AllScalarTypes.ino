// SPDX-License-Identifier: MIT
#include <LightYak.h>
bool flag = false;
char letter = 'A';
int8_t signedByte = -1;
uint8_t unsignedByte = 255;
int16_t signedWord = -1200;
uint16_t unsignedWord = 65535;
int32_t signedLong = -100000;
uint32_t unsignedLong = 100000;
float realValue = 22.5f;
volatile uint16_t interruptValue = 0;

void setup() {
  LightYak.Listen(Serial, 115200, SERIAL_8N1, F("All Scalar Types"));
  LightYak.AddTag(flag, F("Boolean"), lightyak::ReadWrite);
  LightYak.AddTag(letter, F("Character"), lightyak::ReadWrite);
  LightYak.AddTag(signedByte, F("Int8"), lightyak::ReadWrite);
  LightYak.AddTag(unsignedByte, F("UInt8"), lightyak::ReadWrite);
  LightYak.AddTag(signedWord, F("Int16"), lightyak::ReadWrite);
  LightYak.AddTag(unsignedWord, F("UInt16"), lightyak::ReadWrite);
  LightYak.AddTag(signedLong, F("Int32"), lightyak::ReadWrite);
  LightYak.AddTag(unsignedLong, F("UInt32"), lightyak::ReadWrite);
  LightYak.AddTag(realValue, F("Float32"), lightyak::ReadWrite);
  LightYak.AddTag(interruptValue, F("ISR Shared UInt16"));
}
void loop() { LightYak.Update(); }
