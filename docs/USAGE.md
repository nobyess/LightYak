# LightYak usage and development guide

Run the commands below from the repository root. LightYak is pre-alpha software; its API and protocol may change.

A small, browsable Arduino serial library with writable scalar tags. The main design target is **bounded work per `Update()` call**, not maximum throughput. This first implementation is version 0.1.0 of wire protocol v1.

## Install and run

In Arduino IDE, install the supplied `LightYak-v0.1.0.zip` with **Sketch > Include Library > Add .ZIP Library**. For AVR, open **File > Examples > LightYak > BasicTags** and select your Uno or Mega. For ESP32, install Arduino-ESP32 3.x and open **ESP32Serial**; see [ESP32 setup](ESP32.md) for the dedicated UART connection. Alternatively, put this `LightYak` folder in your sketchbook's `libraries` directory.

```cpp
#include <LightYak.h>
bool enabled = false;
int16_t speed = 1200;
float temperature = 22.5f;

void setup() {
  LightYak.Listen(Serial, 115200, SERIAL_8N1, F("Pump Controller"));
  LightYak.SetCatalogRevision(1);
  LightYak.AddTag(enabled, F("Enabled"), lightyak::ReadWrite);
  LightYak.AddTag(speed, F("Speed"), lightyak::ReadWrite);
  LightYak.AddTag(temperature, F("Temperature"));
}
void loop() {
  LightYak.Update();
  // Run your application without delay() or other long blocking work.
}
```

The AVR library initializes hardware serial. Keep the port exclusive: no `Serial.print()`, other serial writers, or calls to `Update()` from an interrupt. SoftwareSerial is not supported. ESP32 uses the dedicated `Esp32Uart` adapter rather than the ordinary `HardwareSerial`/USB CDC path, whose waiting locks are unsuitable for our work-budget contract.

## API

- `Listen(port, baud, serialConfig, name)` returns success and starts a fresh registry. The names can be plain `"text"` or Arduino `F("text")`. Flash names save SRAM on AVR; ordinary literals consume SRAM on a classic Uno.
- `AddTag(variable, name, access)` returns a reference starting at 1; zero means registration failed. Access defaults to `lightyak::ReadOnly`; opt in to `lightyak::ReadWrite`.
- `SetCatalogRevision(uint32_t)` sets a programmer-maintained catalog revision. Change it when names, references, access, or types change. The default is 1. A host should always browse after reconnecting, even if this revision was not maintained.
- `Update()` processes at most the configured byte budgets and one internal state action. It never waits for more input or output space.
- `Ready()` and `LastError()` expose sticky initialization errors. `Identify` works even when registration failed; other operations return `NOT_READY`.
- `TagCount()` returns application tag count. `LastWork()` exposes work counters from the most recent call for diagnostics and tests.

Register tags only during setup, before the first `Update()`. Global or static variables and metadata must remain alive; names are referenced, not copied, and must remain unchanged. Duplicate names are allowed: numeric references are authoritative. Name/device-name length is 1..24 UTF-8 bytes, excluding the terminating null. No descriptions in this release.

Scalar types: bool, char, signed/unsigned 8/16/32-bit integers, and IEEE 754 binary32 float. Native integer names map by width. `double` is accepted only when it is binary32 (as on a classic Uno); 64-bit double, arrays, strings, structs, pointers and const values are rejected at compile time. A character is one byte, not a string. Float bit patterns, including NaN and infinities, are preserved; v1 provides no engineering-range validation or callbacks.

## HMI flow

Open port -> identify -> browse -> poll by `(reference, property)` -> optionally write. There is one outstanding request. Browse returns one definition per request and a next cursor. Reads/writes batch 1..6 items. All values have explicit types and sizes; byte order is little-endian.

Device reference 0 contains numeric read-only properties: Heartbeat (property 1), Protocol Version (2), Catalog Revision (3), Payload Limit (4), Batch Limit (5), and Initialization Status (6). Device Name is metadata in identification, not a string-valued tag. Application tags use property 0. `tools/lightyak_client.py` hides this encoding behind `identify()`, `browse()`, `read()` and `write()`.

Heartbeat toggles every 500 ms, making a 1 Hz square wave derived from uptime. Poll around 200 ms to see it; use response timeouts for connection health. Uptime wraps with Arduino `millis()`; the heartbeat may have an irregular interval at that rollover.

## Writable tags

The entire frame and all write-item boundaries are checked before any effect. Each item is then checked and applied separately, at most one per `Update()`. Type and width must match exactly. Booleans must be 0 or 1. Device properties and read-only application tags cannot be written. Responses report a status for every item. A structurally valid batch can partially succeed; it is not atomic and may span calls. Repeated targets are applied in order.

Application code must consume writable settings rather than overwrite them every loop. AVR scalar copies use short interrupt-protected sections of at most four bytes, preserving the previous interrupt state. `volatile` scalars are supported for AVR ISR-shared variables. ESP32 uses a zero-wait try-critical-section around each scalar copy: if another task/ISR holds the guard, that item is retried on a later `Update()` without advancing the batch or sending a new status. Other producers must use the same `TryLockTags()`/`UnlockTags()` guard; `volatile` alone is insufficient. See [ESP32 shared variables](ESP32.md#shared-variables).

No request is retried automatically. A timed-out write may already have happened; reopen/resynchronize, then read back. Request IDs match responses but do not deduplicate writes or guarantee exactly-once effects. Opening an Uno serial port normally resets the board; the reference tool waits two seconds for startup. For transports that do not reset the device, wait for outstanding work to drain and use a quiet period longer than the partial-frame timeout before a fresh identification. Do not resume an uncertain session blindly.

## Predictable work and memory

Defaults, independent of the number of registered tags:

| Per `Update()` | Maximum |
|---|---:|
| Incoming wire bytes | 4 |
| Outgoing wire bytes | 4 |
| Metadata-copy or outgoing CRC bytes | 4 |
| Internal state-machine actions | 1 |
| Scalar copies | 1, at most 4 bytes |

Receive work can occur alongside one processing/transmit action. CRC is incremental using a 16-entry lookup table (in flash on AVR). There is no runtime tag search, name scan, core heap allocation, whole-frame CRC pass, or whole-batch execution. The idle path makes one nonblocking read and returns without querying the clock or scanning the registry. AVR transmits only when hardware serial reports space; ESP32 checks and writes one byte directly to its exclusively owned UART FIFO. ESP32 driver resources are allocated at initialization; OS/interrupt scheduling still affects elapsed time.

The default registry supports 10 application tags, plus the six system properties. On an Uno or Mega each reserved slot uses eight bytes, excluding the application's variable and any RAM-stored name. Increasing capacity from 10 to 48 reserves 304 additional bytes of SRAM, even if those slots are unused. There are two 71-byte receive buffers and a 71-byte response buffer: the extra receive storage permits parsing/rejecting unsolicited requests while an accepted request owns its buffer. One reserved busy-rejection slot exists. More excess requests are dropped until that slot drains. This keeps memory and work bounded under a misbehaving sender.

To expand the registry, open the installed library's `src/LightYakConfig.h`, change `#define LIGHTYAK_MAX_TAGS 10` to (for example) `#define LIGHTYAK_MAX_TAGS 48`, and rebuild the whole sketch and library. Keep the receive, transmit and preparation budgets unchanged if you want the same per-call work limits. Do not add a sketch-local capacity override. If registration exceeds the configured capacity, `AddTag()` returns zero and `LastError()` reports `RegistryFull`; increase capacity and rebuild rather than ignoring that error.

Edit the defaults in [src/LightYakConfig.h](../src/LightYakConfig.h) to change `LIGHTYAK_MAX_TAGS`, `LIGHTYAK_RX_BUDGET`, `LIGHTYAK_TX_BUDGET`, or `LIGHTYAK_PREP_BUDGET`. This shared header is included by both the sketch and library, keeping their class layouts consistent. Rebuild the entire sketch/library after changing it; use a clean build if your IDE retains stale objects. Advanced builds can still override settings with compiler `-D` flags, but those must apply to **every translation unit**. Do not define settings only in your sketch: a sketch-local override can break class layout. Tag capacity must be 1..255. Receive/transmit budgets must be 1..32 and preparation budget 1..16; invalid settings fail at compile time. Lower byte budgets reduce work per call but require calling `Update()` more often. Frame size, name limit, and batch limit are fixed for wire v1. Increasing tag capacity does not change these budgets or the direct reference lookup; browsing and polling more tags take more calls in total.

At 115200 baud, a standard Uno's receive buffer can fill in a few milliseconds during a burst. With a four-byte receive budget, the application must service `Update()` at least as fast as bytes arrive; bounded library work does not compensate for long application delays. Measured maximum call time still depends on the compiler, MCU, transport, interrupts, and selected operation. No microsecond upper bound is claimed from native tests.

## Reference serial client

Only actual serial-port access needs the optional `pyserial` package. The codec, simulator, and automated tests use the Python standard library.

```text
python -m pip install pyserial
python tools/lightyak_client.py --port COM4 browse
python tools/lightyak_client.py --port COM4 read 1 2 5 0:1
python tools/lightyak_client.py --port COM4 write 3 1500
python tools/lightyak_client.py --port COM4 write 1 true
python tools/lightyak_client.py --port COM4 write 6 true
```

These targets match BasicTags: 1 Enabled, 2 Mode, 3 RPM, 4 Counter, 5 Temperature, 6 LED. The writable boolean LED tag controls the Uno's built-in LED: true turns it on and false turns it off. The sketch applies LED changes after `Update()` and only writes the pin when the value changes. Serial parity and stop bits are selectable; data bits are fixed at 8 for the binary protocol. Each CLI invocation opens a new connection and may reset an Uno, so use one persistent `Client` instance for continuous HMI use or write/readback sequences.

## Test suite

```text
python tests/run_tests.py
python tests/run_tests.py --build-dir ../../work/lightyak-build
```

Requires Python 3.10+ and a C++11-capable g++ or clang++. The runner tests the default capacity under budgets of 4, 1 and 8 bytes, compares a 24-tag baseline, and tests a copied configuration changed to 64 tags by editing only the shared header. The core suite also runs through the ESP32 compile-time branches under three budgets. IDF/HAL fakes test the real adapter, and both C++ simulators run against the same independent Python client. Compile-time checks reject unsupported types, invalid settings and unbounded ESP32 serial adapters. On compilers with sanitizer runtimes, add `--sanitize`. Coverage includes a full registry, browsing and writing its final slot, framing/escaping/CRC, typed reads/writes, read-only protection, full structural validation before writes, partial results, busy handling, backpressure, timeouts and rollover, randomized traffic, bounded work, and no heap allocation inside the core's `Update()`.

Compile `examples/BasicTags` and `examples/TimingProbe` for `arduino:avr:uno`. TimingProbe exposes maximum observed `micros()` duration and pulses pin 8 around `Update()` for a logic analyzer. It includes measurement overhead and interrupt latency; it is not a formal WCET proof. Reset its maximum by writing zero, then exercise identify, browse, maximum read/write batches, malformed traffic, and input/output congestion. Hardware validation is still required before claiming an actual time bound.

The [FullCapacity test sketch](../tests/arduino/FullCapacity/FullCapacity.ino) registers every configured slot with a separate writable `uint32_t` value. Compile it to check SRAM use with a populated registry. Its names intentionally repeat; references remain distinct.

With the board packages already installed, `python tests/compile_boards.py --build-dir <build-folder>` runs the Uno, Mega, ESP32, S3 and C3 compile matrix without uploading. Pass `--cli <arduino-cli-path>` and `--config-file <yaml>` if necessary. Use `--target` to select one or more builds. Logs and memory summaries are saved in the supplied build folder.

See [PROTOCOL.md](../PROTOCOL.md) for the complete wire contract and [TEST_RESULTS.md](../TEST_RESULTS.md) for this build's validation results.

## Later extensions

TCP transport, writes to richer types, COV/subscriptions, event ranges and remote input bindings are deferred. The core uses a nonblocking transport interface; future adapters can carry the same operations. v1 does not provide broadcast discovery, authentication, encryption, or arbitrary application callbacks. Protocol-version and reserved flag fields permit deliberate future extensions rather than changing existing operation meanings.
