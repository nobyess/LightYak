// SPDX-License-Identifier: MIT
#ifndef LIGHTYAK_MOCK_PORTMACRO_H
#define LIGHTYAK_MOCK_PORTMACRO_H
#include "Esp32Mock.h"
struct portMUX_TYPE { bool held; };
#define portMUX_INITIALIZER_UNLOCKED {false}
#define portTRY_ENTER_CRITICAL_SAFE(mux, cycles) esp32mock::tryLock(&(mux)->held, cycles)
#define portEXIT_CRITICAL_SAFE(mux) esp32mock::unlock(&(mux)->held)
#endif
