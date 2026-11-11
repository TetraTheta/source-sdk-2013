#!/usr/bin/env bash
set -euo pipefail

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

assert_clean() {
  local changes
  changes=$(git status --porcelain=v1 --untracked-files=all --ignore-submodules=none)
  [[ -z "$changes" ]] || fail "Working tree changed; commit or preserve your files before continuing:"$'\n'"$changes"
}

clean_sp() {
  local target links
  [[ $(git branch --show-current) == main ]] || return 1
  [[ -z $(git ls-files -- sp) ]] || return 1
  [[ ! -L "$ROOT/sp" ]] || return 1
  [[ ! -e "$ROOT/sp" ]] && return 0
  [[ -d "$ROOT/sp" ]] || return 1
  target=$(realpath -e -- "$ROOT/sp") || return $?
  [[ "$target" == "$(realpath -e -- "$ROOT")/sp" ]] || return 1
  # Git for Windows exposes both junctions and symbolic links as find's type l.
  links=$(find -P "$target" -type l -print -quit) || return $?
  [[ -z "$links" ]] || return 1
  rm -rf -- "$target"
}

discard_build_changes() {
  # The initial clean-tree check makes every subsequent tracked diff disposable.
  git restore --source=HEAD --staged --worktree -- .
}

finish() {
  local result=$? cleanup_result=0
  trap - EXIT INT TERM
  set +e
  cd "$ROOT"
  if [[ ${OWN_SP:-0} == 1 ]]; then
    discard_build_changes && git switch --no-overwrite-ignore main && clean_sp
    cleanup_result=$?
  fi
  if (( cleanup_result != 0 )); then
    echo 'Recovery incomplete: preserve changes, switch to main, and inspect sp before removing it.' >&2
    result=1
  fi
  if [[ ${LOCKED:-0} == 1 ]]; then
    rmdir "$LOCK_DIR"
  fi
  printf '\nResult: %s | branch: %s | stage: %s\n' "$result" "${BRANCH:-preflight}" "${STAGE:-preflight}"
  printf 'Copied DLLs:\n%s\n' "${COPIED:-  (none)}"
  [[ -z ${LOG:-} ]] || printf 'Log: %s\n' "$LOG"
  if [[ -t 0 ]]; then
    read -r -n 1 -s -p 'Press any key to continue...'
    echo
  fi
  exit "$result"
}

native() {
  MSYS2_ARG_CONV_EXCL='*' "$@"
}

check_dll() {
  local size offset flags
  [[ -s "$1" ]] || fail "Missing DLL: $1"
  size=$(stat -c %s -- "$1") || return $?
  (( size >= 88 )) || fail "Truncated DLL: $1"
  [[ $(od -An -tx1 -N2 -- "$1" | tr -d '[:space:]') == 4d5a ]] || fail "Invalid DOS header: $1"
  offset=$(od --endian=little -An -tu4 -j60 -N4 -- "$1") || return $?
  (( offset >= 64 && offset <= size - 24 )) || fail "Invalid PE offset: $1"
  [[ $(od -An -tx1 -j "$offset" -N6 -- "$1" | tr -d '[:space:]') == 504500004c01 ]] || fail "Expected x86 PE: $1"
  flags=$(od --endian=little -An -tu2 -j "$((offset + 22))" -N2 -- "$1") || return $?
  (( flags & 0x2000 )) || fail "Expected DLL flag: $1"
}

build_variant() {
  local variant=$1 name target project properties
  for name in Client Server Shaders; do
    [[ "$BRANCH" != gm/ez2 || "$name" != Shaders ]] || continue
    name="$name ($variant)"
    # MSBuild replaces solution project name punctuation with underscores.
    target=$(printf '%s' "$name" | sed 's/[%$@;.()\x27]/_/g')
    grep -Fq "= \"$name\", " everything.sln || fail "Project not found: $name"
    project=$(grep -F "= \"$name\", " everything.sln | cut -d '"' -f 6 | tr -d '\r')
    project=$(cygpath -u "$project")
    grep -Fq 'Release|Win32' "$project" || fail "Release/Win32 project configuration missing: $project"
    STAGE="Check settings: $name"
    properties=$(native "$MSBUILD" "$project" -nologo '-property:Configuration=Release;Platform=Win32;PlatformToolset=v143' -getProperty:PlatformToolset,WindowsTargetPlatformVersion,VCToolsVersion) || return $?
    printf '%s\n' "$properties"
    grep -Eq '"PlatformToolset": "v143"' <<< "$properties" || fail "Expected effective v143 toolset: $name"
    grep -Eq '"WindowsTargetPlatformVersion": "[0-9][^"]*"' <<< "$properties" || fail "Windows SDK is missing: $name"
    grep -Eq '"VCToolsVersion": "14\.[0-9][^"]*"' <<< "$properties" || fail "MSVC tools are missing: $name"
    STAGE="Rebuild $name"
    # Keep literal quotes so MSBuild does not split RTSL's comma into two targets.
    native "$MSBUILD" everything.sln "-target:\"$target:Rebuild\"" '-property:Configuration=Release;Platform=x86;PlatformToolset=v143' -maxCpuCount:1 -nodeReuse:false -verbosity:minimal || return $?
  done
}

copy_variant() {
  local source=$1 destination=$2 dll expected actual
  local -a dlls=(client.dll server.dll)
  [[ "$BRANCH" == gm/ez2 ]] || dlls+=(game_shader_dx9.dll)
  for dll in "${dlls[@]}"; do
    check_dll "$ROOT/sp/game/$source/bin/$dll" || return $?
  done
  mkdir -p "$destination" || return $?
  for dll in "${dlls[@]}"; do
    expected=$(sha256sum "$ROOT/sp/game/$source/bin/$dll" | cut -d ' ' -f 1) || return $?
    cp -- "$ROOT/sp/game/$source/bin/$dll" "$destination/$dll" || return $?
    COPIED+="  $destination/$dll"$'\n'
    actual=$(sha256sum "$destination/$dll" | cut -d ' ' -f 1) || return $?
    [[ "$expected" == "$actual" ]] || fail "Copy verification failed: $destination/$dll"
    printf 'SHA256 %s  %s\n' "$actual" "$destination/$dll"
  done
}

main() {
  OWN_SP=0
  LOCKED=0
  COPIED=''
  LOG=''
  BRANCH=preflight
  STAGE=preflight
  ROOT=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
  cd "$ROOT"
  trap finish EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  [[ $(uname -s) == MINGW* ]] || fail 'Run this script with Git Bash on Windows.'
  LOCK_DIR="$(git rev-parse --absolute-git-dir)/gunmaster-build.lock"
  mkdir "$LOCK_DIR" || fail "Build lock exists: $LOCK_DIR (remove only after confirming no build is running)."
  LOCKED=1
  [[ $(git branch --show-current) == main ]] || fail 'Start on main.'
  assert_clean
  [[ ! -e sp && ! -L sp ]] || fail 'Existing sp directory: inspect and remove it manually first.'
  local branch
  for branch in gm/ez2 gm/mapbase gm/maplabs gm/rtsl; do
    git show-ref --verify --quiet "refs/heads/$branch" || fail "Missing local branch: $branch"
  done
  local vswhere='/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' installation
  [[ -f "$vswhere" ]] || fail 'vswhere.exe is missing.'
  installation=$(native "$vswhere" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | tr -d '\r')
  [[ -n "$installation" ]] || fail 'Visual Studio C++ build tools are missing.'
  installation=$(cygpath -u "$installation")
  MSBUILD="$installation/MSBuild/Current/Bin/MSBuild.exe"
  DEVENV="$installation/Common7/IDE/devenv.com"
  [[ -f "$MSBUILD" && -f "$DEVENV" ]] || fail 'MSBuild.exe or devenv.com is missing.'
  local registry kits
  registry=$(native reg.exe query 'HKLM\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\10.0\Projects\{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}' /v DefaultProjectExtension) || fail 'Apply fix-vpc-regkey.reg first.'
  grep -Eq 'REG_SZ[[:space:]]+vcproj[[:space:]]*$' <<< "$registry" || fail 'Apply fix-vpc-regkey.reg first.'
  compgen -G "$installation/MSBuild/Microsoft/VC/*/Platforms/Win32/PlatformToolsets/v143/Toolset.props" >/dev/null || fail 'Install MSVC v143 x86 tools.'
  kits=$(native reg.exe query 'HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots' /v KitsRoot10 | sed -n 's/.*REG_SZ[[:space:]]*//p' | tr -d '\r') || fail 'Windows SDK registry entry is missing.'
  [[ -n "$kits" ]] || fail 'Windows SDK registry entry is missing.'
  kits=$(cygpath -u "$kits")
  compgen -G "${kits%/}/Lib/*/um/x86/kernel32.lib" >/dev/null || fail 'Install Windows 10/11 SDK.'
  local log_dir="$(git rev-parse --absolute-git-dir)/gunmaster-build-logs"
  mkdir -p "$log_dir"
  LOG="$log_dir/$(date +%Y%m%d-%H%M%S)-$$.log"
  exec > >(tee -i -a "$LOG") 2>&1
  local -a games
  for BRANCH in gm/ez2 gm/mapbase gm/maplabs gm/rtsl; do
    STAGE=checkout
    assert_clean
    git switch --no-overwrite-ignore "$BRANCH"
    OWN_SP=1
    cd "$ROOT/sp/src"
    case "$BRANCH" in
      gm/ez2) games=(/ez2) ;;
      gm/mapbase) games=(/hl2 /episodic) ;;
      *) games=(/episodic) ;;
    esac
    STAGE=VPC
    native ./devtools/bin/vpc.exe "${games[@]}" +everything /mksln everything.sln
    [[ -s everything.sln ]] || fail 'VPC did not generate everything.sln.'
    STAGE=upgrade
    native "$DEVENV" everything.sln /upgrade
    grep -Fq 'Release|x86' everything.sln || fail 'Release/x86 solution configuration is missing.'
    case "$BRANCH" in
      gm/ez2) build_variant 'Entropy Zero 2' ;;
      gm/mapbase) build_variant HL2; build_variant Episodic ;;
      gm/maplabs) build_variant Episodic ;;
      gm/rtsl) build_variant 'Episodic, RTSL' ;;
    esac
    STAGE=copy
    case "$BRANCH" in
      gm/ez2) copy_variant mod_ez2 /e/GunMaster/ez2/bin ;;
      gm/mapbase)
        local dll
        for dll in client.dll server.dll game_shader_dx9.dll; do
          check_dll "$ROOT/sp/game/mod_hl2/bin/$dll"
          check_dll "$ROOT/sp/game/mod_episodic/bin/$dll"
        done
        copy_variant mod_hl2 /e/GunMaster/mapbase/hl2/bin
        copy_variant mod_episodic /e/GunMaster/mapbase/episodic/bin
        ;;
      gm/maplabs) copy_variant mod_episodic /e/GunMaster/maplabs/bin ;;
      gm/rtsl) copy_variant mod_episodic /e/GunMaster/rtsl/bin ;;
    esac
    STAGE=cleanup
    cd "$ROOT"
    discard_build_changes
    git switch --no-overwrite-ignore main
    clean_sp
    OWN_SP=0
  done
  STAGE=complete
  assert_clean
}

# Read the complete program before checkout removes this main-only script.
main "$@"
