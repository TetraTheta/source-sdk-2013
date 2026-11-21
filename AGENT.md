# Maintaining GunMaster HL2

Work on the independent orphan branch `gm/hl2`. Do not merge other SDK trees or run their build scripts. Keep the plugin version at **1.0** until instructed otherwise.
README is for players: place technical material inside `<details>` or link to these maintenance documents. Do not publish personal installation paths, process/session logs, or redundant investigation history.

## Scope and file ownership

Ship the contents of repository-root `Half-Life 2/` for Steam HL2/EP1/EP2. Preserve Valve DLLs, cfg, saves, gameinfo, launch options and Workshop files. Player Admin SMG and cleanup take priority over NPC support.

| Files | Responsibility |
| --- | --- |
| `profiles/*.json`, `tools/profiles.py` | Reviewed hashes/operands, discovery templates, inspection/audit and header generation. |
| `tools/pe_study.py`, discovery modules | PE/RTTI, unique signatures, registrations, datamaps and patch generation. |
| `tools/ExportEvidence.java`, `CheckCodeCaves.java` | Targeted Ghidra export and validation of padding consumed by constructor edits. |
| `src/runtime_patches.*`, `server_plugin.cpp` | Image/hash validation, transactions, lifecycle and rollback. |
| `src/cheat_commands.*`, `sprint_movement.*` | Command and paired movement callbacks. |
| `src/retail_entity_api.*`, `player_tools_runtime.*`, `weapon_admin.cpp`, `sc_remove.cpp`, `sc_clean.cpp`, `keyvalues_settings.*` | Retail ABI bridge, player tools, callback ownership and settings. |
| `tests/` | Discovery/generation, ABI, layout, rollback, export and offline image checks needed for DLL updates. |

`src/profiles.generated.hpp` is generated from reviewed JSON. Retain it so normal building needs no Python. Read [IMPLEMENTATION.md](docs/IMPLEMENTATION.md) before accepting a new binary and [VALIDATION.md](docs/VALIDATION.md) before making gameplay claims.

## Invariants

1. Never modify, replace, redistribute or execute Valve DLLs in automated checks. Raw-byte inspection and offline mapping/relocation without imports or entry-point execution are allowed. The user launches Steam for gameplay tests.
2. Reject unknown module hashes. Never bypass guards, choose the first ambiguous match, or approve a new hash solely because patterns still match.
3. Validate signatures, class/vtable identity and original operands before writes. Changes must be bounded, ASLR-aware and reversible. Restore callbacks before scalar edits and roll back earlier transactions if activation fails.
4. Retain callback code while pointers can reference it. On foreign-write or rollback failure, disable effects, retain forwarding code and require restart. Never overwrite another hook or self-unload from an executing callback.
5. Run writes on engine lifecycle/game-thread callbacks, never DllMain or a background thread. Retail field layouts and call conventions must be established from native evidence, not SDK sizeof assumptions.
6. Constructor growth may consume only reviewed adjacent INT3 padding with no function/incoming reference. Preserve allocation-failure branches. Existing-save underwater repair is excluded; do not add frame-time flag fixups.
7. Preserve Valve checks unrelated to each feature, including normal achievement conditions, command cheat checks, ownership and map movement constraints. Do not award achievements from tests.
8. Record actual gameplay reports separately from static/offline results. Absence of crashes or an ACTIVE message is not a feature pass.

## Updating after a Steam DLL change

Use a scratch directory outside the checkout. In the examples, replace `GAME`, `SCRATCH`, `GHIDRA` and `REPO` with local paths; no machine-specific path is required by the repository.

1. Check branch/status and preserve unrelated edits. Find the Steam installation and read `steamapps/appmanifest_220.acf`. Inspect server/client under `hl2_complete`, `hl2`, `episodic`, plus shared `bin/engine.dll` and `bin/vstdlib.dll`. Recheck gameinfo search paths; EP2 uses episodic binaries in the baseline.
2. Inventory and inspect:

   ```bat
   python -B tools\pe_study.py --self-test "GAME\hl2_complete\bin\server.dll"
   python -B tools\profiles.py audit --game "GAME"
   python -B tools\profiles.py inspect --game "GAME" --output "SCRATCH\candidate-profiles.json"
   ```

   Candidates are not compatibility approval. A failed audit/inspection is a reason to investigate.
3. Analyze changed images in Ghidra. Preserve old baselines outside Git when available. Use Version Tracking/BSim only to suggest candidates, then verify semantics and ABI.

   ```bat
   "GHIDRA\support\analyzeHeadless.bat" "SCRATCH\ghidra" HL2_Maintenance -import "SCRATCH\server-NEWBUILD.dll" -analysisTimeoutPerFile 600
   "GHIDRA\support\analyzeHeadless.bat" "SCRATCH\ghidra" HL2_Maintenance -process "server-NEWBUILD.dll" -noanalysis -scriptPath "REPO\tools" -postScript ExportEvidence.java "SCRATCH\evidence.txt" RVA xref:STRING_RVA
   ```

   Substitute hexadecimal RVAs found through PE/RTTI and callers. `function:RVA` can define a missing analysis function. Keep full decompilations, Valve binaries/PDBs and databases outside Git.
4. Review every changed function, caller, operand, field, global, interface and virtual slot described in IMPLEMENTATION. Update discovery templates/extractors only after review. Mask relocations and relative-call displacements, not bytes whose meaning establishes the feature. Check constructor padding with `CheckCodeCaves.java RVA:LENGTH ...`. Inspect client and server separately.
5. Re-run inspect and review all candidate metadata. Only then set `review_status` to `STATIC_REVIEWED_RUNTIME_UNVERIFIED`, retain `runtime_verified: false`, and save `profiles/profiles.json`. Never simply replace hashes or mix unmatched module pairs.
6. Generate and verify:

   ```bat
   python -B tools\profiles.py generate
   python -B tools\profiles.py audit --game "GAME"
   ruff check --no-cache tools tests
   ruff format --check --no-cache tools tests
   python -B -m unittest discover -s tests -p "test_*.py"
   build_hl2.cmd
   ```

7. Run offline mappings for all eight module files from Command Prompt:

   ```bat
   for %G in (hl2_complete hl2 episodic) do for %M in (server client) do build\runtime_tests.exe --image "GAME\%G\bin\%M.dll"
   for %M in (engine vstdlib) do build\runtime_tests.exe --image "GAME\bin\%M.dll"
   ```

   Require every result to pass. These mappings do not execute Valve code. Maintain negative cases for changed/ambiguous signatures, wrong layouts/slots/operands and rollback ownership.
8. Verify the packaged x86 DLL exports undecorated CreateInterface and matches the tested output. Package only the DLL, two VDF files, two settings scripts and carrier script. The build must not deploy to Steam. Update validation evidence; gameplay acceptance remains a separate manual step.

## Stable project decisions

- Admin SMG uses `basehlcombatweapon`, not a new allocation/network class. NPC use is deferred. Preserve silent Explosion attacks and correct debris flag `0x400000`.
- Combine Ball uses its configured speed. No speed sampler, diagnostic setting or global physics adjustment is requested.
- A reported crouch-sprint lockout was associated with a map force-crouch trigger after successful movement elsewhere. Do not add energy refills or bypass triggers for that unconfirmed power issue.
- First launch after a DLL replacement may miss autoload; restart/campaign switching can recover. Keep a brief player note. Further loader investigation is deferred; plugin_load is a no-op in the baseline engine.
- Retain maintenance tests, not HUD/configuration/game-balance tests. Build/cache directories are disposable; preserve the packaged DLL. Follow the existing formatting configurations.
- Refresh README's branch comparison when feature coverage changes. Code presence and gameplay validation are different statuses.
