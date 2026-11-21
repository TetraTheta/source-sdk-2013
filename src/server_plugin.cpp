#include <array>
#include <cstdio>
#include <cstring>
#include <exception>

#include "cheat_commands.hpp"
#include "player_tools_runtime.hpp"
#include "runtime_patches.hpp"
#include "sprint_movement.hpp"

namespace {
using Factory = void*(__cdecl*)(const char*, int*);
using ConsolePrint = void(__cdecl*)(const char*, ...);
HMODULE gSelf = nullptr;

void log(const std::string& message) {
  const std::string line = "[GunMaster HL2] " + message + " [PID " + std::to_string(GetCurrentProcessId()) + "]\n";
  auto print = reinterpret_cast<ConsolePrint>(GetProcAddress(GetModuleHandleW(L"tier0.dll"), "Msg"));
  if (print) print("%s", line.c_str());
  OutputDebugStringA(line.c_str());
  auto path = gm::modulePath(gSelf);
  const auto slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return;
  // Logging is confined to the added plugin directory, never the player's cfg
  // or saves.
  path = path.substr(0, slash) + L"\\gunmaster.log";
  FILE* file = nullptr;
  if (_wfopen_s(&file, path.c_str(), L"ab") == 0 && file) {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u %s", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                 time.wSecond, line.c_str());
    std::fclose(file);
  }
}

struct Module {
  HMODULE handle = nullptr;
  std::wstring path;
  WIN32_FILE_ATTRIBUTE_DATA stamp{};
};

bool sameStamp(const WIN32_FILE_ATTRIBUTE_DATA& a, const WIN32_FILE_ATTRIBUTE_DATA& b) {
  return a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
         CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
}

// The virtual order is the public IServerPluginCallbacks003 ABI; it has no
// destructor slot. Only opaque arguments cross this boundary; no SDK entity
// layout is compiled into the plugin.
class GunMaster final {
 public:
  bool isLoaded() const { return loaded_; }
  virtual bool Load(Factory, Factory serverFactory) {
    // The engine calls Unload after a failed duplicate Load on the same
    // singleton.
    if (loaded_) {
      skipUnload_ = true;
      log("WARNING: duplicate load rejected");
      return false;
    }
    disabled_ = false;
    skipUnload_ = false;
    paused_ = false;
    client_ = {};
    try {
      log(
        "Version 1.0: player Admin SMG and protected cleanup; in-game "
        "validation pending");
      HMODULE server = nullptr;
      if (!serverFactory ||
          !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(serverFactory), &server) ||
          server != GetModuleHandleW(L"server.dll")) {
        return reject("Cannot identify the calling server.dll from gameServerFactory");
      }
      std::string hash;
      if (!gm::hashFile(gm::modulePath(server), hash)) return reject("Cannot read server.dll SHA-256");
      profile_ = gm::findProfile(hash);
      if (!profile_)
        return reject("UNSUPPORTED server.dll SHA-256=" + hash +
                      "; update the reviewed profile, DO NOT bypass the guard");
      if (!verify(server_, server, profile_->serverHash, "server.dll") ||
          !verify(engine_, GetModuleHandleW(L"engine.dll"), profile_->engineHash, "engine.dll"))
        return false;
      loaded_ = true;
      log(std::string("Accepted server profile: ") + profile_->name);
      return activate();
    } catch (const std::exception& error) {
      return reject(std::string("Initialization exception: ") + error.what());
    }
  }

  virtual void Unload() {
    if (skipUnload_) {
      skipUnload_ = false;
      return;
    }
    if (!restore()) log("WARNING: rollback incomplete; restart HL2 before continuing");
    if (loaded_) log("Unloaded; GunMaster memory patches removed");
    loaded_ = false;
    disabled_ = true;
    profile_ = nullptr;
  }
  virtual void Pause() {
    paused_ = true;
    if (!restore()) reject("Pause rollback failed; restart HL2");
    log("Paused; patches removed");
  }
  virtual void UnPause() {
    paused_ = false;
    if (!disabled_ && loaded_) activate();
  }
  virtual const char* GetPluginDescription() { return "GunMaster HL2 1.0 (guarded runtime hooks)"; }
  virtual void LevelInit(const char* map) {
    if (!loaded_ || disabled_ || paused_) return;
    log(std::string("Map: ") + (map ? map : "(unknown)"));
    if (!restore()) {
      reject("Map-change rollback failed; restart HL2");
      return;
    }
    activate();
  }
  virtual void ServerActivate(void*, int, int) {
    if (!loaded_ || disabled_ || paused_) return;
    try {
      if (!patches_.active() && !activate()) return;
      tools_.captureMapWeapons();
    } catch (const std::exception& error) {
      reject(error.what());
    }
  }
  virtual void GameFrame(bool) {
    if (!loaded_ || disabled_ || paused_) return;
    try {
      tools_.updateHints();
    } catch (const std::exception& error) {
      reject(error.what());
      return;
    }
    if (GetTickCount64() < nextCheck_) return;
    nextCheck_ = GetTickCount64() + 1000;
    try {
      if (!unchanged(server_, L"server.dll") || !unchanged(engine_, L"engine.dll") ||
          (client_.handle && !unchanged(client_, L"client.dll")) || !patches_.intact() || !commands_.intact() ||
          !movement_.intact() || !tools_.intact() || (vstdlib_.handle && !unchanged(vstdlib_, L"vstdlib.dll"))) {
        reject(
          "Module/file identity or installed patch changed; disabled "
          "until HL2 restarts");
        return;
      }
      if (!patches_.active()) activate();
    } catch (const std::exception& error) {
      reject(std::string("Runtime validation exception: ") + error.what());
    }
  }
  virtual void LevelShutdown() {
    if (!restore()) reject("Level-shutdown rollback failed; restart HL2");
  }
  virtual void ClientActive(void*) {}
  virtual void ClientDisconnect(void*) {}
  virtual void ClientPutInServer(void*, const char*) {}
  virtual void SetCommandClient(int) {}
  virtual void ClientSettingsChanged(void*) {}
  virtual int ClientConnect(bool*, void*, const char*, const char*, char*, int) { return 0; }
  virtual int ClientCommand(void*, const void*) { return 0; }
  virtual int NetworkIDValidated(const char*, const char*) { return 0; }
  virtual void OnQueryCvarValueFinished(int, void*, int, const char*, const char*) {}
  virtual void OnEdictAllocated(void*) {}
  virtual void OnEdictFreed(const void*) {}

 private:
  bool restore() {
    const bool toolsRestored = tools_.restore();
    const bool movementRestored = movement_.restore();
    const bool commandsRestored = commands_.restore();
    return patches_.restore() && commandsRestored && movementRestored && toolsRestored;
  }
  bool reject(const std::string& reason) {
    disabled_ = true;
    const bool restored = restore();
    log("WARNING: " + reason + ". NO FURTHER GUNMASTER PATCHES WILL RUN.");
    if (!restored) log("WARNING: rollback incomplete; close and restart HL2");
    // A failed console-hook rollback retains the DLL and forwards to Valve's
    // dispatcher.
    return false;
  }
  bool verify(Module& module, HMODULE handle, const char* expected, const char* name) {
    if (!handle) return reject(std::string("Missing ") + name);
    const auto path = gm::modulePath(handle);
    std::string actual;
    WIN32_FILE_ATTRIBUTE_DATA before{}, after{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &before) ||
        !gm::hashFile(path, actual) || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &after) ||
        !sameStamp(before, after)) {
      return reject(std::string("Cannot obtain a stable SHA-256 for ") + name);
    }
    if (actual != expected) return reject(std::string("UNSUPPORTED ") + name + " SHA-256=" + actual);
    module = {handle, path, after};
    log(std::string("Verified ") + name + " SHA-256=" + actual);
    return true;
  }
  bool unchanged(const Module& module, const wchar_t* name) const {
    WIN32_FILE_ATTRIBUTE_DATA stamp{};
    return module.handle && module.handle == GetModuleHandleW(name) && gm::modulePath(module.handle) == module.path &&
           GetFileAttributesExW(module.path.c_str(), GetFileExInfoStandard, &stamp) && sameStamp(module.stamp, stamp);
  }
  bool activate() {
    if (disabled_ || paused_ || patches_.active()) return !disabled_;
    if (!unchanged(server_, L"server.dll") || !unchanged(engine_, L"engine.dll"))
      return reject("Server/engine changed before activation");
    if (!verify(server_, server_.handle, profile_->serverHash, "server.dll") ||
        !verify(engine_, engine_.handle, profile_->engineHash, "engine.dll"))
      return false;
    HMODULE client = GetModuleHandleW(L"client.dll");
    if (!client) {
      log("Waiting for client.dll; no patches installed");
      return true;
    }
    if (!verify(client_, client, profile_->clientHash, "client.dll")) return false;
    if (!profile_->retail ||
        !verify(vstdlib_, GetModuleHandleW(L"vstdlib.dll"), profile_->retail->vstdlibHash, "vstdlib.dll"))
      return reject("Unsupported player-tool runtime");
    std::string error;
    if (!patches_.install(server_.handle, *profile_, error)) return reject(error);
    if (!profile_->commands || !commands_.install(server_.handle, *profile_->commands, error)) return reject(error);
    if (!profile_->movement || !movement_.install(server_.handle, client, *profile_->movement, error))
      return reject(error);
    if (!tools_.install(gSelf, server_.handle, client, engine_.handle, vstdlib_.handle, *profile_, error))
      return reject(error);
    log(
      "ACTIVE: weapon handling/water fire; AR2 ball=2000; auxiliary-power "
      "setting; carry/crouch sprint; god/noclip [on|off]; achievements "
      "ignore cheat history. player Admin SMG: give basehlcombatweapon; "
      "sc_remove; sc_clean.");
    return true;
  }

  gm::Patches patches_;
  gm::Commands commands_;
  gm::Movement movement_;
  gm::PlayerTools tools_;
  const gm::Profile* profile_ = nullptr;
  Module server_, client_, engine_, vstdlib_;
  ULONGLONG nextCheck_ = 0;
  bool loaded_ = false;
  bool disabled_ = false;
  bool paused_ = false;
  bool skipUnload_ = false;
};

GunMaster gPlugin;
}  // namespace

extern "C" __declspec(dllexport) void* __cdecl CreateInterface(const char* name, int* result) {
  const bool matches = name && std::strcmp(name, "ISERVERPLUGINCALLBACKS003") == 0 && !gPlugin.isLoaded();
  if (matches)
    log(
      "Loader requested ISERVERPLUGINCALLBACKS003; entering plugin "
      "initialization");
  if (result) *result = matches ? 0 : 1;
  return matches ? &gPlugin : nullptr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) gSelf = instance;
  return TRUE;
}
