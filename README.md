# LightYak

A lightweight communication library that exposes Arduino variables as browsable, typed tags. It lets an HMI or other host discover a device's tags, poll their values, and write to tags explicitly marked writable.

**Status: pre-alpha.** LightYak is experimental software under active development. The API and wire protocol may change. Automated tests and AVR/ESP32 build checks are in place; physical hardware timing and end-to-end serial validation are still pending.

## What it does

- Discovers tag names, types, references, and access permissions.
- Supports booleans, characters, signed and unsigned 8/16/32-bit integers, and 32-bit floating-point values.
- Reads and writes up to six tags per request, with a result for each item.
- Provides built-in device information and a 1 Hz heartbeat value.
- Uses fixed memory and bounded work to return from `Update()` quickly.

The current implementation supports AVR hardware serial and a dedicated ESP32 hardware-UART adapter. It supports 10 application tags by default, plus six built-in system properties. Names are text metadata; string-valued tags are not supported.

## Basic use

Register global or static variables during `setup()`, then call `Update()` frequently from `loop()`. On AVR:

```cpp
#include <LightYak.h>

bool enabled = false;
float temperature = 22.5f;

void setup() {
  LightYak.Listen(Serial, 115200, SERIAL_8N1, F("My Device"));
  LightYak.AddTag(enabled, F("Enabled"), lightyak::ReadWrite);
  LightYak.AddTag(temperature, F("Temperature"));
}

void loop() {
  LightYak.Update();
  // Your application work goes here.
}
```

Tags are read-only unless marked `ReadWrite`. Keep the serial port exclusive to LightYak and avoid long blocking work in the sketch. The [BasicTags example](examples/BasicTags/BasicTags.ino) includes a writable tag controlling the Uno's built-in LED.

On ESP32, use Arduino-ESP32 3.x and the dedicated [ESP32Serial example](examples/ESP32Serial/ESP32Serial.ino). It uses `lightyak::Esp32Uart` with explicit UART/RX/TX pins; tag registration, `Update()`, and the HMI protocol are the same. See the [ESP32 guide](docs/ESP32.md) for wiring and shared-variable access. Native USB CDC and TCP are not implemented.

## Need more tags?

Open the installed library's shared [src/LightYakConfig.h](src/LightYakConfig.h) and change `LIGHTYAK_MAX_TAGS` from `10` to the capacity you need, for example:

```cpp
#define LIGHTYAK_MAX_TAGS 48
```

Rebuild the entire sketch and library after editing it. Do not put this override only in your sketch. Capacity can be 1..255, subject to available RAM; each reserved slot costs eight bytes on an Uno or Mega, plus your variable and any RAM-stored name. All configured slots reserve memory, even when unused. Leave the work budgets unchanged to keep the same limits per `Update()` call.

## How it works

The host identifies the device, browses its tag definitions, then uses compact numeric references for reads and writes. Messages are binary, length-delimited, and checked with a CRC.

`Update()` advances receiving, validation, value access, and transmission in small steps. By default, each call handles at most four incoming bytes, four outgoing bytes, and one scalar copy. Larger operations span multiple calls; no runtime heap allocation is needed. Actual execution time depends on the board, interrupts, and transport.

The host keeps one request outstanding at a time. Batch writes are sequential and can partially succeed; they are not atomic. The core and transport adapter are separate to allow future transports while preserving operation semantics. TCP support is not implemented yet.

## More information

- [Usage, installation, and testing](docs/USAGE.md)
- [Wire protocol](PROTOCOL.md)
- [Python reference client](tools/lightyak_client.py)
- [Validation results](TEST_RESULTS.md)

## License

LightYak is released under the [MIT License](LICENSE).
