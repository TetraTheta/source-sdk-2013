#include "cheat_commands.hpp"

#include <cstring>

namespace gm {
namespace {
Commands* gCommands = nullptr;

void __fastcall dispatchHook(void* command, void*, const CommandArgs& args) {
  if (gCommands) gCommands->dispatch(command, args);
}

std::uint32_t hookAddress() { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&dispatchHook)); }
}  // namespace

ToggleAction toggleAction(int count, const char* argument, bool enabled) {
  if (count == 1) return ToggleAction::Toggle;
  if (count != 2 || !argument) return ToggleAction::Usage;
  if (_stricmp(argument, "on") == 0) return enabled ? ToggleAction::Keep : ToggleAction::Toggle;
  if (_stricmp(argument, "off") == 0) return enabled ? ToggleAction::Toggle : ToggleAction::Keep;
  return ToggleAction::Usage;
}

bool Commands::identities() const {
  Image image(module_);
  if (!image.valid() || !image.contains(spec_.table, 13 * 4) || !image.executable(spec_.dispatch, 1) ||
      !image.executable(spec_.getPlayer, 1))
    return false;
  for (const auto& command : {spec_.god, spec_.noclip}) {
    if (!image.contains(command.object, 36) || !image.contains(command.name, 1) ||
        !image.executable(command.callback, 1) || image.read(command.object) != image.base() + spec_.table ||
        image.read(command.object + 12) != image.base() + command.name ||
        image.read(command.object + 24) != image.base() + command.callback ||
        !(image.read(command.object + 20) & 0x4000) || (image.read(command.object + 32) & 6))
      return false;
  }
  return true;
}

bool Commands::install(HMODULE module, const CommandsSpec& spec, std::string& error) {
  if (module_ || gCommands) {
    error = "Console hook already owned";
    return false;
  }
  module_ = module;
  spec_ = spec;
  Image image(module);
  if (!identities() || image.read(spec_.table + 48) != image.base() + spec_.dispatch) {
    module_ = nullptr;
    error = "Registered console command identity/dispatcher changed";
    return false;
  }
  // Keep callback code resident if rollback ever fails before the engine
  // unloads us.
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&dispatchHook),
                          &retained_)) {
    module_ = nullptr;
    error = "Cannot retain console-hook code";
    return false;
  }
  gCommands = this;
  enabled_ = true;
  if (!writeWord(image.at(spec_.table + 48), hookAddress())) {
    error = "Console hook write failed; rolling back";
    restore();
    return false;
  }
  return true;
}

bool Commands::intact() const {
  return !module_ || (enabled_ && identities() && Image(module_).read(spec_.table + 48) == hookAddress());
}

bool Commands::restore() {
  enabled_ = false;
  if (!module_) return true;
  Image image(module_);
  if (!image.valid() || !image.contains(spec_.table + 48, 4)) return false;
  const auto original = static_cast<std::uint32_t>(image.base() + spec_.dispatch);
  const auto actual = image.read(spec_.table + 48);
  if (actual == hookAddress()) {
    if (!writeWord(image.at(spec_.table + 48), original)) return false;
  } else if (actual != original) {
    // shortcut: a foreign detour may retain our callback; stay resident until
    // restart.
    return false;
  }
  gCommands = nullptr;
  module_ = nullptr;
  HMODULE retained = retained_;
  retained_ = nullptr;
  if (retained) FreeLibrary(retained);
  return true;
}

void Commands::dispatch(void* command, const CommandArgs& args) {
  Image image(module_);
  using Dispatch = void(__thiscall*)(void*, const CommandArgs&);
  const auto original = reinterpret_cast<Dispatch>(image.at(spec_.dispatch));
  const bool god = command == image.at(spec_.god.object);
  const bool noclip = command == image.at(spec_.noclip.object);
  if (!enabled_ || (!god && !noclip) || args.count == 1) {
    original(command, args);
    return;
  }
  const char* argument = nullptr;
  if (args.count == 2) {
    const auto pointer = reinterpret_cast<std::uintptr_t>(args.argv[1]);
    const auto begin = reinterpret_cast<std::uintptr_t>(args.tokens);
    if (pointer >= begin && pointer < begin + sizeof(args.tokens) &&
        std::memchr(args.argv[1], 0, begin + sizeof(args.tokens) - pointer))
      argument = args.argv[1];
  }
  if (toggleAction(args.count, argument, false) == ToggleAction::Usage) {
    using Print = void(__cdecl*)(const char*, ...);
    auto print = reinterpret_cast<Print>(GetProcAddress(GetModuleHandleW(L"tier0.dll"), "Msg"));
    if (print) print("Usage: %s [on|off]\n", god ? "god" : "noclip");
    return;
  }
  if (!identities()) {
    enabled_ = false;
    return;
  }
  using GetPlayer = unsigned char*(__cdecl*)();
  auto* player = reinterpret_cast<GetPlayer>(image.at(spec_.getPlayer))();
  if (!player) return;
  std::uint32_t flags = 0;
  std::memcpy(&flags, player + spec_.playerFlags, sizeof(flags));
  const bool on = god ? (flags & 0x4000) != 0 : player[spec_.playerMoveType] == 8;
  if (toggleAction(args.count, argument, on) == ToggleAction::Toggle) original(command, args);
}
}  // namespace gm
