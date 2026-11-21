# Retail implementation reference

This document retains the ABI and patch semantics needed to review a new DLL. Exact hashes, RVAs, signatures and named fields are owned by `profiles/profiles.json`; do not duplicate or update them here as a substitute for profile review.
The public SDK is a semantic reference, not the retail ABI. Baseline: Steam build `19307283`, Windows x86.

## Activation and rollback

`ISERVERPLUGINCALLBACKS003` has no virtual destructor slot. Identify the actual server module from gameServerFactory and require the reviewed server/client/engine/vstdlib hashes. Defer activation until the client exists. Revalidate at activation; during play monitor module identity, file stamps and owned edits once per second.

All writes are game-thread, memory-only transactions. Restore callback owners before scalar edits on pause, shutdown, rejection or unload. Restore only values still owned by GunMaster. Retain plugin/host references and disabled forwarding code if rollback fails or another detour may reference a callback. Never self-unload from its stack. A failure disables further effects until restart; a resident/listed plugin is not proof that effects are active.

Campaign module replacement is conservative: an unrecognized transition disables rather than silently adopting another module. The two autoload descriptors share one physical DLL. First-launch loading remains an observed limitation; baseline plugin_load is a no-op, so use plugin_print/logging rather than that command for diagnosis. No engine signature or secure-mode check is bypassed.

## Weapon edits

- **Spread:** GetBulletSpread returns a Vector reference. Update instruction immediates and initialized Vector words; otherwise activation after the first shot can retain stock spread. Rollback restores stock vectors even when the original memory was zero, since initialization flags may now be set. SMG1/AR2 methods use primary slot 280.
- **Kick:** AddViewKick uses slot 297. Preserve the machine-gun helper, duration and difficulty logic. Redirect only the reviewed loads to immutable in-module constants: SMG1 slide 2→1, AR2 slide 5→1 and kick 8→4. Baseline operand offsets are 33/97/117; verify preceding D9 05 instructions and pointed float values after an update.
- **Water:** native boolean fields are adjacent at 0x4b4/0x4b5. .357 initializes primary true; SMG1/AR2/shotgun initialize both. No saved-weapon repair or frame hook is used. Two secondary water-level comparisons change 3→4, preserving ownership/ammo checks.
- **Constructor growth:** preserve the original epilogue and move the SMG allocation-failure branch by seven bytes when expanding its tail. Only verified adjacent INT3 padding may be consumed. Bind constructors to class vtables: shotgun bytes also match Annabelle. Require a unique class-bound match and check incoming references in Ghidra.
- **AR2 ball:** DelayedAttack redirects its own 1000 multiplier to immutable 2000; baseline operand offset 382. Do not modify a shared constant used by other projectiles. Admin balls use a separate configured 3000 speed.

## Movement, settings and commands

Carry patches skip pickup sprint-disable and gravity-gun mass slowdown. On release, retain EnableSprint(true) and skip only the speed reset, allowing previously disabled carry state to recover. PreThink edits remove duck/ground sprint-stop blocks while retaining sticky-sprint timeout. Retail CanSprint already lacks the old SDK duck exclusion.

Both CHL2GameMovement croppers call their original first. Adjust inputs only when newly cropped and sprinting. Desired fraction is `max(0, 1 - normalSpeed * (1 - 1/3) / maximumSpeed)`; scale already-cropped input by that fraction divided by 1/3. Preserve ordinary crouch, airborne conditions and repeated-call behavior. The client reads the same in-process server normal-speed value.

| Reviewed layout | Baseline offsets |
| --- | --- |
| Movement object: move pointer / cropped flags | 8 / 0x688 |
| Move data: inputs / max-client-speed / sprint | 0x2c / 0x3c / 0xa4 |
| ConVar: name / flags / self-parent / float / integer | 12 / 20 / 28 / 44 / 48 |
| ConCommand: object size / callback / mode | 36 / 24 / 32 |
| CCommand argv | 0x408 |
| Player flags / move type | 0x104 / 0x13a |

sv_infinite_aux_power clears only FCVAR_CHEAT, preserving its value/default and restoring original flags on rollback. Name/self-parent probes are required. Native drain checks the setting; no refill or map-trigger bypass is added.

The god/noclip dispatcher intercepts only pinned command objects. Bounded on/off arguments delegate actual toggles to Valve; unrelated/bare commands forward unchanged. Preserve cheat/alive/game-state checks. The achievement patch skips only cheat-history rejection, retaining Steam/commentary and normal earning conditions; it calls no achievement setter.

## Retail bridge ABI

These are checked baseline layouts, not stable SDK promises. Re-establish changed declarations from native callers and stack cleanup.

| Structure | Required layout |
| --- | --- |
| Variant | 20 bytes: four payload words, then type at 16. AcceptInput passes it by value and pops 36 stack bytes. |
| BulletInfo | 80 bytes: shots0, source4, direction16, spread28, distance40, ammo44, tracer48, damage52, player damage56, flags60, force64, attacker68, ignore72, primary76. |
| Trace | 84 bytes: end12, fraction44, startsolid55, hit entity76. |
| Datamap | fields0, count4, class name8, base12; descriptor stride52, type0, name4, offset8, flags18. Output action-list pointer follows its 20-byte Variant. |
| Entity entry/handle | Entry stride16, pointer+4, serial+8; index mask0xfff, serial shift12. Resolve full handles, not stale pointers. |
| Client entity list | Secondary interface offset0x10014. Discover via RTTI/interface registration, not SDK sizeof. |

Runtime datamap validation requires registered names, types and offsets and rejects invalid/deep/cyclic maps. Offline tests do not run Valve constructors; they simulate only explicitly reviewed registration-owned fields when necessary.

| Interface/class | Required zero-based slots |
| --- | --- |
| ConCommand | 12 dispatch |
| CHL2GameMovement, server and client | 47 crouch crop |
| CBaseHLCombatWeapon hooks | 22 Spawn, 23 Precache, 110 MakeTracer, 113 DoImpactEffect, 258 ItemPostFrame |
| Entity operations | 2 handle, 11 datamap, 17 owner, 31 string KeyValue, 32 GetKeyValue, 36 AcceptInput, 65 alive, 80 player |
| Player attacks | 112 FireBullets, 264 shoot position, 306 muzzle flash, 365 animation, 414 autoaim |
| Weapon presentation | 235 animation, 261 idle, 366 lowering |
| VEngineCvar004 | 6 register, 7 unregister, 10 find; do not use newer ICvar ordinals |
| VEngineModel016 | 10 remove all model decals |

Shoot-position/autoaim use a hidden Vector output pointer; autoaim also takes a float. Native UTIL_Tracer takes **eight cdecl arguments**, including trailing whiz-entity, rather than the SDK's seven. LookupAttachment is thiscall/RET4.

Native ConCommand construction uses UNREGISTERED to avoid linking plugin memory into the server's static constructor list; the active ICvar accessor can still register it. Reject pre-existing command owners and explicitly unregister only the two owned commands. Its destructor does not unregister; never call global ConVar_Unregister.

## Player weapon and cleanup

The carrier reuses basehlcombatweapon allocation/networking and existing SMG models. Derived weapon tables are unchanged. Mode is stored in m_iClip2. Engine-owned explosion/hint helpers contain no plugin callback pointers and are recovered by name/owner/serial handle; pending plugin state clears on rollback.

During synchronous FireBullets, a nested/exception-safe shot context scopes carrier impact suppression and Airboat tracers. Player impact/tracer delegates forward to its active weapon; review both delegates and call sites on updates. Other calls forward to original methods. Native glass-impact paths are not fully adapted.

Airboat tracers resolve the first m_hViewModel handle and LookupAttachment("muzzle"), then send weapon entity/index to let the client substitute the animated viewmodel. Fall back to the weapon attachment if unavailable; do not guess an index. m_hViewModel's baseline offset is0xd54, but use reviewed metadata.

Explosion flags are 0x37e: repeatable, no fireball/smoke/decal/sparks/sound/fireball-smoke/particles, with NODAMAGE clear. Reapply flags to restored helpers. Do not emit a separate explosion firing cue. Cleanup debris uses SF_PHYSPROP_IS_GIB=0x400000, not 4; native fragment creation sets that bit on prop_physics.

Retail hints use message KeyValue and HideHudHint/ShowHudHint. SetHudHint is Mapbase-only. Queue replacement after 0.15 game seconds; delayed weapon hints check alive/current weapon. Cleanup hides after three seconds. No plugin think pointer is stored in an entity. Selection uses WeaponIcons/WeaponIconsSelected glyph a, bucket 2 position 2; deploy the carrier script with the DLL.

Cleanup preserves named/connected-output objects, player-associated parent/child chains and initial map weapon handles. Late/save activation conservatively protects existing weapons. Client cleanup releases exact C_ClientRagdoll instances; model and world decals use separate native paths. sc_remove intentionally permits aimed named/scripted objects, with door-areaportal handling. Remaining parity differences are listed in README.
