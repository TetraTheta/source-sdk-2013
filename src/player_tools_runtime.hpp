#pragma once

#include <random>
#include <unordered_map>
#include <unordered_set>

#include "cheat_commands.hpp"
#include "keyvalues_settings.hpp"
#include "retail_entity_api.hpp"

namespace gm {
constexpr std::uint32_t kPhysicsPropIsGib = 0x400000;
bool isPhysicsDebris(const std::string& className, std::uint32_t flags);
constexpr std::uint32_t kAdminExplosionFlags = 2 | 4 | 8 | 16 | 32 | 64 | 256 | 512;
struct AdminShotContext {
  void* weapon = nullptr;
  bool airboat = false;
};
class ScopedAdminShot {
 public:
  ScopedAdminShot(AdminShotContext& context, void* weapon, bool airboat) : context_(context), previous_(context) {
    context_ = {weapon, airboat};
  }
  ~ScopedAdminShot() { context_ = previous_; }
  ScopedAdminShot(const ScopedAdminShot&) = delete;
  ScopedAdminShot& operator=(const ScopedAdminShot&) = delete;

 private:
  AdminShotContext& context_;
  AdminShotContext previous_;
};

class PlayerTools {
 public:
  bool install(HMODULE self, HMODULE server, HMODULE client, HMODULE engine, HMODULE vstdlib, const Profile& profile,
               std::string& error);
  bool intact() const;
  bool restore();
  void captureMapWeapons();
  void adminFrame(void* weapon);
  void adminSpawn(void* weapon);
  void adminPrecache(void* weapon);
  void adminImpact(void* weapon, Trace& trace, int damageType);
  void adminTracer(void* weapon, const Vector& start, const Trace& trace, int type);
  void updateHints();
  void command(bool clean, const CommandArgs& args);
  void fail(const std::string& reason);

 private:
  float setting(const char* path, float fallback, float minimum, float maximum) const;
  void original(RetailFn function, void* object) const;
  void precacheAssets();
  void sound(void* entity, const char* name) const;
  void hint(void* player, const std::string& message, void* weapon = nullptr);
  bool slots(int reserve) const;
  Vector position(void* player) const;
  Vector direction(void* player) const;
  Vector projectileOrigin(void* player, const Vector& eye, const Vector& aim, float radius) const;
  void bullets(void* weapon, void* player, const Vector& aim, bool airboat);
  bool explosion(void* player, const Vector& eye, const Vector& aim);
  bool projectile(void* player, const Vector& eye, const Vector& aim, int mode);
  void finish(void* weapon, void* player, float delay, const char* recoil);
  void* player() const;
  bool protectedEntity(void* entity, bool mapProtection, std::unordered_set<std::uint32_t>& visited) const;
  bool category(void* entity, const std::string& name) const;
  void removeTarget(void* player);
  void clean(void* player, const std::string& category);
  struct Slot {
    std::uint32_t rva, original, replacement;
  };
  struct OwnedCommand {
    std::array<std::uint32_t, 9> object{};
    const char* name;
  };
  Retail retail_;
  HMODULE retained_ = nullptr;
  HMODULE hostRetained_ = nullptr;
  const CommandsSpec* commands_ = nullptr;
  std::vector<Slot> hooks_;
  std::array<OwnedCommand, 2> owned_{{{{}, "sc_remove"}, {{}, "sc_clean"}}};
  KeyValues admin_, cleanup_;
  std::unordered_set<std::uint32_t> mapWeapons_;
  std::unordered_map<std::uint32_t, std::uint32_t> explosions_;
  struct Hint {
    std::uint32_t entity = 0xffffffff, weapon = 0xffffffff;
    std::string message;
    float showAt = 0, hideAt = 0;
    bool pending = false;
  };
  std::unordered_map<std::uint32_t, Hint> hints_;
  AdminShotContext shot_;
  bool enabled_ = false;
  bool assetsReady_ = false;
  bool mapWeaponsReady_ = false;
  bool failed_ = false;
  std::minstd_rand random_{1};
};
}  // namespace gm
