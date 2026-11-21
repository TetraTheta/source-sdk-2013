"""Inspect candidate HL2 profiles, audit reviewed profiles, or generate C++ data."""

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from features import discover as discover_features
from gameplay import discover
from movement import discover as discover_movement
from pe_study import PE, scan
from retail import discover as discover_retail
from settings import discover as discover_settings

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "profiles/profiles.json"
PATTERNS = ROOT / "profiles/weapon-patterns.json"


class Arguments(argparse.Namespace):
    action: str
    game: Path | None
    output: Path | None


PARSER = argparse.ArgumentParser(description=__doc__)
PARSER.add_argument("action", choices=["inspect", "audit", "generate"])
PARSER.add_argument("--game", type=Path, help="Steam Half-Life 2 installation root")
PARSER.add_argument("--output", type=Path)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def float_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def inspect_game(game: Path) -> dict[str, object]:
    """Produce candidates, never approve a new hash or modify the installation."""
    patterns = json.loads(PATTERNS.read_text(encoding="utf-8"))["targets"]
    profiles = []
    for folder in ["hl2_complete", "hl2", "episodic"]:
        server = PE(game / folder / "bin/server.dll")
        targets = []
        patches = []
        for target in patterns:
            matches = [
                s["rva"] + at
                for s in server.sections
                if s["flags"] & 0x20000000
                for at in scan(server.data[s["raw"] : s["raw"] + s["size"]], target["pattern"])
            ]
            if len(matches) != 1:
                raise ValueError(f"{folder}/{target['name']}: expected one signature, found {len(matches)}; use Ghidra")
            tables = server.vtables(target["class"])
            if len(tables) != 1:
                raise ValueError(f"{folder}/{target['name']}: ambiguous RTTI table")
            table = tables[0]
            entry = matches[0]
            if int(table["entries"][target["slot"]], 16) != entry:
                raise ValueError(f"{folder}/{target['name']}: virtual slot changed; use Ghidra")
            targets.append(
                {
                    "name": target["name"],
                    "rva": entry,
                    "pattern": target["pattern"],
                    "vtable": int(table["rva"], 16),
                    "slot": target["slot"],
                }
            )
            code = server.data[server.raw(entry) : server.raw(entry) + target["length"]]
            if target["name"].endswith("GetBulletSpread"):
                stock = float_bits(0.04362 if target["class"] == "CWeaponSMG1" else 0.02618)
                stores = list(
                    re.finditer(
                        b"\xc7\x05(.{4})" + re.escape(struct.pack("<I", stock)),
                        code,
                        re.DOTALL,
                    )
                )
                if len(stores) != 3:
                    raise ValueError("Spread initialization changed")
                cone = struct.unpack("<I", stores[0][1])[0] - server.image_base
                for i, store in enumerate(stores):
                    address = struct.unpack("<I", store[1])[0] - server.image_base
                    if address != cone + i * 4:
                        raise ValueError("Spread no longer uses one contiguous Vector")
                    patches.append(
                        {
                            "rva": entry + store.start() + 6,
                            "kind": "Scalar",
                            "before": stock,
                            "after": float_bits(0.00873),
                        }
                    )
                    patches.append(
                        {
                            "rva": address,
                            "kind": "Cone",
                            "before": stock,
                            "after": float_bits(0.00873),
                        }
                    )
                if b"\xb8" + struct.pack("<I", server.image_base + cone) not in code:
                    raise ValueError("Spread return address changed")
            else:
                # These instruction offsets are reviewed against Build 19307283 disassembly.
                operands = [(33, 2.0, 1.0)] if target["class"] == "CWeaponSMG1" else [(97, 5.0, 1.0), (117, 8.0, 4.0)]
                for offset, before, after in operands:
                    if code[offset - 2 : offset] != b"\xd9\x05":
                        raise ValueError("Recoil instruction layout changed; use Ghidra")
                    original = struct.unpack_from("<I", code, offset)[0] - server.image_base
                    if server.u32(server.raw(original)) != float_bits(before):
                        raise ValueError("Recoil source constant changed")
                    constants = [
                        s["rva"] + at
                        for s in server.sections
                        if s["name"] == ".rdata"
                        for at in range(0, s["size"] - 3, 4)
                        if server.u32(s["raw"] + at) == float_bits(after)
                    ]
                    if not constants:
                        raise ValueError("Replacement constant absent")
                    patches.append(
                        {
                            "rva": entry + offset,
                            "kind": "Pointer",
                            "before": original,
                            "after": constants[0],
                        }
                    )
        gameplay_targets, gameplay_patches = discover(server)
        targets.extend(gameplay_targets)
        patches.extend(gameplay_patches)
        patches.extend(discover_settings(server))
        feature_targets, feature_patches, commands = discover_features(server)
        targets.extend(feature_targets)
        patches.extend(feature_patches)
        movement, movement_probes = discover_movement(server, PE(game / folder / "bin/client.dll"))
        patches.extend(movement_probes)
        retail = discover_retail(
            server,
            PE(game / folder / "bin/client.dll"),
            PE(game / "bin/engine.dll"),
            PE(game / "bin/vstdlib.dll"),
        )
        if len({p["rva"] for p in patches}) != len(patches):
            raise ValueError("Overlapping patches")
        profiles.append(
            {
                "name": folder,
                "runtime_verified": False,
                "server_sha256": sha256(server.path),
                "client_sha256": sha256(game / folder / "bin/client.dll"),
                "engine_sha256": sha256(game / "bin/engine.dll"),
                "targets": targets,
                "patches": patches,
                "commands": commands,
                "movement": movement,
                "retail": retail,
            }
        )
    manifest = game.parent.parent / "appmanifest_220.acf"
    build = re.search(r'"buildid"\s+"(\d+)"', manifest.read_text(encoding="utf-8")) if manifest.exists() else None
    return {
        "build_id": build[1] if build else "unknown",
        "review_status": "CANDIDATE_REQUIRES_REVIEW",
        "profiles": profiles,
    }


def generate(config: dict[str, object]) -> str:
    """Keep reviewed JSON as the only manually maintained source of offsets."""
    if config["review_status"] != "STATIC_REVIEWED_RUNTIME_UNVERIFIED":
        raise ValueError("Review candidate profiles before generating a build")
    lines = [
        "// Generated by tools/profiles.py; edit reviewed JSON instead.",
        "#pragma once",
        '#include "retail_entity_api.hpp"',
        "namespace gm {",
    ]
    names = []
    for i, profile in enumerate(config["profiles"]):
        names.append(i)
        lines.append(f"inline constexpr Target kTargets{i}[] = {{")
        for t in profile["targets"]:
            lines.append(f'  {{"{t["name"]}", 0x{t["rva"]:x}, "{t["pattern"]}", 0x{t["vtable"]:x}, {t["slot"]}, 0x{t.get("class_vtable", 0):x}}},')
        lines.append("};")
        retail = profile["retail"]
        lines.append(f"inline constexpr RetailFunction kRetailFunctions{i}[] = {{")
        for t in retail["server_targets"]:
            lines.append(f'  {{RetailFn::{t["name"]}, {{"{t["name"]}", 0x{t["rva"]:x}, "{t["pattern"]}", 0, 0}}}},')
        lines.append("};")
        for key, label in (
            ("engine_targets", "RetailEngine"),
            ("vstdlib_targets", "RetailCvar"),
        ):
            lines.append(f"inline constexpr Target k{label}{i}[] = {{")
            for t in retail[key]:
                lines.append(f'  {{"{t["name"]}", 0x{t["rva"]:x}, "{t["pattern"]}", 0, 0}},')
            lines.append("};")
        lines.append(f"inline constexpr RetailValue kRetailValues{i}[] = {{")
        lines.extend(f'  {{"{name}", 0x{rva:x}}},' for name, rva in retail["values"].items())
        lines.append("};")
        lines.append(f"inline constexpr RetailField kRetailFields{i}[] = {{")
        lines.extend(f'  {{"{name}", {data["type"]}, 0x{data["offset"]:x}}},' for name, data in retail["fields"].items())
        lines.append("};")
        lines.append(f"inline constexpr RetailMap kRetailMaps{i}[] = {{")
        lines.extend(f'  {{"{name}", 0x{data["table"]:x}, 0x{data["map"]:x}}},' for name, data in retail["maps"].items())
        lines.append("};")
        cleanup = retail["client_cleanup"]
        lines.append(f'inline constexpr RetailSpec kRetail{i}{{"{retail["vstdlib_sha256"]}",')
        lines.append(f"  kRetailFunctions{i}, std::size(kRetailFunctions{i}), kRetailEngine{i}, std::size(kRetailEngine{i}),")
        lines.append(f"  kRetailCvar{i}, std::size(kRetailCvar{i}), kRetailValues{i}, std::size(kRetailValues{i}),")
        lines.append(f"  kRetailFields{i}, std::size(kRetailFields{i}), kRetailMaps{i}, std::size(kRetailMaps{i}),")
        lines.append(
            f"  0x{retail['cvar_table']:x}, 0x{retail['model_table']:x}, {{"
            + ", ".join(
                f"0x{cleanup[key]:x}"
                for key in (
                    "list_primary",
                    "list_secondary",
                    "list_offset",
                    "list_instance",
                    "ragdoll_table",
                    "release_field",
                )
            )
            + "}};"
        )
        movement = profile["movement"]
        lines.append(f"inline constexpr MovementSpec kMovement{i}{{")
        for key in (
            "server_crop",
            "client_crop",
            "server_sprint_probe",
            "client_sprint_probe",
        ):
            t = movement[key]
            lines.append(f'  {{"{t["name"]}", 0x{t["rva"]:x}, "{t["pattern"]}", 0x{t["vtable"]:x}, {t["slot"]}}},')
        lines.append(
            "  "
            + ", ".join(
                f"0x{movement[key]:x}"
                for key in (
                    "norm_speed",
                    "norm_name",
                    "movement_move",
                    "movement_cropped",
                    "move_sprint",
                    "move_inputs",
                    "move_maxspeed",
                )
            )
            + "};"
        )
        commands = profile["commands"]
        lines.append(f"inline constexpr CommandsSpec kCommands{i}{{")
        for name in ("god", "noclip"):
            command = commands[name]
            lines.append(f"  {{0x{command['object']:x}, 0x{command['name']:x}, 0x{command['callback']:x}}},")
        lines.append(
            "  "
            + ", ".join(
                f"0x{commands[key]:x}"
                for key in (
                    "dispatch_table",
                    "dispatch",
                    "get_player",
                    "player_flags",
                    "player_movetype",
                )
            )
            + "};"
        )
        lines.append(f"inline constexpr PatchSpec kPatches{i}[] = {{")
        for p in profile["patches"]:
            lines.append(f"  {{0x{p['rva']:x}, PatchKind::{p['kind']}, 0x{p['before']:x}, 0x{p['after']:x}}},")
        lines.append("};")
    lines.append("inline constexpr Profile kProfiles[] = {")
    for i, p in enumerate(config["profiles"]):
        lines.append(
            f'  {{"{p["name"]}", "{p["server_sha256"]}", "{p["client_sha256"]}", "{p["engine_sha256"]}", kTargets{i}, std::size(kTargets{i}), kPatches{i}, std::size(kPatches{i}), &kCommands{i}, &kMovement{i}, &kRetail{i}}},'
        )
    lines.extend(["};", "} // namespace gm", ""])
    return "\n".join(lines)


def main() -> None:
    args = PARSER.parse_args(namespace=Arguments())
    if args.action == "generate":
        content = generate(json.loads(CONFIG.read_text(encoding="utf-8")))
        output = args.output or ROOT / "src/profiles.generated.hpp"
        output.write_text(content, encoding="utf-8", newline="\n")
        print(f"Generated {output}")
        return
    if args.game is None:
        PARSER.error("--game is required for inspect/audit")
    candidate = inspect_game(args.game.resolve())
    if args.action == "audit":
        reviewed = json.loads(CONFIG.read_text(encoding="utf-8"))
        if candidate["profiles"] != reviewed["profiles"]:
            raise ValueError("Installed hashes/targets differ from reviewed profiles; DO NOT HOOK")
        print("All three installed profiles match the reviewed static baseline; runtime remains unverified.")
        return
    content = json.dumps(candidate, indent=2) + "\n"
    if args.output:
        args.output.write_text(content, encoding="utf-8", newline="\n")
    else:
        print(content)


if __name__ == "__main__":
    main()
