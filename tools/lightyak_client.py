# SPDX-License-Identifier: MIT
"""LightYak v1 reference codec and small polling client. Python standard library only,
except that actual COM ports require pyserial. No automatic write retries.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import struct
import time

VERSION, MAX_PAYLOAD, MAX_BATCH = 1, 64, 6
IDENTIFY, BROWSE, READ, WRITE, ERROR = 1, 2, 3, 4, 255
FORMATS = {1: "?", 2: "B", 3: "b", 4: "B", 5: "h", 6: "H", 7: "i", 8: "I", 9: "f"}
TYPE_NAMES = {1: "bool", 2: "char", 3: "int8", 4: "uint8", 5: "int16", 6: "uint16", 7: "int32", 8: "uint32", 9: "float32"}
STATUS_NAMES = {0: "OK", 1: "BAD_REQUEST", 2: "UNKNOWN_TARGET", 3: "NOT_WRITABLE", 4: "TYPE_MISMATCH", 5: "BAD_VALUE", 6: "BUSY", 7: "UNSUPPORTED", 8: "NOT_READY", 9: "BAD_CURSOR"}


class ProtocolError(Exception):
    pass


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


@dataclass(frozen=True)
class Packet:
    operation: int
    request: int
    payload: bytes


def encode(operation: int, request: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("Payload exceeds 64 bytes")
    body = bytes((VERSION, operation, 0, request, len(payload))) + payload
    body += struct.pack("<H", crc16(body))
    wire = bytearray((0x7E,))
    for byte in body:
        if byte in (0x7E, 0x7D):
            wire.extend((0x7D, byte ^ 0x20))
        else:
            wire.append(byte)
    wire.append(0x7E)
    return bytes(wire)


class Decoder:
    """Bounded incremental frame decoder; corrupt frames are silently discarded."""
    def __init__(self):
        self.body = bytearray()
        self.open = False
        self.escaped = False
        self.discard = False

    def feed(self, data: bytes) -> list[Packet]:
        packets = []
        for byte in data:
            if byte == 0x7E:
                b = self.body
                if (self.open and not self.discard and not self.escaped
                        and len(b) >= 7 and b[0] == VERSION and b[2] == 0
                        and b[4] <= MAX_PAYLOAD and len(b) == b[4] + 7
                        and crc16(b[:-2]) == int.from_bytes(b[-2:], "little")):
                    packets.append(Packet(b[1], b[3], bytes(b[5:-2])))
                self.body = bytearray()
                self.open, self.escaped, self.discard = True, False, False
                continue
            if not self.open or self.discard:
                continue
            if byte == 0x7D and not self.escaped:
                self.escaped = True
                continue
            if self.escaped:
                if byte not in (0x5E, 0x5D):
                    self.discard = True
                    continue
                byte ^= 0x20
                self.escaped = False
            if len(self.body) >= MAX_PAYLOAD + 7:
                self.discard = True
                continue
            self.body.append(byte)
            if len(self.body) == 5 and self.body[4] > MAX_PAYLOAD:
                self.discard = True
        return packets


@dataclass(frozen=True)
class Tag:
    reference: int
    property: int
    type: int
    writable: bool
    name: str

    @property
    def target(self):
        return self.reference, self.property


def pack_value(type_code: int, value) -> bytes:
    if type_code not in FORMATS:
        raise ValueError("Unsupported scalar type")
    if type_code == 1 and not isinstance(value, bool):
        raise ValueError("Boolean values must be true or false")
    if type_code == 2 and isinstance(value, str):
        if len(value) != 1 or ord(value) > 255:
            raise ValueError("Character must fit one byte")
        value = ord(value)
    return struct.pack("<" + FORMATS[type_code], value)


def unpack_value(type_code: int, data: bytes):
    if type_code not in FORMATS or len(data) != struct.calcsize("<" + FORMATS[type_code]):
        raise ProtocolError("Invalid scalar representation")
    if type_code == 1 and data[0] not in (0, 1):
        raise ProtocolError("Invalid boolean representation")
    value = struct.unpack("<" + FORMATS[type_code], data)[0]
    return chr(value) if type_code == 2 else value


class Client:
    """channel.write(bytes) and channel.read(size) must have finite timeouts.
    One outstanding request. After a timeout reopen/reset the connection.
    """
    def __init__(self, channel, timeout: float = 2.0):
        self.channel = channel
        self.timeout = timeout
        self.decoder = Decoder()
        self.request_id = 0
        self.synchronized = True

    def request(self, operation: int, payload: bytes = b"") -> bytes:
        if not self.synchronized:
            raise ProtocolError("Connection outcome uncertain; reopen/reset before more requests")
        self.request_id = (self.request_id + 1) & 0xFF
        wire = encode(operation, self.request_id, payload)
        try:
            if self.channel.write(wire) != len(wire):
                raise TimeoutError("Incomplete request transmission")
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                for packet in self.decoder.feed(self.channel.read(1)):
                    if packet.request != self.request_id:
                        continue
                    if packet.operation == ERROR:
                        if len(packet.payload) != 1:
                            raise ProtocolError("Malformed error response")
                        raise ProtocolError(STATUS_NAMES.get(packet.payload[0], "Unknown device error"))
                    if packet.operation != operation | 0x80:
                        raise ProtocolError("Unexpected response operation")
                    return packet.payload
            raise TimeoutError("Response timeout; write may already have taken effect")
        except (TimeoutError, OSError):
            self.synchronized = False
            raise

    def identify(self) -> dict:
        p = self.request(IDENTIFY)
        if len(p) < 11 or len(p) != 11 + p[10]:
            raise ProtocolError("Malformed identity response")
        return {"initialization_status": p[0], "tag_count": int.from_bytes(p[1:3], "little"),
                "catalog_revision": int.from_bytes(p[3:7], "little"), "max_payload": p[7],
                "max_batch": p[8], "max_tags": p[9], "device_name": p[11:].decode("utf-8")}

    def browse(self) -> list[Tag]:
        tags, seen, cursor = [], set(), 0
        while cursor != 0xFFFF:
            if cursor in seen or len(seen) >= 261:
                raise ProtocolError("Invalid browse continuation")
            seen.add(cursor)
            p = self.request(BROWSE, struct.pack("<H", cursor))
            if len(p) < 8 or len(p) != 8 + p[7]:
                raise ProtocolError("Malformed definition")
            cursor, ref, prop, typ, access, _ = struct.unpack("<HHBBBB", p[:8])
            if typ not in FORMATS or access not in (0, 1):
                raise ProtocolError("Unsupported definition")
            tags.append(Tag(ref, prop, typ, access == 1, p[8:].decode("utf-8")))
        return tags

    def read(self, targets: list[tuple[int, int]]) -> list[dict]:
        if not 1 <= len(targets) <= MAX_BATCH:
            raise ValueError("Read 1..6 targets per request")
        payload = bytes((len(targets),)) + b"".join(struct.pack("<HB", *t) for t in targets)
        p = self.request(READ, payload)
        if not p or p[0] != len(targets):
            raise ProtocolError("Invalid result count")
        results, pos = [], 1
        for target in targets:
            if len(p) < pos + 6:
                raise ProtocolError("Truncated read result")
            ref, prop, status, typ, length = struct.unpack("<HBBBB", p[pos:pos + 6])
            pos += 6
            if (ref, prop) != target or pos + length > len(p):
                raise ProtocolError("Invalid read result")
            value = unpack_value(typ, p[pos:pos + length]) if status == 0 else None
            if status != 0 and (typ or length):
                raise ProtocolError("Invalid failed-item encoding")
            results.append({"reference": ref, "property": prop, "status": status, "value": value})
            pos += length
        if pos != len(p):
            raise ProtocolError("Trailing read bytes")
        return results

    def write(self, items: list[tuple[int, int, int, object]]) -> list[dict]:
        if not 1 <= len(items) <= MAX_BATCH:
            raise ValueError("Write 1..6 items per request")
        payload = bytearray((len(items),))
        for ref, prop, typ, value in items:
            data = pack_value(typ, value)
            payload.extend(struct.pack("<HBBB", ref, prop, typ, len(data)))
            payload.extend(data)
        p = self.request(WRITE, bytes(payload))
        if len(p) != 1 + 4 * len(items) or p[0] != len(items):
            raise ProtocolError("Invalid write result")
        results = []
        for i, item in enumerate(items):
            ref, prop, status = struct.unpack_from("<HBB", p, 1 + 4 * i)
            if (ref, prop) != item[:2]:
                raise ProtocolError("Mismatched write target")
            results.append({"reference": ref, "property": prop, "status": status})
        return results


def parse_target(text):
    parts = text.split(":")
    return int(parts[0]), int(parts[1]) if len(parts) == 2 else 0


def main():
    parser = argparse.ArgumentParser(description="LightYak v1 serial reference client")
    parser.add_argument("--port", required=True, help="e.g. COM4 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--parity", choices=("N", "E", "O"), default="N")
    parser.add_argument("--stopbits", type=int, choices=(1, 2), default=1)
    parser.add_argument("--startup-wait", type=float, default=2.0, help="Allow Arduino auto-reset to finish")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("identify")
    sub.add_parser("browse")
    read = sub.add_parser("read")
    read.add_argument("targets", nargs="+", help="ref or ref:property; at most six")
    write = sub.add_parser("write")
    write.add_argument("target")
    write.add_argument("value", help='JSON scalar, e.g. 1500, true, or \'"A"\'')
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        parser.error("COM port access needs pyserial: python -m pip install pyserial")
    try:
        with serial.Serial(args.port, args.baud, parity=args.parity, stopbits=args.stopbits,
                           timeout=0.02, write_timeout=2) as port:
            time.sleep(args.startup_wait)
            port.reset_input_buffer()
            client = Client(port)
            identity = client.identify()
            if identity["initialization_status"]:
                raise ProtocolError("Device initialization failed: " + str(identity))
            if args.command == "identify":
                result = identity
            elif args.command == "browse":
                result = [dict(reference=t.reference, property=t.property, name=t.name,
                               type=TYPE_NAMES[t.type], writable=t.writable) for t in client.browse()]
            elif args.command == "read":
                result = client.read([parse_target(t) for t in args.targets])
            else:
                target = parse_target(args.target)
                tag = next((t for t in client.browse() if t.target == target), None)
                if tag is None or not tag.writable:
                    raise ProtocolError("Target is missing or read-only")
                result = client.write([(*target, tag.type, json.loads(args.value))])
            print(json.dumps(result, indent=2))
    except (ProtocolError, TimeoutError, OSError, ValueError, struct.error) as exc:
        parser.exit(1, str(exc) + "\n")


if __name__ == "__main__":
    main()
