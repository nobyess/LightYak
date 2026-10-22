# Validation results — LightYak 0.1.0

Validated October 22, 2026 with the 10-tag default, shared configuration header, and dedicated ESP32 UART adapter. No board was flashed or physically exercised.

## Automated suite

**484 checks passed:** 54 core C++ cases under eight build configurations (432 executions), 13 Python codec/client/interoperability cases against each of two simulators (26 executions), 15 ESP32 adapter cases, and 11 expected compile-time rejections. In addition, nine real-toolchain firmware builds passed.

Command:

```text
python tests/run_tests.py --build-dir <workspace-build-directory>
```

Native compiler: MinGW-w64 GCC 16.1.0, C++11, `-O2 -Wall -Wextra -Wpedantic -Werror`. Python: 3.14.0. All configurations passed:

| Registry capacity | Receive / transmit / preparation budgets | Configuration source |
|---:|---|---|
| 10 | 4 / 4 / 4 | Shared-header default |
| 10 | 1 / 1 / 1 | Project-wide budget flags |
| 10 | 8 / 8 / 8 | Project-wide budget flags |
| 24 | 4 / 4 / 4 | Original-capacity baseline |
| 64 | 4 / 4 / 4 | Edit only a copied shared header, then compile core and tests as separate translation units |
| 10 | 4 / 4 / 4 | ESP32 compile-time path, IDF/HAL fakes |
| 10 | 1 / 1 / 1 | ESP32 compile-time path, IDF/HAL fakes |
| 10 | 8 / 8 / 8 | ESP32 compile-time path, IDF/HAL fakes |

Coverage includes:

- Independent bitwise CRC golden vector versus the production flash-table implementation.
- Identity, complete catalog enumeration, system properties, and scalar type/byte-order handling.
- Writable booleans, signed/unsigned integer widths, characters, and floating-point bit patterns.
- Read-only protection, exact type/width checks, invalid boolean encodings, invalid references/properties.
- Maximum batches, sequential write visibility, per-item failures and partial success.
- No write before CRC/end delimiter; malformed later items and trailing data prevent every effect.
- Escaping, input fragmentation, blocked output and one-byte-at-a-time output availability.
- One pending busy rejection; excess-request storms do not apply unsolicited writes.
- Partial-frame timeout, clock rollover, oversized/malformed frames and resynchronization.
- 1,000 deterministic random malformed-input bursts per native configuration, each followed by a valid identity exchange.
- 500 randomized float-bit-pattern write/readbacks per native configuration.
- Per-call receive/transmit/preparation/action/scalar-copy limits checked on every harness update.
- No `new`/`new[]` allocation during any core harness `Update()` call, including ESP32 guard/deferral branches. The core source uses no allocation functions; ESP32 driver allocation occurs at initialization.
- The same independent Python client talking to the production C++ core and to the production ESP32 UART adapter through separate native simulators, including 300 heartbeat polls crossing request-ID wrap and 100 random signed write/readbacks for each. No HMI protocol changes were needed.
- Compile-time rejection of character arrays/string values, const scalars, custom structs and wide integers.
- Full registry enumeration through its last reference, read/write access to that final slot, and idle behavior with all slots occupied.
- Identity advertises the configured capacity consistently with the client-side class layout.
- Invalid tag capacities 0 and 256, receive budget 0, transmit budget 33 and preparation budget 17 are rejected by the shared header's static assertions.
- ESP32 full-width serial configuration, flash-name overload, invalid ports/pins/formats/buffer sizes, existing-driver refusal and setup-failure cleanup.
- ESP32 zero-wait single-byte receive, initialization-only driver setup, full-FIFO deferral, and one-free-slot output pacing. Fakes deliberately omit blocking convenience TX APIs, making accidental use a compile failure.
- Contended ESP32 reads/writes stay pending, never emit the internal deferral marker, and do not reapply earlier successful batch items. System properties remain readable without the value guard.
- Compile-time rejection of direct ESP32 HardwareSerial/USB-style use and native 64-bit double registration.

The native harness uses in-memory transports and small IDF/HAL fakes. These verify the actual adapter's choices and protocol behavior but cannot establish real driver/interrupt timing, inter-core scheduling or a physical worst-case execution time. No AddressSanitizer/UBSan run is claimed.

## Arduino Uno builds

Arduino CLI 1.5.1; installed Arduino AVR core 1.8.8; AVR GCC 7.3.0. All three Uno examples, the full-capacity test and Mega BasicTags compiled successfully. No downloads were necessary.

| Example | Flash | Static RAM | Remaining SRAM for stack/application |
|---|---:|---:|---:|
| BasicTags, 10-slot default | 6,824 / 32,256 bytes | 566 / 2,048 bytes | 1,482 bytes |
| TimingProbe, 10-slot default | 6,016 / 32,256 bytes | 560 / 2,048 bytes | 1,488 bytes |
| AllScalarTypes, 10-slot default | 5,794 / 32,256 bytes | 570 / 2,048 bytes | 1,478 bytes |
| FullCapacity, 10 separate writable uint32 values | 5,734 / 32,256 bytes | 587 / 2,048 bytes | 1,461 bytes |
| Mega BasicTags, 10-slot default | 7,588 / 253,952 bytes | 566 / 8,192 bytes | 7,626 bytes |
| BasicTags, 24-slot comparison build | 6,824 / 32,256 bytes | 678 / 2,048 bytes | 1,370 bytes |
| BasicTags, previous 48-slot build | 6,824 / 32,256 bytes | 870 / 2,048 bytes | 1,178 bytes |

The 10-slot registry reduces the global `LightYak` object to **349 bytes of AVR static RAM**, including its fixed buffers. The same object with 24 slots uses 461 bytes and with 48 slots uses 653 bytes: **eight bytes per reserved slot**. Compared with 48 slots, the new default saves 304 bytes before counting application variables or names. The sketch totals also include Arduino serial buffers, runtime globals, and application variables. Remaining SRAM is not a stack high-water measurement.

The 24-/48-slot rows are earlier comparison builds; the new 10-slot AVR builds retain the same flash/RAM figures after adding ESP32 support. ESP32-only guard/deferral branches leave the AVR execution path unchanged. Work budgets remain 4 receive bytes, 4 transmit bytes, 4 preparation/CRC bytes, one state action and at most one scalar copy per call. Tests enforce these bounds with full registries. These observations support unchanged bounded work; they are not a claim of equal measured microsecond timing on hardware.

Build with:

```text
arduino-cli compile --fqbn arduino:avr:uno --library <LightYak-folder> <LightYak-folder>/examples/BasicTags
```

## ESP32 builds

Installed Arduino-ESP32 3.3.11, ESP-IDF headers 5.5.5. Successful builds use `Esp32Uart` on dedicated UART1 with explicit pins. Normal HardwareSerial and native USB CDC are not the tested transport.

| Target / sketch | Flash | Static RAM | Reported remaining RAM |
|---|---:|---:|---:|
| ESP32 / ESP32Serial | 271,420 bytes | 22,620 bytes | 305,060 bytes |
| ESP32 / ESP32Timing | 274,152 bytes | 22,604 bytes | 305,076 bytes |
| ESP32-S3 / ESP32Serial | 275,989 bytes | 22,088 bytes | 305,592 bytes |
| ESP32-C3 / ESP32Serial | 280,496 bytes | 13,696 bytes | 313,984 bytes |

The global LightYak device object is **456 bytes** in the classic ESP32 build. Sketch totals include Arduino/RTOS support and are not the library's standalone cost. The UART driver additionally allocates its RX ring and control objects at setup; those allocations and later application heap/stack use are not captured by these static totals. No physical UART, Wi-Fi-load, power-management or inter-core hardware timing test is claimed.

Reproduce all nine builds with installed board packages:

```text
python tests/compile_boards.py --build-dir <workspace-build-directory>
```

Optional `--cli`, `--config-file` and repeated `--target` arguments select the installed toolchain and builds. The script records CLI logs and `results.json` in the supplied build directory and never uploads firmware.

## On-board follow-up

Use TimingProbe and a logic analyzer on pin 8 for AVR, or ESP32Timing on its configurable measurement pin. Measure idle, identify, browse, six-item reads/writes, malformed input and serial congestion. The maximum-us tag includes measurement overhead and uses the board's `micros()` resolution. Work counters establish bounded operation counts, not a microsecond guarantee. End-to-end COM-port operation, physical receive overrun behavior, interrupt interference, ESP32 task/guard contention, and stack high-water use remain to be measured on hardware.
