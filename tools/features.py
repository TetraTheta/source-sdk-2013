"""Discover carry-sprint, achievement, and console-dispatch edits in retail code."""

import json
import struct
from pathlib import Path

from pe_study import PE, occurrences, scan


def command_registration(pe: PE, name: str, callback: int) -> dict[str, int]:
    """Bind a command to its literal name and reviewed old-style callback."""
    registrations = []
    for at in occurrences(pe.data, (name + "\0").encode()):
        name_rva = pe.rva(at)
        prefix = b"\x68" + struct.pack("<I", pe.image_base + callback)
        prefix += b"\x68" + struct.pack("<I", pe.image_base + name_rva) + b"\xb9"
        for ref in occurrences(pe.data, prefix):
            if ref + len(prefix) + 5 > len(pe.data) or pe.data[ref + len(prefix) + 4] != 0xE8:
                continue
            object_rva = pe.u32(ref + len(prefix)) - pe.image_base
            if not 0 <= object_rva <= pe.image_size - 36:
                raise ValueError(f"{name}: command object outside image")
            registrations.append(
                {
                    "object": object_rva,
                    "name": name_rva,
                    "callback": callback,
                }
            )
    if len(registrations) != 1:
        raise ValueError(f"{name}: command registration changed")
    return registrations[0]


def discover(pe: PE) -> tuple[list[dict[str, object]], list[dict[str, object]], dict]:
    config = json.loads((Path(__file__).resolve().parents[1] / "profiles/feature-patterns.json").read_text(encoding="utf-8"))
    targets = []
    patches = []
    addresses = {}
    for template in config["targets"]:
        matches = [
            s["rva"] + at
            for s in pe.sections
            if s["flags"] & 0x20000000
            for at in scan(pe.data[s["raw"] : s["raw"] + s["size"]], template["pattern"])
        ]
        if len(matches) != 1:
            raise ValueError(f"{template['name']}: expected one signature, got {len(matches)}")
        rva = matches[0]
        addresses[template["name"]] = rva
        table_rva = 0
        slot = template.get("slot", 0)
        if "class" in template:
            tables = pe.vtables(template["class"])
            if len(tables) != 1 or int(tables[0]["entries"][slot], 16) != rva:
                raise ValueError("Console dispatch virtual slot changed")
            table_rva = int(tables[0]["rva"], 16)
        targets.append(
            {
                "name": template["name"],
                "rva": rva,
                "pattern": template["pattern"],
                "vtable": table_rva,
                "slot": slot,
            }
        )
        if "replacement" in template:
            operand = rva + template["edit_offset"]
            replacement = bytes.fromhex(template["replacement"])
            if len(replacement) != 4:
                raise ValueError("Feature edits must be one complete patch word")
            patches.append(
                {
                    "rva": operand,
                    "kind": "Scalar",
                    "before": pe.u32(pe.raw(operand)),
                    "after": struct.unpack("<I", replacement)[0],
                }
            )
    return (
        targets,
        patches,
        {
            "god": command_registration(pe, "god", addresses["Commands.God"]),
            "noclip": command_registration(pe, "noclip", addresses["Commands.Noclip"]),
            "dispatch_table": next(t["vtable"] for t in targets if t["name"] == "Commands.Dispatch"),
            "dispatch": addresses["Commands.Dispatch"],
            "get_player": addresses["Commands.GetPlayer"],
            # These operands are pinned by the God/Noclip signatures above.
            "player_flags": 0x104,
            "player_movetype": 0x13A,
        },
    )
