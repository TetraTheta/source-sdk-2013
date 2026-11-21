#include "player_tools_runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace gm {
namespace {
PlayerTools* gTools = nullptr;
void __fastcall frameHook(void* weapon, void*) {
  try {
    if (gTools) gTools->adminFrame(weapon);
  } catch (const std::exception& error) {
    if (gTools) gTools->fail(error.what());
  }
}
void __fastcall spawnHook(void* weapon, void*) {
  try {
    if (gTools) gTools->adminSpawn(weapon);
  } catch (const std::exception& error) {
    if (gTools) gTools->fail(error.what());
  }
}
void __fastcall precacheHook(void* weapon, void*) {
  try {
    if (gTools) gTools->adminPrecache(weapon);
  } catch (const std::exception& error) {
    if (gTools) gTools->fail(error.what());
  }
}
void __fastcall impactHook(void* weapon, void*, Trace& trace, int damageType) {
  try {
    if (gTools) gTools->adminImpact(weapon, trace, damageType);
  } catch (const std::exception& error) {
    if (gTools) gTools->fail(error.what());
  }
}
void __fastcall tracerHook(void* weapon, void*, const Vector& start, const Trace& trace, int type) {
  try {
    if (gTools) gTools->adminTracer(weapon, start, trace, type);
  } catch (const std::exception& error) {
    if (gTools) gTools->fail(error.what());
  }
}
void removeCommand(const CommandArgs& args) {
  try {
    if (gTools) gTools->command(false, args);
  } catch (const std::exception& error) {
    if (gTools) gTools->fail(error.what());
  }
}
void cleanCommand(const CommandArgs& args) {
  try {
    if (gTools) gTools->command(true, args);
  } catch (const std::exception& error) {
    if (gTools) gTools->fail(error.what());
  }
}
int completeClean(const char* partial, char output[64][64]) {
  const char* argument = std::strchr(partial, ' ');
  argument = argument ? argument + 1 : "";
  while (*argument == ' ') ++argument;
  int count = 0;
  for (const char* option : {"all", "ammo", "debris", "decals", "gibs", "powerups", "ragdolls", "small", "weapons"}) {
    if (_strnicmp(option, argument, std::strlen(argument)) == 0)
      std::snprintf(output[count++], 64, "sc_clean %s", option);
  }
  return count;
}
std::uint32_t address(const void* pointer) {
  return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}
KeyValues load(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot read GunMaster settings: " + path.string());
  const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return parseKeyValues(content);
}
using FindCommand = void*(__thiscall*)(void*, const char*);
using RegisterCommand = void(__thiscall*)(void*, void*);
}  // namespace

bool PlayerTools::install(HMODULE self, HMODULE server, HMODULE client, HMODULE engine, HMODULE vstdlib,
                          const Profile& profile, std::string& error) try {
  if (retained_ || gTools) {
    error = "Player-tool callbacks already owned";
    return false;
  }
  if (!profile.retail || !profile.commands) {
    error = "Missing reviewed player-tool profile";
    return false;
  }
  if (!retail_.initialize(server, client, engine, vstdlib, *profile.retail, error)) return false;
  commands_ = profile.commands;
  const auto root = std::filesystem::path(modulePath(self)).parent_path().parent_path();
  const auto scripts = root / L"scripts";
  const auto weapon = load(root.parent_path() / L"hl2/scripts/basehlcombatweapon.txt");
  if (std::find(weapon.begin(), weapon.end(),
                std::pair<std::string, std::string>{"WeaponData/printname", "Admin SMG"}) == weapon.end()) {
    error = "Missing Admin SMG carrier script";
    return false;
  }
  admin_ = load(scripts / L"weapon_admin.txt");
  cleanup_ = load(scripts / L"sc_clean.txt");
  auto* cvar = retail_.cvar();
  auto find = retail_.method<FindCommand>(cvar, 10);
  for (const auto& command : owned_) {
    if (find(cvar, command.name)) {
      error = std::string("Another addon owns command: ") + command.name;
      return false;
    }
  }
  Image image(server);
  const auto table = retail_.map("CBaseHLCombatWeapon").table;
  using Method = void(__thiscall*)(void*);
  const std::array<std::uint32_t, 5> slots{258, 22, 23, 113, 110};
  const std::array<Method, 5> originals{
    retail_.function<Method>(RetailFn::WeaponFrame), retail_.function<Method>(RetailFn::WeaponSpawn),
    retail_.function<Method>(RetailFn::WeaponPrecache), retail_.function<Method>(RetailFn::WeaponImpact),
    retail_.function<Method>(RetailFn::WeaponTracer)};
  const std::array<void*, 5> callbacks{reinterpret_cast<void*>(&frameHook), reinterpret_cast<void*>(&spawnHook),
                                       reinterpret_cast<void*>(&precacheHook), reinterpret_cast<void*>(&impactHook),
                                       reinterpret_cast<void*>(&tracerHook)};
  for (std::size_t i = 0; i < slots.size(); ++i) {
    if (image.read(table + slots[i] * 4) != address(reinterpret_cast<void*>(originals[i]))) {
      error = "Admin carrier virtual slot changed";
      return false;
    }
  }
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&frameHook), &retained_)) {
    error = "Cannot retain player-tool callback code";
    return false;
  }
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(originals[0]),
                          &hostRetained_)) {
    FreeLibrary(retained_);
    retained_ = nullptr;
    error = "Cannot retain player-tool host ABI";
    return false;
  }
  enabled_ = true;
  failed_ = false;
  assetsReady_ = false;
  mapWeaponsReady_ = false;
  random_.seed(GetTickCount());
  gTools = this;
  for (std::size_t i = 0; i < slots.size(); ++i) {
    const Slot entry{table + slots[i] * 4, address(reinterpret_cast<void*>(originals[i])), address(callbacks[i])};
    hooks_.push_back(entry);
    if (!writeWord(image.at(entry.rva), entry.replacement)) {
      error = "Admin callback write failed";
      restore();
      return false;
    }
  }
  using Callback = void(__cdecl*)(const CommandArgs&);
  using Completion = int(__cdecl*)(const char*, char[64][64]);
  using Construct = void*(__thiscall*)(void*, const char*, Callback, const char*, int, Completion);
  for (std::size_t i = 0; i < owned_.size(); ++i) {
    auto& command = owned_[i];
    command.object.fill(0);
    // UNREGISTERED avoids linking plugin memory into the server DLL's static constructor list.
    // The reviewed constructor still registers through the active ICvar accessor.
    retail_.function<Construct>(RetailFn::CommandCtor)(
      command.object.data(), command.name, i ? &cleanCommand : &removeCommand,
      i ? "Remove unprotected map clutter." : "Remove the aimed entity safely.", 5, i ? &completeClean : nullptr);
    command.object[5] &= ~1u;
    if (!find(cvar, command.name)) retail_.method<RegisterCommand>(cvar, 6)(cvar, command.object.data());
    if (find(cvar, command.name) != command.object.data()) {
      error = std::string("Cannot register command: ") + command.name;
      restore();
      return false;
    }
  }
  return true;
} catch (const std::exception& exception) {
  error = exception.what();
  restore();
  return false;
}

bool PlayerTools::intact() const {
  if (!retained_) return true;
  if (!enabled_ || failed_) return false;
  Image image(reinterpret_cast<HMODULE>(retail_.server()));
  if (!image.valid()) return false;
  for (const auto& hook : hooks_)
    if (image.read(hook.rva) != hook.replacement) return false;
  for (std::size_t i = 0; i < owned_.size(); ++i) {
    const auto& command = owned_[i];
    if (retail_.method<FindCommand>(retail_.cvar(), 10)(retail_.cvar(), command.name) != command.object.data() ||
        command.object[0] != image.base() + commands_->table || command.object[3] != address(command.name) ||
        command.object[6] !=
          address(i ? reinterpret_cast<void*>(&cleanCommand) : reinterpret_cast<void*>(&removeCommand)) ||
        (command.object[8] & 6) != 2)
      return false;
  }
  return true;
}

bool PlayerTools::restore() {
  enabled_ = false;
  if (!retained_) return true;
  bool restored = true;
  auto* cvar = retail_.cvar();
  for (auto& command : owned_) {
    if (!command.object[0]) continue;
    auto find = retail_.method<FindCommand>(cvar, 10);
    if (find(cvar, command.name) == command.object.data())
      retail_.method<RegisterCommand>(cvar, 7)(cvar, command.object.data());
    if (find(cvar, command.name) == command.object.data()) restored = false;
  }
  Image image(reinterpret_cast<HMODULE>(retail_.server()));
  for (auto it = hooks_.rbegin(); it != hooks_.rend(); ++it) {
    if (!image.valid() || !image.contains(it->rva, 4)) {
      restored = false;
      continue;
    }
    const auto actual = image.read(it->rva);
    if (actual == it->replacement)
      restored = writeWord(image.at(it->rva), it->original) && restored;
    else if (actual != it->original)
      restored = false;
  }
  if (!restored) return false;
  hooks_.clear();
  mapWeapons_.clear();
  explosions_.clear();
  hints_.clear();
  shot_ = {};
  gTools = nullptr;
  auto host = hostRetained_;
  hostRetained_ = nullptr;
  auto self = retained_;
  retained_ = nullptr;
  if (host) FreeLibrary(host);
  if (self) FreeLibrary(self);
  return true;
}

void PlayerTools::fail(const std::string& reason) {
  failed_ = true;
  enabled_ = false;
  using Print = void(__cdecl*)(const char*, ...);
  auto print = reinterpret_cast<Print>(GetProcAddress(GetModuleHandleW(L"tier0.dll"), "Msg"));
  if (print) print("[GunMaster HL2] WARNING: player tools disabled: %s; restart HL2.\n", reason.c_str());
}
}  // namespace gm

namespace gm {
void PlayerTools::hint(void* player, const std::string& message, void* weapon) {
  if (message.empty()) return;
  auto& state = hints_[retail_.handle(player)];
  auto* entity = retail_.resolve(state.entity);
  constexpr const char* marker = "__gunmaster_hudhint";
  const auto belongs = [&](void* candidate) {
    return candidate && !retail_.marked(candidate) && retail_.text(candidate, "m_iClassname") == "env_hudhint" &&
           retail_.text(candidate, "m_iName") == marker &&
           retail_.resolve(field<std::uint32_t>(candidate, retail_.offset("m_hOwnerEntity"))) == player;
  };
  if (!belongs(entity)) {
    entity = nullptr;
    for (auto* candidate = retail_.next(nullptr); candidate; candidate = retail_.next(candidate)) {
      if (belongs(candidate)) {
        entity = candidate;
        break;
      }
    }
  }
  if (!entity) {
    if (!slots(1)) return;
    using Create = void*(__cdecl*)(const char*, int, bool);
    entity = retail_.function<Create>(RetailFn::Create)("env_hudhint", -1, true);
    if (!entity) return;
    retail_.key(entity, "targetname", marker);
    using Spawn = int(__cdecl*)(void*);
    if (retail_.function<Spawn>(RetailFn::Spawn)(entity) < 0) return;
    using Owner = void(__thiscall*)(void*, void*);
    retail_.method<Owner>(entity, 17)(entity, player);
  }
  retail_.input(entity, "HideHudHint", player);
  state.entity = retail_.handle(entity);
  state.weapon = retail_.handle(weapon);
  state.message = message;
  state.showAt = retail_.time() + 0.15f;
  state.hideAt = 0;
  state.pending = true;
}
void PlayerTools::updateHints() {
  if (!enabled_) return;
  const auto now = retail_.time();
  for (auto it = hints_.begin(); it != hints_.end();) {
    auto& state = it->second;
    auto* player = retail_.resolve(it->first);
    auto* entity = retail_.resolve(state.entity);
    if (!retail_.isPlayer(player) || retail_.marked(entity)) {
      it = hints_.erase(it);
      continue;
    }
    if (state.pending && now >= state.showAt) {
      bool valid = true;
      if (state.weapon != 0xffffffff) {
        auto* weapon = retail_.resolve(state.weapon);
        using Alive = bool(__thiscall*)(void*);
        valid = weapon && retail_.method<Alive>(player, 65)(player) &&
                field<std::uint32_t>(player, retail_.offset("m_hActiveWeapon")) == state.weapon;
      }
      if (valid) {
        retail_.key(entity, "message", state.message);
        retail_.input(entity, "ShowHudHint", player);
        if (state.weapon == 0xffffffff) state.hideAt = now + 3;
      }
      state.pending = false;
    }
    if (state.hideAt && now >= state.hideAt) {
      retail_.input(entity, "HideHudHint", player);
      state.hideAt = 0;
    }
    ++it;
  }
}
}  // namespace gm
