# LightYak

A lightweight communication library that exposes Arduino variables as browsable, typed tags. It lets an HMI or other host discover a device's tags, poll their values, and write to tags explicitly marked writable.

**Status: pre-alpha.** LightYak is experimental software under active development. The API and wire protocol may change. Automated tests and Arduino Uno compilation checks are in place; physical hardware timing and end-to-end serial validation are still pending.

## What it does

- Discovers tag names, types, references, and access permissions.
- Supports booleans, characters, signed and unsigned 8/16/32-bit integers, and 32-bit floating-point values.
- Reads and writes up to six tags per request, with a result for each item.
- Provides built-in device information and a 1 Hz heartbeat value.
- Uses fixed memory and bounded work to return from `Update()` quickly.

The current implementation targets Arduino Uno-style AVR boards using hardware serial. It supports 24 application tags by default. Names are text metadata; string-valued tags are not supported.

## Basic use

Register global or static variables during `setup()`, then call `Update()` frequently from `loop()`:

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
