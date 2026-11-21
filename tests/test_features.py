"""Small PE fixtures exercise registration and movement-discovery rejection paths."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from features import command_registration
from movement import locate
from pe_study import PE
from retail import field_offset


class DiscoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "fixture.dll"
        data = bytearray(0x600)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3C, 0x80)
        data[0x80:0x84] = b"PE\0\0"
        struct.pack_into("<HH", data, 0x84, 0x14C, 2)
        struct.pack_into("<H", data, 0x94, 0xE0)
        struct.pack_into("<H", data, 0x98, 0x10B)
        struct.pack_into("<I", data, 0x98 + 28, 0x10000000)
        struct.pack_into("<I", data, 0x98 + 56, 0x4000)
        for i, (name, rva, raw, flags) in enumerate(
            (
                (b".text", 0x1000, 0x200, 0x60000020),
                (b".data", 0x2000, 0x400, 0xC0000040),
            )
        ):
            at = 0x178 + 40 * i
            data[at : at + len(name)] = name
            struct.pack_into("<IIII", data, at + 8, 0x200, rva, 0x200, raw)
            struct.pack_into("<I", data, at + 36, flags)
        data[0x450:0x454] = b"god\0"
        data[0x300:0x304] = b"\x55\x8b\xec\xc3"
        registration = b"\x68" + struct.pack("<I", 0x10001100)
        registration += b"\x68" + struct.pack("<I", 0x10002050)
        registration += b"\xb9" + struct.pack("<I", 0x10002100) + b"\xe8\0\0\0\0"
        data[0x220 : 0x220 + len(registration)] = registration
        struct.pack_into("<I", data, 0x480, 0x10001100)
        self.data = data

    def image(self) -> PE:
        self.path.write_bytes(self.data)
        return PE(self.path)

    def test_retail_field_static_and_initializer(self) -> None:
        self.data[0x460:0x469] = b"m_iClip2\0"
        struct.pack_into("<III", self.data, 0x4A0, 5, 0x10002060, 0x4B0)
        self.assertEqual(field_offset(self.image(), "m_iClip2", 5), 0x4B0)
        struct.pack_into("<III", self.data, 0x4A0, 0, 0, 0)
        stores = b""
        for destination, value in (
            (0x10002100, 5),
            (0x10002104, 0x10002060),
            (0x10002108, 0x4B0),
        ):
            stores += b"\xc7\x05" + struct.pack("<II", destination, value)
        self.data[0x340 : 0x340 + len(stores)] = stores
        self.assertEqual(field_offset(self.image(), "m_iClip2", 5), 0x4B0)
        struct.pack_into("<III", self.data, 0x4A0, 5, 0x10002060, 0x4AC)
        with self.assertRaises(ValueError):
            field_offset(self.image(), "m_iClip2", 5)

    def test_command_is_bound_to_both_name_and_callback(self) -> None:
        self.assertEqual(
            command_registration(self.image(), "god", 0x1100),
            {"object": 0x2100, "name": 0x2050, "callback": 0x1100},
        )
        with self.assertRaises(ValueError):
            command_registration(self.image(), "god", 0x1101)

    def test_duplicate_registration_rejected(self) -> None:
        self.data[0x260:0x274] = self.data[0x220:0x234]
        with self.assertRaises(ValueError):
            command_registration(self.image(), "god", 0x1100)

    def test_out_of_image_object_rejected(self) -> None:
        struct.pack_into("<I", self.data, 0x22B, 0x20000000)
        with self.assertRaises(ValueError):
            command_registration(self.image(), "god", 0x1100)

    def test_movement_requires_unique_function_and_matching_slot(self) -> None:
        self.assertEqual(locate(self.image(), "crop", "55 8B EC C3", 0x2080)["rva"], 0x1100)
        struct.pack_into("<I", self.data, 0x480, 0)
        with self.assertRaises(ValueError):
            locate(self.image(), "crop", "55 8B EC C3", 0x2080)
        self.data[0x330:0x334] = self.data[0x300:0x304]
        with self.assertRaises(ValueError):
            locate(self.image(), "crop", "55 8B EC C3")


if __name__ == "__main__":
    unittest.main()
