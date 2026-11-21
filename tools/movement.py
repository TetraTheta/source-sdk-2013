"""Locate matching server/client crouch crop paths and the live normal-speed ConVar."""

import json
import struct
from pathlib import Path

from pe_study import PE, occurrences, scan


def locate(pe: PE, name: str, pattern: str, table: int = 0, slot: int = 0) -> dict:
    matches = [s["rva"] + at for s in pe.sections if s["flags"] & 0x20000000 for at in scan(pe.data[s["raw"] : s["raw"] + s["size"]], pattern)]
    if len(matches) != 1:
        raise ValueError(f"{name}: missing/ambiguous movement signature")
    if table and pe.u32(pe.raw(table + slot * 4)) != pe.image_base + matches[0]:
        raise ValueError(f"{name}: movement virtual slot changed")
    return {
        "name": name,
        "rva": matches[0],
        "pattern": pattern,
        "vtable": table,
        "slot": slot,
    }


def discover(server: PE, client: PE) -> tuple[dict, list[dict]]:
    config = json.loads((Path(__file__).resolve().parents[1] / "profiles/movement-patterns.json").read_text(encoding="utf-8"))
    result = {}
    for name, pe in (("server", server), ("client", client)):
        template = config[name]
        tables = pe.vtables(template["class"])
        if len(tables) != 1:
            raise ValueError(f"{name}: ambiguous HL2 movement class")
        result[name + "_crop"] = locate(
            pe,
            name + ".CrouchCrop",
            template["pattern"],
            int(tables[0]["rva"], 16),
            template["slot"],
        )
        result[name + "_sprint_probe"] = locate(pe, name + ".MoveSprintField", template["sprint_probe"])
    registrations = []
    for at in occurrences(server.data, b"hl2_normspeed\0"):
        name_rva = server.rva(at)
        for ref in occurrences(server.data, b"\x68" + struct.pack("<I", server.image_base + name_rva)):
            if (
                ref >= 7
                and ref + 11 <= len(server.data)
                and (server.data[ref - 7 : ref - 4] == b"\x6a\x00\x68" and server.data[ref + 5] == 0xB9 and server.data[ref + 10] == 0xE8)
            ):
                registrations.append((server.u32(ref + 6) - server.image_base, name_rva))
    if len(registrations) != 1:
        raise ValueError("Normal-speed ConVar registration changed")
    obj, name = registrations[0]
    result.update(
        {
            "norm_speed": obj,
            "norm_name": name,
            "movement_move": 8,
            "movement_cropped": 0x688,
            "move_sprint": 0xA4,
            "move_inputs": 0x2C,
            "move_maxspeed": 0x3C,
        }
    )
    return result, [
        {"rva": obj + 12, "kind": "Pointer", "before": name, "after": name},
        {"rva": obj + 28, "kind": "Pointer", "before": obj, "after": obj},
    ]
