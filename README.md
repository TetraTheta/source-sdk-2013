# Source SDK 2013

- Entropy Zero: <https://github.com/entropy-zero/source-sdk-2013>
- MapBase: <https://github.com/mapbase-source/source-sdk-2013>
- Map Labs: <https://github.com/map-labs-source/Map-Labs>
- RTSL: <https://github.com/RunThinkShootLive/Source-PlusPlus>

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

Run the focused checks with:

```bash
bash tests/test_build_gunmaster.sh
bash tests/test_script_output.sh
```

These checks require Git Bash, MSBuild, and Windows PowerShell only for the exclusive DLL-lock fixture. They use temporary
fixtures for target selection (including RTSL's comma with the real MSBuild CLI), failed builds, missing/invalid/locked
DLLs, copy failures, clean-tree enforcement,
staged/unstaged change disposal, safe deletion, and interrupt handling. Expected failure cases print errors; the final
line is `GunMaster checks passed.` They do not build game code or modify `E:\GunMaster`.

The script output checks use mock tools and a temporary Git repository to verify stage colors, plain logs, CP949
conversion, failure propagation, and helpers surviving checkout. They do not run the build or deploy DLLs.
