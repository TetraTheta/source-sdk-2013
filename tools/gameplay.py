"""Discover constructor flag edits and the player AR2 ball-speed operand."""

import json
import struct
from pathlib import Path

from pe_study import PE, occurrences, scan


def constructor_edits(code: bytes, target: dict[str, object], field: int) -> list[tuple[int, bytes]]:
    """Enable flags at construction, preserving the reviewed factory error branch."""
    offset = target["operand_offset"]
    kind = target["kind"]
    primary = b"\xc6\x86" + struct.pack("<I", field) + b"\x01"
    alternate = b"\xc6\x86" + struct.pack("<I", field + 1) + b"\x01"
    if kind == "primary_flag":
        if code[offset - 3 : offset + 4] != primary[:-1] + b"\x00":
            raise ValueError(".357 primary flag initialization changed")
        return [(offset, code[offset : offset + 3] + b"\x01")]
    if kind == "append_flags":
        if code[offset:] != b"\x5e\xc3":
            raise ValueError("Shotgun constructor epilogue changed")
        tail = primary + alternate + code[offset:]
    elif kind == "expand_flags":
        if code[offset : offset + 7] != alternate[:-1] + b"\x00":
            raise ValueError("Alternate flag initialization changed")
        tail = primary + alternate + code[offset + 7 :]
    else:
        raise ValueError("Unknown constructor edit")
    tail += b"\xcc" * (-len(tail) % 4)
    result = [(offset, tail)]
    if "jump_displacement_offset" in target:
        jump = target["jump_displacement_offset"]
        if code[jump - 1] != 0x74 or not offset + 7 <= jump + 1 + code[jump] < len(code) or code[jump] + 7 > 127:
            raise ValueError("SMG allocation-failure branch changed")
        result.append((jump, bytes([code[jump] + 7]) + code[jump + 1 : jump + 4]))
    return result


def field_offset(pe: PE, name: str) -> int:
    """Resolve the retail datamap field instead of trusting SDK entity layout."""
    offsets = set()
    for at in occurrences(pe.data, (name + "\0").encode()):
        pointer = pe.image_base + pe.rva(at)
        for ref in occurrences(pe.data, struct.pack("<I", pointer)):
            if ref >= 4 and pe.u32(ref - 4) == 6:
                offsets.add(pe.u32(ref + 4))
    if len(offsets) != 1:
        raise ValueError(f"{name}: ambiguous or absent boolean datamap")
    return offsets.pop()


def discover(pe: PE) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    config = json.loads((Path(__file__).resolve().parents[1] / "profiles/gameplay-patterns.json").read_text(encoding="utf-8"))
    field = field_offset(pe, "m_bFiresUnderwater")
    if field != config["underwater_field"] or field_offset(pe, "m_bAltFiresUnderwater") != field + 1:
        raise ValueError("Underwater field layout changed; review constructors")
    targets = []
    patches = []
    for target in config["targets"]:
        constructor = target["kind"] in {"primary_flag", "append_flags", "expand_flags"}
        class_vtable = 0
        if constructor:
            tables = pe.vtables(target["class"])
            if len(tables) != 1:
                raise ValueError("Ambiguous constructor class")
            class_vtable = int(tables[0]["rva"], 16)
        matches = [
            s["rva"] + at for s in pe.sections if s["flags"] & 0x20000000 for at in scan(pe.data[s["raw"] : s["raw"] + s["size"]], target["pattern"])
        ]
        if constructor:
            marker = b"\xc7\x06" + struct.pack("<I", pe.image_base + class_vtable)
            matches = [r for r in matches if marker in pe.data[pe.raw(r) : pe.raw(r) + target["length"]]]
        if len(matches) != 1:
            raise ValueError(f"{target['name']}: expected one match, found {len(matches)}")
        entry = matches[0]
        vtable = 0
        slot = target["slot"]
        if slot is not None:
            tables = pe.vtables(target["class"])
            if len(tables) != 1 or int(tables[0]["entries"][slot], 16) != entry:
                raise ValueError(f"{target['name']}: vtable changed")
            vtable = int(tables[0]["rva"], 16)
        targets.append(
            {
                "name": target["name"],
                "rva": entry,
                "pattern": target["pattern"],
                "vtable": vtable,
                "slot": slot or 0,
            }
        )
        if constructor:
            targets[-1]["class_vtable"] = class_vtable
            raw = pe.raw(entry)
            code = pe.data[raw : raw + target["length"]]
            tables = pe.vtables(target["class"])
            if len(tables) != 1 or b"\xc7\x06" + struct.pack("<I", pe.image_base + int(tables[0]["rva"], 16)) not in code:
                raise ValueError("Constructor no longer installs the expected class vtable")
            for offset, replacement in constructor_edits(code, target, field):
                padding = max(0, offset + len(replacement) - len(code))
                if pe.data[raw + len(code) : raw + len(code) + padding] != b"\xcc" * padding:
                    raise ValueError("Constructor expansion overlaps live code")
                for i in range(0, len(replacement), 4):
                    patches.append(
                        {
                            "rva": entry + offset + i,
                            "kind": "Scalar",
                            "before": pe.u32(raw + offset + i),
                            "after": struct.unpack_from("<I", replacement, i)[0],
                        }
                    )
        elif target["kind"] == "water_compare":
            operand = entry + target["operand_offset"]
            raw = pe.raw(operand)
            # Water levels are 0..3; compare to 4 only in these two secondary attacks.
            if pe.data[raw - 3] != 0x80 or pe.data[raw + 3] != 3:
                raise ValueError("Secondary water check changed")
            patches.append(
                {
                    "rva": operand,
                    "kind": "Scalar",
                    "before": pe.u32(raw),
                    "after": (pe.u32(raw) & 0xFFFFFF) | 0x04000000,
                }
            )
        elif target["kind"] == "ball_speed":
            operand = entry + target["operand_offset"]
            raw = pe.raw(operand)
            original = pe.u32(raw) - pe.image_base
            if pe.data[raw - 4 : raw] != b"\xf3\x0f\x10\x0d" or pe.u32(pe.raw(original)) != 0x447A0000:
                raise ValueError("AR2 velocity multiplier changed")
            constants = [
                s["rva"] + i for s in pe.sections if s["name"] == ".rdata" for i in range(0, s["size"] - 3, 4) if pe.u32(s["raw"] + i) == 0x44FA0000
            ]
            if not constants:
                raise ValueError("No immutable 2000.0 constant")
            patches.append(
                {
                    "rva": operand,
                    "kind": "Pointer",
                    "before": original,
                    "after": constants[0],
                }
            )
        else:
            raise ValueError("Unknown gameplay patch kind")
    return targets, patches
