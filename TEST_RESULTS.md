# Validation results — LightYak 0.1.0

Native suite validated October 21, 2026. BasicTags was recompiled October 22, 2026 after adding its writable LED tag (catalog revision 2). No board was flashed or physically exercised.

## Automated suite

**170 checks passed:** 51 native C++ cases repeated under three work-budget configurations (153 executions), 13 Python codec/client/interoperability tests, and four expected compile-time rejections.

Command:

```text
python tests/run_tests.py --build-dir <workspace-build-directory>
```

Native compiler: MinGW-w64 GCC 16.1.0, C++11, `-O2 -Wall -Wextra -Wpedantic -Werror`. Python: 3.14.0. Byte budgets tested: 4 (default), 1, and 8 for receive, transmit, and preparation. All configurations passed.

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
- No `new`/`new[]` allocation during any native harness `Update()` call. Source uses no allocation functions.
- Python client talking to the production C++ core through a native process, including 300 heartbeat polls crossing request-ID wrap and 100 random signed write/readbacks.
- Compile-time rejection of character arrays/string values, const scalars, custom structs and wide integers.

The native harness uses an in-memory nonblocking transport; it cannot establish UART ISR performance or a physical worst-case execution time. No AddressSanitizer/UBSan run is claimed.

## Arduino Uno builds

Arduino CLI 1.5.1; installed Arduino AVR core 1.8.8; AVR GCC 7.3.0; target `arduino:avr:uno`. All three examples compiled successfully using the installed toolchain. No downloads were necessary.

| Example | Flash | Static RAM | Remaining SRAM for stack/application |
|---|---:|---:|---:|
| BasicTags (LED, revision 2) | 6,824 / 32,256 bytes | 678 / 2,048 bytes | 1,370 bytes |
| TimingProbe | 6,016 / 32,256 bytes | 672 / 2,048 bytes | 1,376 bytes |
| AllScalarTypes | 5,794 / 32,256 bytes | 682 / 2,048 bytes | 1,366 bytes |

The default global `LightYak` object occupies **461 bytes of AVR static RAM**, including 24 registry entries and its fixed buffers. The above sketch totals also include Arduino serial buffers, runtime globals, and application variables. Remaining SRAM is not a stack high-water measurement.

Build with:

```text
arduino-cli compile --fqbn arduino:avr:uno --library <LightYak-folder> <LightYak-folder>/examples/BasicTags
```

## On-board follow-up

Use TimingProbe and a logic analyzer on pin 8 to measure idle, identify, browse, six-item reads/writes, malformed input and serial congestion. The maximum-us tag includes measurement overhead and uses the Uno's coarse `micros()` resolution. Work counters establish bounded operation counts, not a microsecond guarantee. End-to-end COM-port operation, physical receive overrun behavior, interrupt interference, and stack high-water use remain to be measured on hardware.
