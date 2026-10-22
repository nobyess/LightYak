# SPDX-License-Identifier: MIT
import pathlib
import random
import struct
import subprocess
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import lightyak_client as ly


class CodecTests(unittest.TestCase):
    def test_crc_golden(self):
        self.assertEqual(ly.crc16(b"123456789"), 0x29B1)

    def test_every_byte_and_fragment_boundary(self):
        for value in range(256):
            wire = ly.encode(ly.READ, value, bytes([value]))
            decoder, packets = ly.Decoder(), []
            for byte in wire:
                packets.extend(decoder.feed(bytes([byte])))
            self.assertEqual(packets, [ly.Packet(ly.READ, value, bytes([value]))])

    def test_maximum_payload(self):
        self.assertEqual(ly.Decoder().feed(ly.encode(1, 2, b"}" * 64))[0].payload, b"}" * 64)
        with self.assertRaises(ValueError):
            ly.encode(1, 2, b"x" * 65)

    def test_corruption_and_recovery(self):
        bad = bytearray(ly.encode(1, 1))
        bad[-2] ^= 1
        decoder = ly.Decoder()
        self.assertEqual(decoder.feed(bad), [])
        self.assertEqual(decoder.feed(ly.encode(1, 2)), [ly.Packet(1, 2, b"")])

    def test_decoder_memory_bound(self):
        decoder = ly.Decoder()
        decoder.feed(b"~" + b"x" * 100000)
        self.assertLessEqual(len(decoder.body), 71)
        self.assertEqual(decoder.feed(ly.encode(1, 3)), [ly.Packet(1, 3, b"")])

    def test_malformed_escapes(self):
        decoder = ly.Decoder()
        self.assertEqual(decoder.feed(b"~\x01}\x00~"), [])
        self.assertEqual(decoder.feed(ly.encode(1, 4)), [ly.Packet(1, 4, b"")])

    def test_scalar_values(self):
        cases = [(1, True), (2, "Z"), (3, -128), (4, 255), (5, -32768),
                 (6, 65535), (7, -(2**31)), (8, 2**32 - 1), (9, 22.5)]
        for typ, value in cases:
            self.assertEqual(ly.unpack_value(typ, ly.pack_value(typ, value)), value)
        with self.assertRaises(ValueError):
            ly.pack_value(1, 2)
        with self.assertRaises(struct.error):
            ly.pack_value(4, 256)
        with self.assertRaises(ly.ProtocolError):
            ly.unpack_value(1, b"\x02")

    def test_timeout_requires_reconnect_and_never_retries(self):
        class Silent:
            writes = 0
            def write(self, b):
                self.writes += 1
                return len(b)
            def read(self, n):
                return b""
        channel = Silent()
        client = ly.Client(channel, timeout=0.001)
        with self.assertRaises(TimeoutError):
            client.write([(1, 0, 1, True)])
        with self.assertRaises(ly.ProtocolError):
            client.identify()
        self.assertEqual(channel.writes, 1)

    def test_partial_transmission_invalidates_connection(self):
        class Partial:
            def write(self, b):
                return 1
        client = ly.Client(Partial())
        with self.assertRaises(TimeoutError):
            client.identify()
        self.assertFalse(client.synchronized)


class SimulatedChannel:
    def __init__(self, executable):
        self.process = subprocess.Popen([executable], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        text=True, bufsize=1)
        self.pending = bytearray()
        self.now = 0

    def write(self, b):
        self.process.stdin.write(f"{self.now} {b.hex()}\n")
        self.process.stdin.flush()
        wire = self.process.stdout.readline()
        if wire == "":
            raise OSError("Native simulator exited")
        self.pending.extend(bytes.fromhex(wire.strip()))
        return len(b)

    def read(self, n):
        result = bytes(self.pending[:n])
        del self.pending[:n]
        return result

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=5)
        self.process.stdout.close()
        if self.process.returncode:
            raise RuntimeError("Native simulator failed")


class InteroperabilityTests(unittest.TestCase):
    def setUp(self):
        if len(sys.argv) < 2:
            self.skipTest("Native simulator path not supplied")
        self.channel = SimulatedChannel(sys.argv[1])
        self.client = ly.Client(self.channel)

    def tearDown(self):
        if hasattr(self, "channel"):
            self.channel.close()

    def test_discover_browse_poll_write(self):
        identity = self.client.identify()
        self.assertEqual(identity["device_name"], "Simulated Uno")
        self.assertEqual(identity["tag_count"], 3)
        tags = self.client.browse()
        self.assertEqual(len(tags), 9)
        self.assertEqual(tags[-1].name, "Temperature")
        self.assertFalse(tags[-1].writable)
        self.assertTrue(tags[-2].writable)
        values = self.client.read([(1, 0), (2, 0), (3, 0)])
        self.assertEqual([x["value"] for x in values], [False, 1200, 22.5])
        self.assertEqual(self.client.write([(1, 0, 1, True), (2, 0, 5, 1500)])[1]["status"], 0)
        self.assertEqual([x["value"] for x in self.client.read([(1, 0), (2, 0)])], [True, 1500])

    def test_individual_failure_results(self):
        self.assertEqual(self.client.write([(3, 0, 9, 10.0)])[0]["status"], 3)
        self.assertEqual(self.client.read([(99, 0)])[0]["status"], 2)
        self.assertEqual(self.client.write([(2, 0, 6, 10)])[0]["status"], 4)

    def test_heartbeat_and_request_id_wrap(self):
        for n in range(300):
            self.channel.now = n * 100
            self.assertEqual(self.client.read([(0, 1)])[0]["value"], bool((n * 100 // 500) & 1))

    def test_random_signed_writes(self):
        rng = random.Random(17)
        for _ in range(100):
            value = rng.randint(-32768, 32767)
            self.assertEqual(self.client.write([(2, 0, 5, value)])[0]["status"], 0)
            self.assertEqual(self.client.read([(2, 0)])[0]["value"], value)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
