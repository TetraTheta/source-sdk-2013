#include <algorithm>
#include <cmath>

#include "player_tools_runtime.hpp"

namespace gm {
namespace {
Vector scaled(Vector v, float scale) { return {v.x * scale, v.y * scale, v.z * scale}; }
Vector added(Vector a, Vector b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr const char* kModes[] = {"Explosion", "Airboat Gun", "Combine Ball", "Crossbow Bolt", "Armed Grenade"};
}  // namespace

float PlayerTools::setting(const char* path, float fallback, float minimum, float maximum) const {
  return gm::setting(admin_, std::string("WeaponData/AdminAttack/") + path, fallback, minimum, maximum);
}
void PlayerTools::original(RetailFn function, void* object) const {
  using Method = void(__thiscall*)(void*);
  retail_.function<Method>(function)(object);
}
void PlayerTools::sound(void* entity, const char* name) const {
  using Emit = void(__thiscall*)(void*, const char*, float, float*);
  retail_.function<Emit>(RetailFn::EmitSound)(entity, name, 0, nullptr);
}
void PlayerTools::precacheAssets() {
  if (assetsReady_) return;
  assetsReady_ = true;
  using Precache = void(__cdecl*)(const char*, const char*);
  for (const char* name : {"env_explosion", "crossbow_bolt", "prop_combine_ball", "npc_grenade_frag"})
    retail_.function<Precache>(RetailFn::Precache)(name, nullptr);
  using Sound = void(__cdecl*)(const char*);
  for (const char* name :
       {"Weapon_SMG1.Single", "weapons/airboat/airboat_gun_lastshot1.wav", "weapons/airboat/airboat_gun_lastshot2.wav",
        "weapons/ar2/ar2_altfire.wav", "weapons/physcannon/energy_bounce1.wav", "Weapon_Crossbow.Single",
        "weapons/grenade/tick1.wav", "ui/buttonclick.wav"})
    retail_.function<Sound>(RetailFn::PrecacheSound)(name);
}
void PlayerTools::adminPrecache(void* weapon) {
  original(RetailFn::WeaponPrecache, weapon);
  if (enabled_) precacheAssets();
}
void PlayerTools::adminSpawn(void* weapon) {
  original(RetailFn::WeaponSpawn, weapon);
  if (!enabled_) return;
  retail_.write(weapon, retail_.offset("m_iClip2"), static_cast<std::uint32_t>(setting("InitialMode", 1, 1, 5)) - 1);
  static_cast<unsigned char*>(weapon)[retail_.offset("m_bFiresUnderwater")] = 1;
  static_cast<unsigned char*>(weapon)[retail_.offset("m_bAltFiresUnderwater")] = 1;
}
bool PlayerTools::slots(int reserve) const {
  using Count = int(__thiscall*)(void*);
  auto* engine = retail_.engine();
  if (retail_.method<Count>(engine, 17)(engine) + reserve >= 2048) return false;
  int count = 0;
  for (void* entity = retail_.next(nullptr); entity; entity = retail_.next(entity)) {
    if (++count + reserve >= 4096) return false;
  }
  return true;
}
Vector PlayerTools::position(void* player) const {
  using Position = Vector*(__thiscall*)(void*, Vector*);
  Vector value;
  retail_.method<Position>(player, 264)(player, &value);
  return value;
}
Vector PlayerTools::direction(void* player) const {
  using Aim = Vector*(__thiscall*)(void*, Vector*, float);
  Vector value;
  retail_.method<Aim>(player, 414)(player, &value, 1);
  return value;
}
Vector PlayerTools::projectileOrigin(void* player, const Vector& eye, const Vector& aim, float radius) const {
  Trace trace;
  retail_.trace(eye, added(eye, scaled(aim, 24)), 0x200400b, player, trace);
  if (trace.bytes[55]) return eye;
  const auto hit = field<Vector>(trace.bytes.data(), 12);
  const float distance = (hit.x - eye.x) * aim.x + (hit.y - eye.y) * aim.y + (hit.z - eye.z) * aim.z;
  return added(eye, scaled(aim, std::max(0.0f, distance - radius)));
}
void PlayerTools::bullets(void* weapon, void* player, const Vector& aim, bool airboat) {
  BulletInfo info{};
  using Initialize = void(__thiscall*)(void*);
  retail_.function<Initialize>(RetailFn::BulletInit)(&info);
  using AmmoDef = void*(__cdecl*)();
  using AmmoIndex = int(__thiscall*)(void*, const char*);
  info.ammo = retail_.function<AmmoIndex>(RetailFn::AmmoIndex)(retail_.function<AmmoDef>(RetailFn::AmmoDef)(),
                                                               airboat ? "AirboatGun" : "Pistol");
  if (info.ammo < 1) throw std::runtime_error("Admin ammunition definition missing");
  info.source = position(player);
  info.direction = aim;
  const float spread = setting(airboat ? "Airboat/Spread" : "Primary/Spread", 0.015f, 0, 1);
  info.spread = {spread, spread, 0};
  info.shots = static_cast<int>(setting(airboat ? "Airboat/Shots" : "Primary/Shots", airboat ? 7.0f : 25.0f, 1, 100));
  info.damage = setting(airboat ? "Airboat/Damage" : "Primary/Damage", airboat ? 50.0f : 1000.0f, 1, 100000);
  info.playerDamage = static_cast<int>(info.damage);
  info.force = setting(airboat ? "Airboat/ForceScale" : "Primary/ForceScale", 1, 0, 10000);
  info.distance = 56756;
  info.tracer = 1;
  info.flags = airboat ? 0 : 1;
  info.attacker = player;
  using Fire = void(__thiscall*)(void*, const BulletInfo&);
  const ScopedAdminShot scope(shot_, weapon, airboat);
  retail_.method<Fire>(player, 112)(player, info);
}
void PlayerTools::adminImpact(void* weapon, Trace& trace, int damageType) {
  if (enabled_ && shot_.weapon == weapon) return;
  using Impact = void(__thiscall*)(void*, Trace&, int);
  retail_.function<Impact>(RetailFn::WeaponImpact)(weapon, trace, damageType);
}
void PlayerTools::adminTracer(void* weapon, const Vector& start, const Trace& trace, int type) {
  if (!enabled_ || shot_.weapon != weapon || !shot_.airboat) {
    using MakeTracer = void(__thiscall*)(void*, const Vector&, const Trace&, int);
    retail_.function<MakeTracer>(RetailFn::WeaponTracer)(weapon, start, trace, type);
    return;
  }
  using Tracer = void(__cdecl*)(const Vector&, const Vector&, int, int, float, bool, const char*, int);
  const auto end = field<Vector>(trace.bytes.data(), 12);
  using Owner = void*(__thiscall*)(void*);
  using Lookup = int(__thiscall*)(void*, const char*);
  auto* player = retail_.function<Owner>(RetailFn::GetOwner)(weapon);
  int attachment = 0;
  if (retail_.isPlayer(player)) {
    auto* viewModel = retail_.resolve(field<std::uint32_t>(player, retail_.offset("m_hViewModel")));
    if (viewModel) attachment = retail_.function<Lookup>(RetailFn::LookupAttachment)(viewModel, "muzzle");
  }
  if (attachment <= 0) attachment = retail_.function<Lookup>(RetailFn::LookupAttachment)(weapon, "muzzle");
  // A weapon entity plus a valid attachment lets the client substitute its animated viewmodel.
  retail_.function<Tracer>(RetailFn::Tracer)(start, end, static_cast<int>(retail_.handle(weapon) & 0xfff),
                                             attachment > 0 ? attachment : -1, 0, false, "AirboatGunTracer", 0);
}
bool PlayerTools::explosion(void* player, const Vector& eye, const Vector& aim) {
  Trace trace;
  retail_.trace(eye, added(eye, scaled(aim, 56756)), 0x4600400b, player, trace);
  void* helper = nullptr;
  const auto owner = retail_.handle(player);
  const auto found = explosions_.find(owner);
  if (found != explosions_.end()) helper = retail_.resolve(found->second);
  constexpr const char* marker = "__gunmaster_admin_explosion";
  const auto belongs = [&](void* entity) {
    return entity && !retail_.marked(entity) && retail_.text(entity, "m_iClassname") == "env_explosion" &&
           retail_.text(entity, "m_iName") == marker &&
           retail_.resolve(field<std::uint32_t>(entity, retail_.offset("m_hOwnerEntity"))) == player;
  };
  if (!belongs(helper)) {
    helper = nullptr;
    // Owner EHANDLE and targetname survive save restoration; plugin cache
    // pointers do not.
    for (void* entity = retail_.next(nullptr); entity; entity = retail_.next(entity))
      if (belongs(entity)) {
        helper = entity;
        break;
      }
  }
  if (!helper) {
    if (!slots(1)) return false;
    using Create = void*(__cdecl*)(const char*, int, bool);
    helper = retail_.function<Create>(RetailFn::Create)("env_explosion", -1, true);
    if (!helper) return false;
    retail_.write(helper, retail_.offset("m_spawnflags"), kAdminExplosionFlags);
    retail_.key(helper, "targetname", marker);
    using Spawn = int(__cdecl*)(void*);
    if (retail_.function<Spawn>(RetailFn::Spawn)(helper) < 0) return false;
    using Owner = void(__thiscall*)(void*, void*);
    retail_.method<Owner>(helper, 17)(helper, player);
  }
  // Repair helpers restored from a 0.5 save as well as newly created ones.
  retail_.write(helper, retail_.offset("m_spawnflags"), kAdminExplosionFlags);
  explosions_[owner] = retail_.handle(helper);
  retail_.key(helper, "iMagnitude", std::to_string(static_cast<int>(setting("Explosion/Magnitude", 175, 1, 10000))));
  const auto point = field<Vector>(trace.bytes.data(), 12);
  using Origin = void(__cdecl*)(void*, const Vector&, bool);
  retail_.function<Origin>(RetailFn::Origin)(helper, point, false);
  return retail_.input(helper, "Explode", player);
}
bool PlayerTools::projectile(void* player, const Vector& eye, const Vector& aim, int mode) {
  if (!slots(mode == 4 ? 3 : 2)) return false;
  if (mode == 2) {
    const float radius = setting("CombineBall/Radius", 2, 1, 64);
    const auto origin = projectileOrigin(player, eye, aim, radius);
    const auto velocity = scaled(aim, setting("CombineBall/Speed", 3000, 1, 10000));
    using Ball = void*(__cdecl*)(const Vector&, const Vector&, float, float, float, void*);
    return retail_.function<Ball>(RetailFn::Ball)(origin, velocity, radius, 150,
                                                  setting("CombineBall/Lifetime", 5, 0.1f, 120), player) != nullptr;
  }
  Vector angles;
  using Angles = void(__cdecl*)(const Vector&, Vector&);
  retail_.function<Angles>(RetailFn::VectorAngles)(aim, angles);
  if (mode == 3) {
    using Create = void*(__cdecl*)(const char*, int, bool);
    auto* bolt = retail_.function<Create>(RetailFn::Create)("crossbow_bolt", -1, true);
    if (!bolt) return false;
    using Origin = void(__cdecl*)(void*, const Vector&, bool);
    retail_.function<Origin>(RetailFn::Origin)(bolt, projectileOrigin(player, eye, aim, 1), false);
    using SetVector = void(__thiscall*)(void*, const Vector&);
    retail_.function<SetVector>(RetailFn::Angles)(bolt, angles);
    using Spawn = void(__thiscall*)(void*);
    retail_.method<Spawn>(bolt, 22)(bolt);
    using Owner = void(__thiscall*)(void*, void*);
    retail_.method<Owner>(bolt, 17)(bolt, player);
    retail_.function<SetVector>(RetailFn::Velocity)(bolt, scaled(aim, setting("Crossbow/Speed", 3750, 1, 10000)));
    return true;
  }
  const auto origin = projectileOrigin(player, eye, aim, 4);
  const auto velocity = scaled(aim, setting("Grenade/Speed", 1000, 1, 10000));
  const Vector spin{};
  using Grenade = void*(__cdecl*)(const Vector&, const Vector&, const Vector&, const Vector&, void*, float, bool);
  return retail_.function<Grenade>(RetailFn::Grenade)(origin, angles, velocity, spin, player,
                                                      setting("Grenade/Fuse", 3, 0.1f, 120), false) != nullptr;
}
void PlayerTools::finish(void* weapon, void* player, float delay, const char* recoilKey) {
  const float next = retail_.time() + delay;
  retail_.writeFloat(weapon, retail_.offset("m_flNextPrimaryAttack"), next);
  retail_.writeFloat(weapon, retail_.offset("m_flNextSecondaryAttack"), next);
  const float recoil = setting(recoilKey, 0.1f, 0, 20);
  const Vector punch{-recoil, std::uniform_real_distribution<float>(-recoil, recoil)(random_), -recoil};
  using Punch = void(__thiscall*)(void*, const Vector&);
  retail_.function<Punch>(RetailFn::Punch)(player, punch);
  using Flash = void(__thiscall*)(void*);
  retail_.method<Flash>(player, 306)(player);
  using Animate = void(__thiscall*)(void*, int);
  retail_.method<Animate>(player, 365)(player, 5);
  using WeaponAnimation = bool(__thiscall*)(void*, int);
  retail_.method<WeaponAnimation>(weapon, 235)(weapon, 180);
}
void PlayerTools::adminFrame(void* weapon) {
  if (!enabled_) {
    original(RetailFn::WeaponFrame, weapon);
    return;
  }
  using Owner = void*(__thiscall*)(void*);
  auto* player = retail_.function<Owner>(RetailFn::GetOwner)(weapon);
  if (!retail_.isPlayer(player)) return;  // NPC weapon handling is deliberately deferred.
  using Alive = bool(__thiscall*)(void*);
  if (!retail_.method<Alive>(player, 65)(player)) return;
  precacheAssets();
  using Lowered = bool(__thiscall*)(void*);
  if (retail_.method<Lowered>(weapon, 366)(weapon)) {
    using Idle = void(__thiscall*)(void*);
    retail_.method<Idle>(weapon, 261)(weapon);
    return;
  }
  const auto modeField = retail_.offset("m_iClip2");
  int mode = field<int>(weapon, modeField);
  if (mode < 0 || mode > 4) {
    mode = static_cast<int>(setting("InitialMode", 1, 1, 5)) - 1;
    retail_.write(weapon, modeField, static_cast<std::uint32_t>(mode));
  }
  const float now = retail_.time();
  if ((field<std::uint32_t>(player, retail_.offset("m_afButtonPressed")) & 0x2000) &&
      field<float>(weapon, retail_.offset("m_flNextPrimaryAttack")) <= now) {
    mode = (mode + 1) % 5;
    retail_.write(weapon, modeField, static_cast<std::uint32_t>(mode));
    hint(player, kModes[mode], weapon);
    sound(player, "ui/buttonclick.wav");
  }
  const auto buttons = field<std::uint32_t>(player, retail_.offset("m_nButtons"));
  const bool secondary = (buttons & 0x800) && field<float>(weapon, retail_.offset("m_flNextSecondaryAttack")) <= now;
  if (!secondary && (!(buttons & 1) || field<float>(weapon, retail_.offset("m_flNextPrimaryAttack")) > now)) {
    using Idle = void(__thiscall*)(void*);
    retail_.method<Idle>(weapon, 261)(weapon);
    return;
  }
  const auto eye = position(player), aim = direction(player);
  if (!secondary) {
    bullets(weapon, player, aim, false);
    sound(weapon, "Weapon_SMG1.Single");
    finish(weapon, player, setting("Primary/Delay", 0.05f, 0.05f, 10), "Primary/Recoil");
    return;
  }
  bool fired = true;
  float delay = 0.25f;
  const char* cue = "weapons/grenade/tick1.wav";
  if (mode == 0) {
    fired = explosion(player, eye, aim);
    delay = setting("Explosion/Delay", 0.05f, 0.05f, 10);
    cue = nullptr;
  } else if (mode == 1) {
    bullets(weapon, player, aim, true);
    delay = setting("Airboat/Delay", 0.05f, 0.05f, 10);
    cue =
      (GetTickCount() & 1) ? "weapons/airboat/airboat_gun_lastshot1.wav" : "weapons/airboat/airboat_gun_lastshot2.wav";
  } else {
    fired = projectile(player, eye, aim, mode);
    if (mode == 2) {
      delay = setting("CombineBall/Delay", 0.5f, 0.05f, 10);
      cue = "weapons/ar2/ar2_altfire.wav";
    } else if (mode == 3) {
      delay = setting("Crossbow/Delay", 0.25f, 0.05f, 10);
      cue = "Weapon_Crossbow.Single";
    } else
      delay = setting("Grenade/Delay", 0.25f, 0.05f, 10);
  }
  if (fired) {
    if (cue) sound(weapon, cue);
    if (mode == 2) sound(weapon, "weapons/physcannon/energy_bounce1.wav");
    finish(weapon, player, delay, "Secondary/Recoil");
  } else {
    retail_.writeFloat(weapon, retail_.offset("m_flNextPrimaryAttack"), now + delay);
    retail_.writeFloat(weapon, retail_.offset("m_flNextSecondaryAttack"), now + delay);
  }
}
}  // namespace gm
