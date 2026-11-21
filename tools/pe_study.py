"""Read-only PE32 inventory, RTTI vtable locator, and signature candidate scanner."""

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


class Arguments(argparse.Namespace):
    files: list[str]
    output: str | None
    pattern: str | None
    self_test: bool


PARSER = argparse.ArgumentParser(description=__doc__)
PARSER.add_argument("files", nargs="*", help="DLL paths (wildcards accepted)")
PARSER.add_argument("--output", help="JSON report path")
PARSER.add_argument("--pattern", help="Space-separated hex bytes; ?? is a wildcard")
PARSER.add_argument("--self-test", action="store_true")


def occurrences(data: bytes, needle: bytes) -> list[int]:
    """Return overlapping matches, including the final possible offset."""
    result = []
    offset = data.find(needle)
    while offset >= 0:
        result.append(offset)
        offset = data.find(needle, offset + 1)
    return result


def scan(data: bytes, pattern: str) -> list[int]:
    """Find candidates only; a unique byte match does not prove ABI compatibility."""
    tokens = pattern.split()
    if not tokens or any(not re.fullmatch(r"[0-9a-fA-F]{2}|\?\?", t) for t in tokens):
        raise ValueError("Expected hex byte pairs or ??")
    expression = b"".join(b"." if t == "??" else re.escape(bytes.fromhex(t)) for t in tokens)
    return [m.start() for m in re.finditer(b"(?=(" + expression + b"))", data, re.DOTALL)]


class PE:
    """Parse only the PE32 structures needed for this installed x86 HL2 build."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError(f"{path}: not an MZ image")
        pe = self.u32(0x3C)
        if self.data[pe : pe + 4] != b"PE\0\0":
            raise ValueError(f"{path}: missing PE header")
        self.machine, count = struct.unpack_from("<HH", self.data, pe + 4)
        optional = pe + 24
        if self.machine != 0x14C or struct.unpack_from("<H", self.data, optional)[0] != 0x10B:
            raise ValueError(f"{path}: only x86 PE32 is supported")
        self.image_base = self.u32(optional + 28)
        self.image_size = self.u32(optional + 56)
        self.directories = [struct.unpack_from("<II", self.data, optional + 96 + i * 8) for i in range(16)]
        self.sections = []
        table = optional + struct.unpack_from("<H", self.data, pe + 20)[0]
        for i in range(count):
            at = table + i * 40
            name = self.data[at : at + 8].rstrip(b"\0").decode("ascii")
            virtual_size, rva, size, raw = struct.unpack_from("<IIII", self.data, at + 8)
            flags = self.u32(at + 36)
            if raw + size > len(self.data):
                raise ValueError(f"{path}: section extends beyond file")
            self.sections.append(
                {
                    "name": name,
                    "rva": rva,
                    "raw": raw,
                    "size": size,
                    "virtual_size": virtual_size,
                    "flags": flags,
                }
            )

    def u32(self, offset: int) -> int:
        return struct.unpack_from("<I", self.data, offset)[0]

    def raw(self, rva: int) -> int:
        for section in self.sections:
            delta = rva - section["rva"]
            if 0 <= delta < section["size"]:
                return section["raw"] + delta
        raise ValueError(f"RVA {rva:#x} is not file-backed")

    def rva(self, raw: int) -> int:
        for section in self.sections:
            delta = raw - section["raw"]
            if 0 <= delta < section["size"]:
                return section["rva"] + delta
        raise ValueError(f"File offset {raw:#x} is outside sections")

    def executable(self, va: int) -> bool:
        rva = va - self.image_base
        return any(s["flags"] & 0x20000000 and s["rva"] <= rva < s["rva"] + s["virtual_size"] for s in self.sections)

    def cstring(self, raw: int) -> str:
        end = self.data.index(b"\0", raw)
        return self.data[raw:end].decode("ascii", errors="replace")

    def exports(self) -> dict[str, str]:
        rva, _size = self.directories[0]
        if not rva:
            return {}
        at = self.raw(rva)
        count = self.u32(at + 24)
        functions = self.raw(self.u32(at + 28))
        names = self.raw(self.u32(at + 32))
        ordinals = self.raw(self.u32(at + 36))
        result = {}
        for i in range(count):
            name = self.cstring(self.raw(self.u32(names + 4 * i)))
            ordinal = struct.unpack_from("<H", self.data, ordinals + 2 * i)[0]
            result[name] = hex(self.u32(functions + 4 * ordinal))
        return result

    def vtables(self, class_name: str) -> list[dict[str, object]]:
        """Follow x86 MSVC TypeDescriptor -> primary COL -> vtable[-1]."""
        result = []
        for name in occurrences(self.data, f".?AV{class_name}@@\0".encode()):
            descriptor = self.image_base + self.rva(name - 8)
            for reference in occurrences(self.data, struct.pack("<I", descriptor)):
                col = reference - 12
                if col < 0 or self.data[col : col + 12] != bytes(12):
                    continue
                col_va = self.image_base + self.rva(col)
                for pointer in occurrences(self.data, struct.pack("<I", col_va)):
                    entries = []
                    for slot in range(700):
                        at = pointer + 4 + slot * 4
                        if at + 4 > len(self.data):
                            break
                        target = self.u32(at)
                        if not self.executable(target):
                            break
                        entries.append(hex(target - self.image_base))
                    if entries:
                        result.append(
                            {
                                "rva": hex(self.rva(pointer + 4)),
                                "col_rva": hex(self.rva(col)),
                                "entries": entries,
                            }
                        )
        return result

    def report(self, pattern: str | None) -> dict[str, object]:
        strings = {}
        for value in [
            "basehlcombatweapon",
            "hl2basecombatweapon",
            "weapon_admin",
            "weapon_smg1",
            "weapon_ar2",
            "VSERVERTOOLS",
            "ServerGameDLL",
            "VClient",
            "ISERVERPLUGINCALLBACKS",
            "plugin_load",
            "CBaseHLCombatWeapon",
            "DT_BaseHLCombatWeapon",
            "VWorkshop001",
        ]:
            strings[value] = [hex(self.rva(at)) for at in occurrences(self.data, value.encode())]
        debug = []
        debug_rva, debug_size = self.directories[6]
        if debug_rva:
            at = self.raw(debug_rva)
            for i in range(debug_size // 28):
                entry = at + 28 * i
                if self.u32(entry + 12) == 2:
                    raw = self.u32(entry + 24)
                    if self.data[raw : raw + 4] == b"RSDS":
                        debug.append(
                            {
                                "guid_hex": self.data[raw + 4 : raw + 20].hex(),
                                "age": self.u32(raw + 20),
                                "pdb_path": self.cstring(raw + 24),
                            }
                        )
        result = {
            "path": str(self.path.resolve()),
            "sha256": hashlib.sha256(self.data).hexdigest(),
            "size": len(self.data),
            "image_base": hex(self.image_base),
            "machine": "x86",
            "security_directory": self.directories[4],
            "debug": debug,
            "exports": self.exports(),
            "strings": strings,
            "vtables": {
                c: self.vtables(c)
                for c in [
                    "CWeaponSMG1",
                    "CWeaponAR2",
                    "CBaseHLCombatWeapon",
                    "C_BaseHLCombatWeapon",
                ]
            },
        }
        if pattern:
            result["signature_candidates"] = [
                hex(s["rva"] + at)
                for s in self.sections
                if s["flags"] & 0x20000000
                for at in scan(self.data[s["raw"] : s["raw"] + s["size"]], pattern)
            ]
        return result


def self_test() -> None:
    """Check wildcard, overlap, empty match, boundary, and input validation."""
    assert occurrences(b"aaaa", b"aa") == [0, 1, 2]
    assert scan(b"\x55\x00\x55\xff\x55", "55 ?? 55") == [0, 2]
    assert scan(b"\x00\xff", "00 FF") == [0]
    assert scan(b"\xff", "00") == []
    for invalid in ["", "GG", "5", "?"]:
        try:
            scan(b"", invalid)
        except ValueError:
            continue
        raise AssertionError(f"Invalid pattern accepted: {invalid}")


def main() -> None:
    args = PARSER.parse_args(namespace=Arguments())
    if args.self_test:
        self_test()
        print("Self-check passed")
    paths = [p for value in args.files for p in (sorted(Path(value).parent.glob(Path(value).name)) if "*" in value else [Path(value)])]
    if args.files and not paths:
        PARSER.error("No input files matched")
    if paths:
        result = json.dumps([PE(p).report(args.pattern) for p in paths], indent=2, ensure_ascii=False)
        if args.output:
            Path(args.output).write_text(result + "\n", encoding="utf-8")
        else:
            print(result)


if __name__ == "__main__":
    main()
