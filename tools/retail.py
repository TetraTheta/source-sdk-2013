"""Discover reviewed retail APIs and datamap operands for player tools."""

import hashlib
import json
import struct
from pathlib import Path

from pe_study import PE, occurrences, scan

CONFIG = Path(__file__).resolve().parents[1] / "profiles/retail-patterns.json"


def field_offset(pe: PE, name: str, field_type: int) -> int:
    """Read either a static descriptor or its compiler-generated initializer stores."""
    offsets = set()
    for at in occurrences(pe.data, (name + "\0").encode()):
        pointer = pe.image_base + pe.rva(at)
        for ref in occurrences(pe.data, struct.pack("<I", pointer)):
            if ref >= 4 and pe.u32(ref - 4) == field_type:
                offsets.add(pe.u32(ref + 4))
            if ref < 6 or pe.data[ref - 6 : ref - 4] != b"\xc7\x05":
                continue
            descriptor = pe.u32(ref - 4) - 4
            offset_stores = occurrences(pe.data, b"\xc7\x05" + struct.pack("<I", descriptor + 8))
            type_stores = occurrences(pe.data, b"\xc7\x05" + struct.pack("<I", descriptor))
            if not any(pe.u32(x + 6) == field_type for x in type_stores):
                continue
            offsets.update(pe.u32(x + 6) for x in offset_stores)
    if len(offsets) != 1 or not 0 < next(iter(offsets)) < 0x10000:
        raise ValueError(f"{name}: ambiguous/invalid retail field offset {offsets}")
    return offsets.pop()


def targets(pe: PE, templates: list[dict]) -> list[dict]:
    result = []
    for t in templates:
        matches = [
            s["rva"] + at for s in pe.sections if s["flags"] & 0x20000000 for at in scan(pe.data[s["raw"] : s["raw"] + s["size"]], t["pattern"])
        ]
        if len(matches) != 1:
            raise ValueError(f"{t['name']}: missing/ambiguous retail API signature ({len(matches)})")
        result.append(
            {
                "name": t["name"],
                "rva": matches[0],
                "pattern": t["pattern"],
                "vtable": 0,
                "slot": 0,
            }
        )
    return result


def secondary_tables(pe: PE, cls: str) -> list[dict]:
    """Read non-primary MSVC COL offsets instead of assuming an SDK base size."""
    result = []
    for at in occurrences(pe.data, (".?AV" + cls + "@@\0").encode()):
        descriptor = pe.image_base + pe.rva(at - 8)
        for ref in occurrences(pe.data, struct.pack("<I", descriptor)):
            col = ref - 12
            if col < 0 or pe.u32(col) or pe.u32(col + 8) or not pe.u32(col + 4):
                continue
            for pointer in occurrences(pe.data, struct.pack("<I", pe.image_base + pe.rva(col))):
                if pe.executable(pe.u32(pointer + 4)):
                    result.append({"offset": pe.u32(col + 4), "table": pe.rva(pointer + 4)})
    return result


def client_cleanup(pe: PE) -> dict:
    interfaces = [t for t in secondary_tables(pe, "CClientEntityList") if t["offset"] > 0x10000]
    lists = pe.vtables("CClientEntityList")
    ragdolls = pe.vtables("C_ClientRagdoll")
    creators = []
    for at in occurrences(pe.data, b"VClientEntityList003\0"):
        for ref in occurrences(pe.data, struct.pack("<I", pe.image_base + pe.rva(at))):
            if pe.data[ref - 1] != 0x68 or pe.data[ref + 4] != 0x68 or pe.data[ref + 9] != 0xB9:
                continue
            creator = pe.u32(ref + 5) - pe.image_base
            raw = pe.raw(creator)
            if pe.data[raw] == 0xB8 and pe.data[raw + 5] == 0xC3:
                creators.append((creator, pe.u32(raw + 1) - pe.image_base))
    if len(interfaces) != 1 or len(lists) != 1 or len(ragdolls) != 1 or len(creators) != 1:
        raise ValueError("Client ragdoll/entity-list identity changed")
    return {
        "list_primary": int(lists[0]["rva"], 16),
        "list_secondary": interfaces[0]["table"],
        "list_offset": interfaces[0]["offset"],
        "list_instance": creators[0][1] - interfaces[0]["offset"],
        "ragdoll_table": int(ragdolls[0]["rva"], 16),
        "release_field": field_offset(pe, "m_bReleaseRagdoll", 6),
    }


def discover(server: PE, client: PE, engine: PE, vstdlib: PE) -> dict:
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    server_targets = targets(server, config["server"])
    functions = {t["name"]: t["rva"] for t in server_targets}
    values = {}
    for key, probe in config["operands"].items():
        values[key] = server.u32(server.raw(functions[probe["function"]] + probe["offset"])) - server.image_base
        if not 0 <= values[key] < server.image_size:
            raise ValueError(f"{key}: pointer operand outside server image")
    maps = {}
    for cls in ["CBaseEntity", "CBaseHLCombatWeapon", "CHL2_Player"]:
        tables = server.vtables(cls)
        if len(tables) != 1:
            raise ValueError(f"{cls}: ambiguous primary vtable")
        getter = int(tables[0]["entries"][11], 16)
        raw = server.raw(getter)
        if server.data[raw] != 0xB8 or server.data[raw + 5] != 0xC3:
            raise ValueError(f"{cls}: datamap getter changed")
        maps[cls] = {
            "table": int(tables[0]["rva"], 16),
            "map": server.u32(raw + 1) - server.image_base,
        }
    cvars = vstdlib.vtables("CCvar")
    models = engine.vtables("CModelRender")
    if len(models) != 1:
        raise ValueError("Ambiguous model-render implementation")
    if len(cvars) != 1:
        raise ValueError("Ambiguous ICvar implementation")
    return {
        "vstdlib_sha256": hashlib.sha256(vstdlib.data).hexdigest(),
        "server_targets": server_targets,
        "engine_targets": targets(engine, config["engine"]),
        "vstdlib_targets": targets(vstdlib, config["vstdlib"]),
        "cvar_table": int(cvars[0]["rva"], 16),
        "model_table": int(models[0]["rva"], 16),
        "client_cleanup": client_cleanup(client),
        "values": values,
        "maps": maps,
        "fields": {name: {"type": kind, "offset": field_offset(server, name, kind)} for name, kind in config["fields"].items()},
    }
