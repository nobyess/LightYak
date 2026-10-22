# ESP32 hardware UART support

LightYak uses the same wire-v1 protocol and HMI driver on AVR and ESP32. The default capacity remains 10 tags; batch size remains six. ESP32 support requires Arduino-ESP32 3.x / ESP-IDF 5.x or newer. The tested core version and board build results are in [TEST_RESULTS.md](../TEST_RESULTS.md).

## Connect and register tags

Start with [ESP32Serial](../examples/ESP32Serial/ESP32Serial.ino). Its UART1 is separate from USB/debug output:

```cpp
#include <LightYak.h>

lightyak::Esp32Uart hmiPort(1, 4, 5); // UART number, RX GPIO, TX GPIO
bool enabled = false;
float temperature = 22.5f;

void setup() {
  LightYak.Listen(hmiPort, 115200, SERIAL_8N1, F("ESP32 Controller"));
  LightYak.AddTag(enabled, F("Enabled"), lightyak::ReadWrite);
  LightYak.AddTag(temperature, F("Temperature"));
}

void loop() {
  LightYak.Update();
}
```

Choose available GPIOs suitable for your board; example pins 4/5 are configurable. Use a **3.3 V USB/UART adapter**: adapter TX connects to the ESP32 RX pin, adapter RX connects to its TX pin, and connect GND. Select that adapter's COM port in the HMI, with matching serial settings. Many boards' built-in USB connections use a different UART or native USB, so selecting the onboard USB COM port does not automatically connect to UART1.

UART0 can be selected with its physical RX/TX pins if it is otherwise unused. No `HardwareSerial`, logger, console, callback, second task, or other driver may read/write/reconfigure the selected UART or pins while LightYak owns it. `Begin()` refuses an already installed driver, rather than stealing it. Do not pass `Serial`/`Serial1` directly to `Listen()` on ESP32; this produces a clear compile-time error. Native USB CDC and Wi-Fi/TCP are not supported by this adapter.

`Listen()` accepts the full 32-bit ESP32 serial configuration. Supported formats use eight data bits, one/two stop bits, and no/even/odd parity. RX ring size defaults to 256 bytes and may be supplied as the fourth `Esp32Uart` constructor argument; it must be at least 256 bytes. All initialization allocation happens before normal `Update()` operation.

Check `LightYak.Ready()` / `LastError()` and `hmiPort.LastError()` after setup. Invalid settings or a failed driver setup leave LightYak unready. Stop using the transport before calling `hmiPort.End()`; call `Listen()` again to reset the protocol state when restarting. Setup, registration, destruction and transport reconfiguration belong to one task, outside `Update()`/ISRs.

## Bounded processing

- Same default limits: four RX wire bytes, four TX wire bytes, four preparation/CRC bytes, one state action and at most one four-byte scalar copy per call.
- RX calls ESP-IDF `uart_read_bytes()` for one byte with zero wait ticks, bypassing Arduino's outer waiting HAL mutex.
- TX uses a free-space check and a single low-level FIFO write. It does not call `HardwareSerial::write()`, `uart_write_bytes()`, `uart_tx_chars()` or wait for transmission to finish. No TX ring/ISR is installed. This is safe only with the exclusive UART ownership described above.
- Value access tries its critical-section guard with zero timeout. If unavailable, the current item stays pending until another call; no partial scalar or extra error code is transmitted.

ESP32 is a scheduled, interrupt-driven system. IDF internal critical sections, Wi-Fi activity, preemption and flash operations can change wall-clock time despite these limits. Keep automatic light sleep disabled while the UART is active; the adapter does not manage UART wakeup, power-management locks or clock changes. Runtime microsecond limits still require on-board measurement. Use [ESP32Timing](../examples/ESP32Timing/ESP32Timing.ino) to inspect GPIO pulses and the maximum observed duration.

## Shared variables

One task owns `Update()`. If all registered values are used only in that task's loop, normal assignments between calls need no extra guard. If another task or ISR reads or writes a tag variable, **every concurrent access must use the same guard**:

```cpp
// Compute or acquire the new measurement before taking the guard.
if (LightYak.TryLockTags()) {
  temperature = newTemperature;
  LightYak.UnlockTags();
}
// If unavailable, let the producer retry later instead of spinning.
```

Keep the guarded section short: no I/O, allocation, logging, delays, or calls to `Update()`. `volatile` alone is not synchronization. Guard entry/exit functions are placed in IRAM and use ESP-IDF's task/ISR-safe critical primitives; ISR code and its data must also satisfy the platform's ISR/IRAM rules. Do not use these APIs from unsupported high-priority/NMI contexts. The guard is scoped to each `Device` instance, so guard using the device that owns the tags.

Use `float` for 32-bit floating-point tags. ESP32 native `double` is 64-bit and is deliberately rejected; wire-v1 value types have not changed. Native integer widths are still advertised in browse results.

## Implementation references

The adapter is built against the installed Espressif headers. Reference documentation: [ESP32 UART API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/uart.html), [Arduino-ESP32 serial API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html), and [ESP-IDF UART driver source](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_uart/src/uart.c). The driver source explains why even convenience TX functions require care around waiting mutexes.
