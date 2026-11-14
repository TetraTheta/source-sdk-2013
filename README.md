# GunMaster

GunMaster provides replacement DLLs for **Entropy: Zero 2, Mapbase, Map Labs, and RTSL Mod Template releases**.
It adds weapon adjustments, underwater firing, faster crouched movement, and optional tools for single-player play.
You need the original game or mod and its content installed.

[Download GunMaster from Releases](https://github.com/TetraTheta/source-sdk-2013/releases)

This README describes the current source on the four `gm/*` branches. A downloadable release may contain an older build;
check its version before expecting every feature listed here.

## Install

1. Choose the build matching your game or mod in the table below. Expand its game-specific section for additional requirements.
2. Close the game and back up the original DLLs in its `bin` folder. Back up any configuration files you change, too.
3. Copy the matching replacement DLLs into that game's or mod's own `bin` folder, replacing the originals.
4. Keep the existing maps, models, materials, sounds, scripts, and settings. These DLLs do not include a complete game.

| Game or mod | Branch | Replacement DLLs |
| --- | --- | --- |
| Entropy: Zero 2 | `gm/ez2` | `client.dll`, `server.dll` |
| Mapbase | `gm/mapbase` | `client.dll`, `server.dll`, `game_shader_dx9.dll`; choose the matching **HL2** or **Episodic** set |
| Map Labs | `gm/maplabs` | `client.dll`, `server.dll`, `game_shader_dx9.dll` |
| RTSL Mod Template challenge releases | `gm/rtsl` | `client.dll`, `server.dll`, `game_shader_dx9.dll`; **Mapbase setup is also required below** |

For small-object cleanup, also put the matching branch's `sc_clean.txt` in the mod's `scripts` folder, beside `bin`.
The default files are under `sp/game/mod_ez2/scripts` for EZ2, `sp/game/mod_hl2/scripts` for Mapbase HL2,
and `sp/game/mod_episodic/scripts` for the other builds. If the download does not include this file,
get it from the matching source branch. Without it, other cleanup categories still work, but small-object cleanup is disabled.

To undo the replacement, close the game and restore your backups. Saves made with different DLL versions are not guaranteed
compatible; keep a backup of your saves as well.

## Changes shared by all four builds

- **Adjusted weapon handling.** The AR2 has less upward kick, and the SMG1 has tighter accuracy and adjusted recoil.
  Exact changes depend on the game; see its section below.
- **Faster energy balls.** Player-fired AR2 Combine Balls travel twice as fast.
- **Underwater firing.** Fire the .357, AR2, SMG1, and shotgun underwater, including AR2 energy balls, SMG1 grenades,
  and the shotgun's double shot.
- **Sprint while crouched.** Use sprint to move faster without standing up. Normal sprint requirements still apply.
- **Sprint while holding objects.** Sprint while carrying an object with the use key (E by default) or the Gravity Gun.
  Normal sprint requirements still apply. `sv_player_enable_propsprint` and `sv_player_enable_gravgun_sprint` now default
  to `1`; explicit configuration values of `0` restore the corresponding restriction.
- **Achievements with cheats.** Enabling `sv_cheats` no longer blocks achievement eligibility. Each achievement's normal
  requirements still apply.
- **Optional Admin SMG.** A powerful, ammo-free weapon with five alternate-fire modes; it uses existing SMG1 models.
- **Cleanup tools.** Remove the object you are looking at with `sc_remove`, or clear selected kinds of clutter with `sc_clean`.
- **Convenient settings.** Enable unlimited suit power without cheats, adjust SLAM capacity, and explicitly turn god mode
  or noclip on or off.
- **Weapon pickup behavior.** Automatic switching on pickup now respects weapons that opt out of switching to or from them,
  including the Admin SMG's default preference to stay equipped.
- **Clearer HUD hints.** Multiline hints can display larger headings above their text, including cleanup results.

<details>
<summary>Console commands, Admin SMG controls, and optional settings</summary>

Enable the developer console in the game's keyboard options, then enter commands there.

### Cleanup

| Command | Effect | Requires `sv_cheats 1`? |
| --- | --- | --- |
| `sc_remove` | Remove the object under your crosshair. Players and their equipment are protected. | No |
| `sc_clean all` | Clean all supported categories, including ragdolls and decals. | No |
| `sc_clean ammo` | Remove eligible ammo pickups. | No |
| `sc_clean debris` | Remove eligible physics debris. | No |
| `sc_clean decals` | Clear world and model decals. | No |
| `sc_clean gibs` | Remove eligible gibs. | No |
| `sc_clean powerups` | Remove eligible health and suit-power pickups. | No |
| `sc_clean ragdolls` | Remove eligible server ragdolls and release client ragdolls. | No |
| `sc_clean small` | Remove eligible physics props matching `scripts/sc_clean.txt`. | No |
| `sc_clean weapons` | Remove unowned weapons that were not originally placed by the map. | No |

Decals are cleared on clients only by `sc_clean decals` or `sc_clean all`. Client ragdolls are released only by
`sc_clean ragdolls` or `sc_clean all`. Server entity cleanup skips protected objects, including named objects,
objects with connected map outputs, and player equipment. It only removes matching
server entities with physics objects, so a category does not necessarily remove every visible object of that type.
A HUD message reports server objects removed when there are results.

**Save before removing objects.** `sc_remove` can remove doors and other progression-critical objects. Cleanup protections
also cannot guarantee that every map remains playable after deleting items or props.

`sc_clean.txt` controls the model patterns used by `small` and `all`; `*` matches any sequence of characters.
It is loaded on the first map only. Restart the game after changing it or adding a missing file.

### Admin SMG

Enter these commands to obtain the weapon:

```text
sv_cheats 1
give weapon_admin
```

- **Primary fire:** rapid, high-damage bullet fire without ammunition or reloading.
- **Secondary fire:** use the selected mode: Explosion, Airboat Gun, Combine Ball, Crossbow Bolt, or Armed Grenade.
- **Reload:** press to cycle through those five modes while the weapon is raised and its attack cooldown has elapsed.
  Each accepted mode change plays a menu click and displays the selected mode as a HUD hint.

The weapon works with built-in defaults; no extra weapon script, models, or sound pack is required beyond the installed
base content. Optionally use `scripts/weapon_admin.txt` to customize it. An example is in the matching branch's
`sp/game/mod_ez2/scripts` or `sp/game/mod_episodic/scripts` directory. Its `AdminAttack` section configures damage,
fire delays, recoil, projectiles, and the starting mode; omitted or invalid attack values use built-in defaults.
The weapon always uses no ammo or clips.

### Other commands and settings

| Command or setting | Behavior | Requires `sv_cheats 1`? |
| --- | --- | --- |
| `sv_infinite_aux_power 1` | Enable unlimited auxiliary suit power. Its original default is unchanged. | No |
| `sk_max_slam <amount>` | Set SLAM carry capacity instead of the old fixed limit of 5. | No |
| `god on` / `god off` | Explicitly enable or disable god mode. `god` alone still toggles it. | Yes |
| `noclip on` / `noclip off` | Explicitly enable or disable noclip. `noclip` alone still toggles it. | Yes |

EZ2 also allows its separate sprint and flashlight power settings without cheats; see below.

### Saving video settings

After changing and applying the resolution in the game's settings, run `mat_savechanges` in the console to save the
current video settings to the Windows registry. `host_writeconfig` saves `config.cfg`; it is not a substitute for
`mat_savechanges`. Resolution launch options or commands in `autoexec.cfg` can override saved settings on the next launch.

</details>

## Game-specific changes and setup

<details>
<summary>Entropy: Zero 2 — gm/ez2</summary>

### Weapon and gameplay differences

- AR2 and prototype AR2 maximum upward kick is 4 degrees; SMG1 and SMG2 maximum upward kick is 1 degree.
- Prototype AR2 and SMG1 spread is 1 degree. The regular AR2 already has 1-degree player accuracy in upstream EZ2;
  SMG2 spread is unchanged.
- SMG2 can also fire underwater.
- Attacks from outside your view no longer cause the extra-heavy damage camera kick.
- `sv_infinite_sprint_power` and `sv_infinite_flashlight_power` no longer require cheats. Their original defaults are unchanged.

### Alternative console

Open or close it with `toggleconsole2`. It includes startup messages, accepts commands, and supports `clear` and `condump`.
Opening it pauses an active single-player game; closing it resumes the game only if this console paused it.
Consecutive identical lines with matching colors are grouped with an `(xN)` count, and bound F1–F12 commands remain
usable while typing in the console.

### Fixes

- Scripted sequences retry when a displaced NPC returns instead of abandoning the sequence.
- Correct an NPC AI condition registration issue and several console-setting conflicts that caused startup errors.
- Correct the flashlight-distance setting name to `cl_stunstick_flashlight_distance`.
- Disable the client-side `dropprimary` command, which could drop every weapon in a slot instead of the intended weapon.

<details>
<summary>Source base</summary>

Based on [Entropy: Zero 2](https://github.com/entropy-zero/source-sdk-2013/commit/4c4fea9c4f8657bd66f964570e11a1714389bc77).

</details>
</details>

<details>
<summary>Mapbase — gm/mapbase</summary>

Choose the **HL2** or **Episodic** DLL set to match your installation.

- AR2 spread changes from 3 degrees to 1 degree; SMG1 spread changes from 5 degrees to 1 degree.
- AR2 maximum upward kick changes from 8 degrees to 4 degrees. SMG1 recoil builds up differently, but its maximum
  upward kick stays at 1 degree.
- On Windows, a first launch without saved resolution settings uses **1920×1080**, regardless of the desktop resolution.
  Existing settings (including 1280×720), explicit launch-option dimensions, and safe mode are respected.

<details>
<summary>Source base</summary>

Based on [Mapbase](https://github.com/mapbase-source/source-sdk-2013/commit/d9fd33535849b70f647895077b6b011b229c373c).

</details>
</details>

<details>
<summary>Map Labs — gm/maplabs</summary>

- AR2 spread changes from 3 degrees to 1 degree; SMG1 spread changes from 5 degrees to 1 degree.
- AR2 maximum upward kick changes from 8 degrees to 4 degrees. SMG1 recoil builds up differently, but its maximum
  upward kick stays at 1 degree.
- On Windows, a first launch without saved resolution settings uses **1920×1080**, regardless of the desktop resolution.
  Existing settings (including 1280×720), explicit launch-option dimensions, and safe mode are respected.
- The Map Labs base already supports compatible loose `.vcd` scene files without rebuilding `scenes.image`.
  GunMaster retains this behavior; it is not a new GunMaster feature.

<details>
<summary>Source base</summary>

Based on [Map Labs](https://github.com/map-labs-source/Map-Labs/commit/a58db32f67857c3b027c88e6d534b6be126f7029).

</details>
</details>

<details>
<summary>RTSL — gm/rtsl</summary>

This build combines the Map Labs base and its GunMaster changes with gameplay support for **RTSL Mod Template challenge
releases**. It also has the Map Labs weapon changes and **1920×1080 first-launch resolution** described above.
Keep the challenge release's content, settings, and original mod folder name.

**RTSL visual features such as CSM, Uberlight, and volumetric lighting are not implemented.**

### Required Mapbase setup

Install Mapbase and add its search paths before using these DLLs. Missing paths can cover the screen in purple-and-black
missing textures when entering a map.

1. Back up the challenge release's DLLs and `gameinfo.txt`.
2. Install Mapbase beside the challenge mod under Steam's `sourcemods` directory. The example assumes folders named
   `mapbase_episodic`, `mapbase_hl2`, and `mapbase_shared`.
3. Put the RTSL replacement DLLs in the challenge mod's own `bin` folder.
4. In `gameinfo.txt`, add the Mapbase entries inside `FileSystem` → `SearchPaths`, after the mod's own content and
   binary paths and before the base Half-Life 2 paths. Keep the remaining existing paths.

```text
game+mod+mod_write+default_write_path  |gameinfo_path|.
gamebin                              |gameinfo_path|bin

// Add these Mapbase paths:
gamebin   |gameinfo_path|../mapbase_episodic/bin
game+mod  |gameinfo_path|../mapbase_episodic
game+mod  |gameinfo_path|../mapbase_episodic/content/*
game+mod  |gameinfo_path|../mapbase_hl2
game+mod  |gameinfo_path|../mapbase_hl2/content/*
game+mod  |gameinfo_path|../mapbase_shared/shared_misc
gamebin   |gameinfo_path|../mapbase_shared/shared_misc/bin
game+mod  |gameinfo_path|../mapbase_shared/shared_content_v7_0.vpk

// Keep the existing base Half-Life 2 paths below, for example:
game      |all_source_engine_paths|ep2/ep2_english.vpk
```

Keep the challenge mod's own `bin` path first so the GunMaster DLLs take precedence over Mapbase's DLLs.

### Gameplay support

- **MechaCrabs:** Combine-aligned mechanical headcrabs with their attacks and sounds. The normal Gravity Gun can attack them.
- **Stasis Crystal:** picking it up unlocks the Gravity Gun's freeze ability, with a message, hint, sound, and crystal
  attachment when the release's weapon model supports it.
- **Freeze controls:** bind a key with `bind mouse3 +attack3`, unless the release already provides a binding. With the
  upgrade and Gravity Gun equipped, freeze a held object or toggle a supported object in front of you. Holding the key
  can repeatedly toggle a targeted object after each half-second cooldown.
- **Freeze limits:** physics props and physics boxes are supported, including fence-like props; vehicles are excluded.
  Ordinary mapper-locked objects cannot be unlocked through this ability.
- **Frozen-object highlighting:** frozen objects have a yellow outline within the original highlight distance.
- **Existing release content:** RTSL graffiti and replacement weapon appearances use the challenge's own assets.
- **Scene loading:** retains Map Labs' existing support for compatible loose `.vcd` files.

The freeze upgrade and frozen-object state support saving and loading. Saves from other DLL versions are not guaranteed compatible.

<details>
<summary>Mapper compatibility, fallback settings, and source bases</summary>

| Feature | Behavior |
| --- | --- |
| MechaCrab canisters | Maps can explicitly select MechaCrabs; random canisters retain Map Labs' ordinary, fast, and poison headcrab selection. |
| Map-controlled freezing | Supports `GGFreeze`, `GGUnFreeze`, and RTSL initial-freeze options. Initial RTSL freezing can be reversed; an ordinary mapper motion lock cannot. |
| Removing the upgrade | `StripGGFreezeModule` removes the ability. Acquiring or removing it updates the compatible weapon attachment and plays the draw animation when the weapon updates. |
| Saved state | Saves the upgrade, frozen-object state, outline reference, and attack cooldown. |
| Other outlines | Mapbase outlines retain their own behavior. |
| Headless zombies | `isheadless` maps to Mapbase's headless state for ordinary and fast zombies, including torso variants. |
| Headcrab detachment | `noheadcrabspawn` uses Mapbase's release prevention, including chopped deaths. |
| Map titles | Uses `scripts/maptitles.txt` with full map-name matches; Mapbase chapter-title support remains available. |

| Setting | Fallback | Meaning |
| --- | --- | --- |
| `sk_mechacrab_health` | 40 | Starting health. |
| `sk_mechacrab_melee_dmg` | 10 | Attack damage. |
| `sk_mechacrab_crowbar_dmg` | 20 | Damage received from club-type hits, including crowbars. |

Based on [Map Labs](https://github.com/map-labs-source/Map-Labs/commit/a58db32f67857c3b027c88e6d534b6be126f7029),
with gameplay compatibility adapted from [RTSL Source-PlusPlus](https://github.com/RunThinkShootLive/Source-PlusPlus).

</details>
</details>

## Developer information

<details>
<summary>Local builds, deployment, checks, and recovery</summary>

## Local GunMaster builds

Commit all changes, switch to `main`, then run in Git Bash:

```bash
bash build_gunmaster.sh
```

The script also works when invoked by its absolute path from another directory. It requires Git Bash, Visual Studio with
the C++ workload, MSVC v143 x86 tools, and a Windows 10/11 SDK. If VPC reports a missing Visual Studio registry key,
apply `fix-vpc-regkey.reg` once. Existing `sp` directories must be inspected and removed before starting.

The script calls VPC directly to generate `sp/src/everything.sln`, upgrades the generated solution, and rebuilds only
Client, Server, Shaders, and their dependencies in Release/x86. EZ2 excludes Shaders. MSBuild explicitly selects v143
because Visual Studio 2026's upgrade otherwise retargets generated projects to v145. Generated settings are checked
before each selected project is rebuilt.

Build orchestration uses Git for Windows utilities for paths, DLL headers, hashing, and cleanup, plus `reg.exe` for registry
queries. The build script does not launch PowerShell. Solution upgrade output is accepted as UTF-8 or converted from CP949
with Git Bash's `iconv`; MSBuild output is passed through unchanged. Both `Release|x86` and `Release|Win32` solution
configurations are supported. Stage headings are cyan in a terminal; errors and failure summaries are red.
Redirected output and build logs contain no script color codes.
The default `scripts/sc_clean.txt` is copied beside each destination `bin` directory when missing; existing configurations are preserved.

| Local branch | Projects | Destination | DLLs |
| --- | --- | --- | --- |
| `gm/ez2` | Entropy Zero 2 | `E:\GunMaster\ez2\bin` | `client.dll`, `server.dll` |
| `gm/mapbase` | HL2 and Episodic | `E:\GunMaster\mapbase\hl2\bin`, `E:\GunMaster\mapbase\episodic\bin` | `client.dll`, `server.dll`, `game_shader_dx9.dll` per variant |
| `gm/maplabs` | Episodic | `E:\GunMaster\maplabs\bin` | `client.dll`, `server.dll`, `game_shader_dx9.dll` |
| `gm/rtsl` | Episodic, RTSL | `E:\GunMaster\rtsl\bin` | `client.dll`, `server.dll`, `game_shader_dx9.dll` |

Each branch is copied immediately after its builds succeed; Mapbase waits for both variants. The script checks DLL
architecture and compares source/destination SHA-256 hashes. It creates destination folders but does not install game
content or copy PDBs or shader resources. Close games using the destination DLLs before building. A copy failure can
leave a partially updated destination; the log identifies files already copied.

**Do not edit or stage files in this checkout while the script runs.** It requires a clean checkout initially, then
discards every tracked change, including staged changes, before returning to `main`. This deliberately restores
checked-in libraries and PDBs overwritten or deleted by Rebuild. After returning to `main`, it deletes the entire
generated `sp` tree, including ignored and untracked files. It refuses recursive deletion through junctions or symbolic
links. It never automatically fetches, commits, or stashes.

On failure or Ctrl+C, the script stops subsequent branches, attempts the same cleanup, reports the failing stage and
previously copied DLLs, and preserves a nonzero exit status. Successful earlier deployments remain in place. Interactive
runs wait for a key before exiting on either success or failure; redirected/noninteractive runs exit without waiting.

Logs are stored in `gunmaster-build-logs` under `git rev-parse --absolute-git-dir`. A `gunmaster-build.lock` directory
there prevents concurrent runs. After a terminal crash or power loss, first confirm no build processes remain, inspect
the checkout, discard build changes with `git restore --source=HEAD --staged --worktree -- .`, switch to `main`, and
inspect/remove the remaining `sp` directory. Remove the stale lock only after confirming no build is running. Automatic
recovery cannot run after a forced process termination or power loss.

Check the build script's Bash syntax without building or deploying:

```bash
bash -n build_gunmaster.sh
```

This repository does not include automated build or runtime tests. A syntax check does not validate the Windows tools,
DLL builds, deployment, or in-game behavior. Validate those separately in the matching game or mod.

</details>
