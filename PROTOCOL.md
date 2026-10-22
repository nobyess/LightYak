# LightYak wire protocol v1

This document specifies the implemented 0.1.0 release, not the earlier design sketches. Integer fields are unsigned unless specified. All multibyte numbers, including CRC, are little-endian. There are no string **values**; names are UTF-8 metadata. No padding or native C++ structs appear on the wire.

## Transport envelope

Wire frame: `7E | escaped body | 7E`.

Decoded body:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 1 | Version = 1 |
| 1 | 1 | Operation |
| 2 | 1 | Flags = 0 (reserved) |
| 3 | 1 | Request ID, echoed by response |
| 4 | 1 | Payload byte length, 0..64 |
| 5 | length | Payload |
| 5 + length | 2 | CRC-16/IBM-3740, also called CCITT-FALSE |

CRC parameters: polynomial `0x1021`, initial `0xFFFF`, no reflection, final XOR zero. Calculate over the five header bytes and payload, before escaping. Check value for ASCII `123456789`: `0x29B1`.

Within the body, `7E` becomes `7D 5E` and `7D` becomes `7D 5D`. Other escape pairs are invalid. Delimiters themselves are not escaped. A delimiter can close one frame and open the next. Empty frames are ignored. The decoded body is at most 71 bytes; the theoretical escaped frame bound is 144 bytes including delimiters. Unescaped framing overhead is nine bytes.

The device discards invalid CRCs, inconsistent lengths, invalid escapes, and oversized frames, resynchronizing at a delimiter. No error reply is generated for an untrusted frame. A partial frame expires after 250 ms without receive progress observed by `Update()`, using unsigned elapsed-time subtraction. Deadline precision is limited by how often `Update()` runs. Flags other than zero in a complete valid frame return `BAD_REQUEST`; unsupported versions return a v1 `UNSUPPORTED` error.

For binary UART communication use 8 data bits. The example uses 115200, 8N1. UART parity/stop bits are adapter settings. A future stream transport may reuse this envelope and the same operations; message parsing never depends on transport receive boundaries.

## Request lifecycle

One accepted request at a time. A response with operation `request operation | 0x80`, or an `FF` error response, completes that request. v1 has no multi-frame response flags: browse is explicitly paginated, and bounded read/write batches fit one response.

The host sends the next request only after the entire previous response is received and validated. Request IDs are host-selected bytes, normally incremented. IDs match responses; they do not deduplicate operations. v1 has no acknowledgments of responses and no exactly-once guarantee. A timeout can leave a write outcome unknown. Do not automatically retry writes. Resynchronize/reconnect and read back first.

The device reserves one pending `BUSY` error slot. While an accepted response is blocked or in progress, a valid excess request may be rejected after that response finishes. If this slot is already occupied, further excess requests are dropped. Accepted writes are not canceled by a busy error. Saturated output can delay both successful responses and errors; no unbounded output/error queue exists.

## Scalar types

| Code | Type | Value bytes |
|---:|---|---:|
| 1 | Boolean, only 00 or 01 | 1 |
| 2 | Character, raw octet | 1 |
| 3 | Signed int8, two's complement | 1 |
| 4 | Unsigned uint8 | 1 |
| 5 | Signed int16, two's complement | 2 |
| 6 | Unsigned uint16 | 2 |
| 7 | Signed int32, two's complement | 4 |
| 8 | Unsigned uint32 | 4 |
| 9 | IEEE 754 binary32 | 4 |

Floating-point bit patterns are transferred exactly. NaN/infinity are not rejected. v1 has no scalar conversion, scaling, units, or engineering-range validation. Code 0 appears only in failed read-item results, with value length zero.

## Target addressing

`target = reference:u16 + property:u8`.

- Reference 0 is the device object. Its properties are listed below.
- Application references 1..tag count use property 0 (Value).
- Other combinations return `UNKNOWN_TARGET`.
- References are assigned in `AddTag()` order. They are valid for this fixed catalog. Browse after reconnecting; use the programmer-maintained revision to detect schema changes.
- Names are labels, not addresses, and need not be unique.

## Operations

### 01 IDENTIFY / 81 response

Request payload is empty. Response payload:

```text
initialization_status:u8
application_tag_count:u16
catalog_revision:u32
max_payload:u8             = 64
max_batch:u8               = 6
registry_capacity:u8       = build setting (default 24)
device_name_byte_length:u8 = 0..24
device_name:bytes
```

Name length can be zero only when initialization failed. Identify remains available on a registration error. Other operations return `NOT_READY`. v1 capabilities are implied by version 1: identify, browse, scalar batch read/write, read-only device properties. Future capabilities must be negotiated rather than assumed from unused flag bits.

Initialization status codes: 0 OK, 1 NoTransport, 2 InvalidName, 3 RegistryFull, 4 RegistryFrozen. NoTransport cannot be communicated through a missing adapter but is available through `LastError()`.

Golden identify request (ID 1):

```text
7E 01 01 00 01 00 D8 FE 7E
```

### 02 BROWSE / 82 response

Request payload: `cursor:u16`, starting at zero.

Response payload contains exactly one definition:

```text
next_cursor:u16           = FFFF when enumeration is complete
reference:u16
property:u8
type:u8
access:u8                 = 0 read-only, 1 read/write
name_byte_length:u8       = 1..24
name:bytes
```

System properties are first, followed by application tags in registration order. The host issues a new browse request with the returned next cursor until FFFF. Do not send FFFF as a cursor. Out-of-range cursors return `BAD_CURSOR`. Each page is assembled over bounded work slices; no response retains a catalog-sized snapshot.

### 03 READ / 83 response

Request payload: `count:u8` followed by `count` targets. Count is 1..6; request payload length must equal `1 + 3*count`.

Response payload: `count:u8`, then one result per requested target, in request order:

```text
reference:u16
property:u8
status:u8
type:u8
value_byte_length:u8
value:bytes
```

On failure, type and length are zero and there are no value bytes. On success the value length matches its type. Maximum read payload is 61 bytes for six four-byte results. Duplicate targets are allowed. Items are sampled individually over successive calls, so this is not a simultaneous snapshot.

Golden read-heartbeat request (ID 2):

```text
7E 01 03 00 02 04 01 00 00 01 F7 5F 7E
```

### 04 WRITE / 84 response

Request payload: `count:u8`, then `count` items:

```text
reference:u16
property:u8
type:u8
value_byte_length:u8      = 0..4 structurally, exact type width for success
value:bytes
```

Count is 1..6. The frame CRC, full payload structure, every item boundary, and absence of trailing bytes are checked before any item is applied. Structurally invalid requests return one global `BAD_REQUEST` and have no effect. Widths greater than four are structurally invalid in v1.

Once structure is validated, permissions, target, type, width, and boolean encoding are checked per item. Semantic failures do not roll back earlier successes and do not prevent later items. At most one item is applied per `Update()`. Duplicate targets apply in request order.

Response payload: `count:u8`, then `reference:u16 + property:u8 + status:u8` per item. An OK is generated after the write is applied; the response may be transmitted later. Actual variable updates are bounded scalar copies, with interrupt-state-preserving protection on AVR. It is possible for an ISR/application to change a value again before acknowledgment reaches the HMI.

### FF ERROR

Payload: exactly one `status:u8`. The request ID is echoed. Global errors reject the request without application effects, except that error delivery cannot undo a different, previously accepted request.

| Status | Meaning |
|---:|---|
| 0 | OK (per-item only) |
| 1 | BAD_REQUEST: invalid structure or arguments |
| 2 | UNKNOWN_TARGET |
| 3 | NOT_WRITABLE |
| 4 | TYPE_MISMATCH: wrong type or width |
| 5 | BAD_VALUE: invalid boolean representation |
| 6 | BUSY: request not accepted |
| 7 | UNSUPPORTED: operation or protocol version |
| 8 | NOT_READY: initialization failed |
| 9 | BAD_CURSOR |

## Device object 0

| Property | Type | Access | Behavior |
|---:|---|---|---|
| 1 Heartbeat | bool | read-only | `(millis / 500) & 1` |
| 2 Protocol Version | uint8 | read-only | 1 |
| 3 Catalog Revision | uint32 | read-only | Programmer-maintained value, default 1 |
| 4 Payload Limit | uint8 | read-only | 64 |
| 5 Batch Limit | uint8 | read-only | 6 |
| 6 Initialization Status | uint8 | read-only | 0 when normal requests are accepted |

Device Name is identity metadata and not a readable string value. Its text comes from `Listen()`. Device properties are discovered like ordinary definitions, distinguished by reference 0 and a nonzero property selector. All writes to known device properties return `NOT_WRITABLE`.

## Extension boundary

v1 has no string values, subscriptions, COV, arbitrary executable callbacks, dynamic tag creation, transport addresses in commands, or network discovery. The adapter owns transport details. New operations may be introduced through a future negotiated capability/version; existing v1 layouts and meanings must not change silently. Unknown executable operations are rejected. Browse metadata in v1 has a fixed layout, so adding new mandatory metadata requires explicit version/capability negotiation, not appending bytes to v1 responses.
