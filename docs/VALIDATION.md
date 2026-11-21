# Validation record

Baseline: Steam build `19307283`, Windows x86. Plugin version: **1.0**.
Current packaged DLL SHA-256: `e967e142788b066cfb58fe9a156fa6b269b5a35a1432fcf31ecc05acda02fb6f`.

## Automated checks

The current runtime source passed a clean MSVC v143 x86 build, retained native maintenance checks, twelve Python discovery/generation tests, Ruff and installed-profile audit.
All eight server/client/shared DLL offline mappings passed. The mapper applies relocations without loading imports, running entry points or executing Valve functions.

Checks cover signature/operand uniqueness, class/vtable binding, datamap rejection, constructor encodings, callback ABI/ownership/rollback, hashes and plugin exports.
These results do not establish gameplay, rendering, Steam autoload, save compatibility or achievement success. The build does not launch HL2 or deploy to Steam.

## Gameplay evidence

Reports from earlier artifacts establish only the behavior described, not a full pass for the current packaged DLL.

| Feature | Reported result | Remaining validation |
| --- | --- | --- |
| SMG1/AR2 spread and recoil | Confirmed in HL2 and EP1. | Current-artifact regression and EP2. |
| Underwater fire and AR2 ball speed | Confirmed in HL2. | EP1/EP2 underwater cases and current-artifact regression. |
| Crouched sprint | Worked elsewhere on the same map after a reported lockout near a force-crouch trigger. | Energy/prediction cases and separate campaign checks; no power bug was isolated. |
| Startup | First launch after a DLL replacement can miss the plugin; restart or campaign switching can recover it. | Reliable fresh-start loading; cause remains unconfirmed. |
| Admin SMG and cleanup presentation | Earlier tests exposed impact, explosion, tracer, hint, icon and debris defects. Corrective code is present. | Latest effects, muzzle placement, hints/icons and debris removal require confirmation. |
| Auxiliary power, carry sprint, command arguments and achievements | No complete recorded gameplay pass. | Test each feature under its normal conditions. |
| Saves, transitions and third-party content | No complete recorded pass. | Save/load, cross-campaign/menu transitions, Workshop and longer play. |

The crouch-sprint observation did not lead to energy refills or trigger bypasses. The uncertain Admin ball-speed observation did not lead to speed changes or diagnostic code.

## Manual checklist

Test on copied saves in HL2, EP1 and EP2:

1. Confirm plugin_print and ACTIVE, including a fresh start and campaign transitions.
2. Compare weapon spread/recoil, ammo, reload and secondary attacks; use newly obtained weapons for underwater checks.
3. Check standing/crouched sprint, carry/drop, sticky sprint, low suit power and auxiliary-power settings away from map-imposed restrictions.
4. Exercise god/noclip bare/on/off behavior and cheat restrictions. Verify achievements only through normal gameplay.
5. Test Admin primary, all five modes, R hints, selection icons, silent Explosion and Airboat muzzle tracers; check save/load and weapon switching.
6. Test each cleanup category, decals/ragdolls, and protection of named/scripted/player-owned objects. Use disposable objects for aimed removal.

Record campaign/map, plugin/module hashes, warnings and observed results when new tests are reported. Keep personal paths, process IDs, full logs and investigation databases outside repository documentation. Add concise outcomes rather than repeating the investigation history.
