# GunMaster 1.0 for Steam Half-Life 2

GunMaster adds weapon adjustments, crouched sprinting, an optional Admin SMG, and cleanup tools to **Half-Life 2, Episode One, and Episode Two** on Windows.
You need the original Steam game and its content installed. GunMaster leaves the original game DLLs in place.

## Install

1. Close Half-Life 2.
2. Copy **the contents of this repository's `Half-Life 2` folder** into your Steam `Half-Life 2` installation folder, beside `hl2.exe`. Merge the folders.
3. Launch the game through Steam and open a map.
4. Enter `plugin_print` in the developer console to check that GunMaster is loaded. An `[GunMaster HL2] ACTIVE` message also confirms activation.

Copy the whole folder, including its scripts. If another addon already supplies any of the same files, back them up before replacing them.
No build or launch-option change is required.

**Known startup issue:** the first launch after replacing GunMaster's DLL may miss the plugin. Close and restart the game, or switch campaigns, then check `plugin_print` again.

## Features

- **Adjusted weapon handling:** tighter SMG1/AR2 spread and reduced AR2 kick.
- **Underwater firing:** use the .357, SMG1, AR2, and shotgun underwater, including supported alternate attacks.
- **Faster AR2 energy balls:** player-fired Combine Balls travel twice as fast.
- **Sprint while crouched or carrying objects:** ordinary suit, energy, and map restrictions still apply.
- **Achievements with cheats:** using cheats no longer triggers the achievement manager's cheat-history restriction. Each achievement still requires its normal conditions.
- **Optional Admin SMG:** rapid, ammo-free fire and five alternate-fire modes using the existing SMG models.
- **Cleanup tools:** remove an aimed object or clear selected kinds of clutter.
- **Convenient settings:** unlimited auxiliary power without cheats, plus explicit god/noclip on/off commands.

<details>
<summary>Console commands, Admin SMG controls, and settings</summary>

Enable the developer console in the game's keyboard options.

### Admin SMG

```text
sv_cheats 1
give basehlcombatweapon
```

- **Primary fire:** rapid, high-damage bullet fire without ammunition.
- **Secondary fire:** use the selected mode: Explosion, Airboat Gun, Combine Ball, Crossbow Bolt, or Armed Grenade.
- **Reload:** cycle modes while the weapon is raised and its attack cooldown has elapsed. The selected mode appears as a HUD hint.

This build uses `give basehlcombatweapon`, rather than `give weapon_admin`. Explosion attacks have no separate firing sound.
Admin SMG is intended for player use; NPC support is not implemented.

`gunmaster/scripts/weapon_admin.txt` controls attack values and the starting mode. Admin Combine Ball speed defaults to 3000; ordinary AR2 alternate fire uses 2000.
Settings are read on activation/map initialization. Restart the game after changing weapon scripts so the client and server reload them.

### Cleanup

| Command | Effect |
| --- | --- |
| `sc_remove` | Remove the object under the crosshair. |
| `sc_clean all` | Clean all supported categories. |
| `sc_clean ammo` | Remove eligible ammo pickups. |
| `sc_clean debris` | Remove eligible physics fragments, such as broken boards and barrel pieces. |
| `sc_clean decals` | Clear world and model decals. |
| `sc_clean gibs` | Remove eligible gibs. |
| `sc_clean powerups` | Remove eligible health and suit-power pickups. |
| `sc_clean ragdolls` | Remove eligible server ragdolls and release client ragdolls. |
| `sc_clean small` | Remove eligible props matching the model whitelist. |
| `sc_clean weapons` | Remove eligible unowned weapons while preserving original map weapons. |

Cleanup commands do not require cheats. Cleanup skips protected objects, including named/script-connected objects and player equipment, so it may leave some visible clutter.
Weapons already present in a loaded save may also be preserved. Results appear as a HUD hint.

**Save before removing objects.** `sc_remove` can remove doors or other objects needed for progression.

`gunmaster/scripts/sc_clean.txt` contains the small-prop model whitelist; `*` matches any sequence of characters. Restart or change maps after changing these settings.

### Other commands

| Command | Effect | Requires `sv_cheats 1`? |
| --- | --- | --- |
| `sv_infinite_aux_power 1` | Enable unlimited auxiliary power; `0` restores normal use. The default is unchanged. | No |
| `god on` / `god off` | Enable or disable god mode. Bare `god` still toggles it. | Yes |
| `noclip on` / `noclip off` | Enable or disable noclip. Bare `noclip` still toggles it. | Yes |

### Weapon adjustments

| Change | SMG1 | AR2 |
| --- | --- | --- |
| Bullet spread | 5 degrees → 1 degree | 3 degrees → 1 degree |
| Maximum upward kick | 1 degree, unchanged | 8 degrees → 4 degrees |
| Recoil slide limit | 2 seconds → 1 second | 5 seconds → 1 second |

Underwater changes cover .357 primary fire and SMG1/AR2/shotgun primary and secondary fire.
Use newly obtained weapons when testing: weapons restored from older saves can retain their original underwater flags.

</details>

## Known limits

- A Steam update may disable GunMaster until support for the changed DLLs is added. Check the console or `gunmaster/bin/gunmaster.log` for warnings.
- NPC Admin SMG support, SLAM capacity changes, and some features of the other GunMaster builds are not included.
- Maps can impose movement restrictions, including force-crouch triggers. Crouched sprinting does not bypass those restrictions.
- The latest fixes and full campaign/mod compatibility still need gameplay confirmation. See the [validation record](docs/VALIDATION.md).

<details>
<summary>Comparison with the other GunMaster builds</summary>

This compares GunMaster changes on `gm/mapbase`, `gm/maplabs`, `gm/ez2`, and `gm/rtsl`, rather than all features of their underlying games or mods.
**Implemented** describes code coverage; it does not certify every campaign or edge case.

| Feature | Steam HL2 status |
| --- | --- |
| SMG1/AR2 adjustments, underwater firing, faster AR2 balls | Implemented. EZ2-specific SMG2/AR2Proto changes are not included. |
| Crouched sprint and sprint while carrying objects | Implemented; normal suit and map restrictions remain. |
| Non-cheat auxiliary-power setting | Implemented without changing its default. EZ2's separate sprint/flashlight settings are not added. |
| god/noclip arguments and achievement cheat-history change | Implemented. |
| Player Admin SMG | Core attacks, five modes, settings and saved mode implemented through `basehlcombatweapon`. |
| NPC Admin SMG | Not implemented. |
| Admin-specific effects and physics | Partial: impact suppression and Airboat muzzle tracers exist; direct floor-turret force, missile defense and temporary AI danger-sound reuse are not ported. |
| Admin projectiles | Partial: custom crossbow damage is absent, and ball/bolt/grenade spawn placement uses wall-checked eye placement instead of the other builds' muzzle/hull method. |
| Admin hints and selection icons | Native hints and SMG icons; custom heading styling/messages are absent. Explosion silence is intentional for this build. |
| `sc_remove` | Implemented with protection checks and door-areaportal handling. |
| `sc_clean` | All categories implemented; map-weapon protection is conservative, and derived client ragdoll types may remain. |
| General weapon-pickup autoswitch policy | Not ported. The Admin script requests no automatic switch away, but the other branches' general policy change is absent. |
| Dedicated UI messages and original sound-volume table | Not ported; stock cues and native hints are used. |
| `sk_max_slam` and first-launch resolution defaults | Not ported. |
| Invalid damage-filter reference defenses | Not ported; applicability to the retail game has not been established. |
| EZ2 alternative console, damage-view-kick changes, `dropprimary` suppression and NPC/console fixes | Not ported. |
| RTSL Stasis Crystal/freezing, MechaCrabs, zombie flags, outlines, chapter titles and shadow defaults | Not ported. |
| Mapbase/Map Labs upstream VScript, entities and rendering features | Not supplied by this plugin. |

<details>
<summary>Comparison baseline for maintainers</summary>

Reviewed branch tips and their upstream baselines:

| Branch | Tip | Baseline |
| --- | --- | --- |
| `gm/mapbase` | `a6e14ac77df5` | `upstream/mapbase`: `d9fd33535849` |
| `gm/maplabs` | `538b4611d880` | `upstream/maplabs`: `a58db32f6785` |
| `gm/ez2` | `0af2d1f52b05` | `upstream/ez2`: `4c4fea9c4f86` |
| `gm/rtsl` | `2b8a533bede8` | `upstream/maplabs`: `a58db32f6785`, including RTSL adaptations |

Use `git log --no-merges BASE..BRANCH` and `git diff -w BASE BRANCH` to refresh the comparison. Source-format and build-only edits are not gameplay features.

</details>
</details>

## Uninstall

Close the game and remove the files supplied by GunMaster:

- `gunmaster/bin/gunmaster.dll` and its optional `gunmaster.log`
- `gunmaster/scripts/weapon_admin.txt` and `gunmaster/scripts/sc_clean.txt`
- `hl2/addons/gunmaster.vdf`
- `hl2_complete/addons/gunmaster.vdf`
- `hl2/scripts/basehlcombatweapon.txt`

Restore any addon files you backed up. There are no original Valve DLLs to restore.

<details>
<summary>Build, compatibility checks, and maintenance</summary>

Install the Visual Studio C++ tools with MSVC v143 x86 and a Windows SDK, then run:

```bat
build_hl2.cmd
```

The script builds the plugin, runs maintenance checks and updates the copy-ready DLL. It does not copy files into Steam or launch the game.
Python is needed for signature-update tools, not for ordinary building or play.

GunMaster validates the actual server/client/engine/vstdlib DLLs against reviewed hashes and signatures. Unknown builds are rejected; a detected change disables the hooks and restores owned edits where possible.
The current baseline is Steam build `19307283`. Lost Coast and other builds are unsupported.

For DLL updates, start with [AGENT.md](AGENT.md). [Implementation notes](docs/IMPLEMENTATION.md) retain the necessary retail ABI details; [validation](docs/VALIDATION.md) separates offline checks from gameplay reports.
Generated `build/` and tool caches may be deleted after checking. Deployment files under `Half-Life 2/` must remain available.

</details>
