"""Locate the registered auxiliary-power ConVar without changing its value."""

import struct

from pe_study import PE, occurrences


def discover(pe: PE) -> list[dict[str, object]]:
    name = b"sv_infinite_aux_power\0"
    matches = []
    for at in occurrences(pe.data, name):
        name_rva = pe.rva(at)
        for reference in occurrences(pe.data, b"\x68" + struct.pack("<I", pe.image_base + name_rva)):
            start = reference - 10
            if (
                start < 0
                or pe.data[start : start + 6] != b"\x68\x00\x40\x00\x00\x68"
                or pe.data[reference + 5] != 0xB9
                or pe.data[reference + 10] != 0xE8
            ):
                continue
            object_rva = pe.u32(reference + 6) - pe.image_base
            matches.append((object_rva, name_rva))
    if len(matches) != 1:
        raise ValueError("Auxiliary-power ConVar registration changed")
    object_rva, name_rva = matches[0]
    # Retail constructor/ConVar::IsFlagSet and a read-only runtime probe establish
    # name/flags/parent offsets 12/20/28; parent must remain this registered object.
    return [
        {
            "rva": object_rva + 12,
            "kind": "Pointer",
            "before": name_rva,
            "after": name_rva,
        },
        {
            "rva": object_rva + 28,
            "kind": "Pointer",
            "before": object_rva,
            "after": object_rva,
        },
        {"rva": object_rva + 20, "kind": "ClearFlags", "before": 0x4000, "after": 0},
    ]
