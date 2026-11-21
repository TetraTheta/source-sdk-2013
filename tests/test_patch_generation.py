"""Check x86 constructor patch generation when updating reviewed signatures."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from gameplay import constructor_edits


class ConstructorTests(unittest.TestCase):
    def test_existing_alt_store_expands_into_two_flag_stores(self) -> None:
        primary = b"\xc6\x86" + struct.pack("<I", 0x4B4) + b"\x01"
        alternate = b"\xc6\x86" + struct.pack("<I", 0x4B5) + b"\x01"
        original = alternate[:-1] + b"\x00\x5e\xc3"
        edits = constructor_edits(original, {"kind": "expand_flags", "operand_offset": 0}, 0x4B4)
        self.assertEqual(edits, [(0, primary + alternate + b"\x5e\xc3")])

    def test_shotgun_appends_before_epilogue(self) -> None:
        edits = constructor_edits(b"\x90\x5e\xc3", {"kind": "append_flags", "operand_offset": 1}, 0x4B4)
        self.assertEqual(len(edits[0][1]), 16)
        self.assertEqual(edits[0][1][-2:], b"\x5e\xc3")

    def test_smg_null_branch_moves_with_expanded_tail(self) -> None:
        code = b"\x74\x0e" + bytes(5) + b"\xc6\x86\xb5\x04\x00\x00\x00" + b"\x5e\xc3\x90\x90"
        edits = constructor_edits(
            code,
            {
                "kind": "expand_flags",
                "operand_offset": 7,
                "jump_displacement_offset": 1,
            },
            0x4B4,
        )
        self.assertEqual(edits[1][1][0], 21)

    def test_changed_initializer_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            constructor_edits(bytes(9), {"kind": "expand_flags", "operand_offset": 0}, 0x4B4)


if __name__ == "__main__":
    unittest.main()
