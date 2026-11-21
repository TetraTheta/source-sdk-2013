#include <algorithm>
#include <cctype>
#include <sstream>

#include "player_tools_runtime.hpp"

namespace gm {
namespace {
constexpr const char* kCategories[] = {"ammo", "debris", "gibs", "powerups", "ragdolls", "small", "weapons"};
constexpr const char* kLabels[] = {"Ammo", "Debris", "Gibs", "Powerups", "Ragdolls", "Small objects", "Weapons"};
constexpr const char* kAmmo[] = {
  "item_ammo_pistol",   "item_ammo_pistol_large", "item_ammo_smg1",        "item_ammo_smg1_large",
  "item_ammo_ar2",      "item_ammo_ar2_large",    "item_ammo_357",         "item_ammo_357_large",
  "item_ammo_crossbow", "item_ammo_smg1_grenade", "item_ammo_ar2_altfire", "item_box_buckshot",
  "item_rpg_round",     "item_ml_grenade",        "item_flare_round",      "item_box_flare_rounds",
  "item_box_srounds",   "item_large_box_srounds", "item_box_mrounds",      "item_large_box_mrounds",
  "item_box_lrounds",   "item_large_box_lrounds", "item_ar2_grenade",      "item_box_sniper_rounds"};
}  // namespace

bool isPhysicsDebris(const std::string& className, std::uint32_t flags) {
  return className == "prop_physics" && (flags & kPhysicsPropIsGib) != 0;
}

bool PlayerTools::category(void* entity, const std::string& name) const {
  const auto cls = retail_.text(entity, "m_iClassname");
  if (name == "ammo") return std::find(std::begin(kAmmo), std::end(kAmmo), cls) != std::end(kAmmo);
  if (name == "debris") return isPhysicsDebris(cls, field<std::uint32_t>(entity, retail_.offset("m_spawnflags")));
  if (name == "gibs") return cls == "gib";
  if (name == "powerups")
    return cls == "item_battery" || cls == "item_healthkit" || cls == "item_healthvial" ||
           cls == "item_battery_custom" || cls == "item_healthkit_custom";
  if (name == "ragdolls") return cls == "prop_ragdoll";
  if (name == "small") {
    if (cls != "prop_physics") return false;
    const auto model = retail_.text(entity, "m_ModelName");
    for (const auto& entry : cleanup_)
      if (entry.first == "Clean/Model/model" && matchesModel(model, entry.second)) return true;
    return false;
  }
  if (name == "weapons") {
    if (!retail_.hasClass(entity, "CBaseCombatWeapon") || field<int>(entity, retail_.offset("m_iHammerID")) ||
        mapWeapons_.count(retail_.handle(entity)) ||
        retail_.resolve(field<std::uint32_t>(entity, retail_.offset("m_hOwnerEntity"))))
      return false;
    using Owner = void*(__thiscall*)(void*);
    return !retail_.function<Owner>(RetailFn::GetOwner)(entity);
  }
  return false;
}

void PlayerTools::captureMapWeapons() {
  mapWeaponsReady_ = false;
  mapWeapons_.clear();
  if (!enabled_) return;
  for (void* entity = retail_.next(nullptr); entity; entity = retail_.next(entity)) {
    if (retail_.hasClass(entity, "CBaseCombatWeapon")) mapWeapons_.insert(retail_.handle(entity));
  }
  mapWeaponsReady_ = true;
}

void PlayerTools::clean(void* player, const std::string& requested) {
  // Late activation conservatively protects weapons already present in a loaded save.
  if (!mapWeaponsReady_) captureMapWeapons();
  const bool all = requested == "all";
  const int clientRagdolls = (all || requested == "ragdolls") ? retail_.clearRagdolls() : 0;
  if (all || requested == "decals") retail_.clearDecals();
  std::ostringstream result;
  for (std::size_t i = 0; i < std::size(kCategories); ++i) {
    if (!all && requested != kCategories[i]) continue;
    int count = 0;
    for (void* entity = retail_.next(nullptr); entity;) {
      auto* next = retail_.next(entity);
      if (!retail_.marked(entity) && field<void*>(entity, retail_.offset("m_pPhysicsObject")) &&
          category(entity, kCategories[i])) {
        std::unordered_set<std::uint32_t> visited;
        if (!protectedEntity(entity, true, visited)) {
          retail_.remove(entity);
          ++count;
        }
      }
      entity = next;
    }
    if (count) result << count << ' ' << kLabels[i] << '\n';
  }
  if (clientRagdolls) result << clientRagdolls << " Client ragdolls\n";
  if (all || requested == "decals") result << "Decals cleared\n";
  const auto message = result.str();
  if (!message.empty()) {
    hint(player, "Clean Result:\n" + message);
    using Precache = void(__cdecl*)(const char*);
    retail_.function<Precache>(RetailFn::PrecacheSound)("ui/buttonclick.wav");
    sound(player, "ui/buttonclick.wav");
  }
}

void PlayerTools::command(bool cleanCommand, const CommandArgs& args) {
  if (!enabled_) return;
  auto* actor = player();
  if (!retail_.isPlayer(actor)) return;
  if (!cleanCommand) {
    if (args.count == 1)
      removeTarget(actor);
    else
      retail_.print(actor, 2, "Usage: sc_remove\n");
    return;
  }
  const char* argument = nullptr;
  if (args.count == 2) {
    const auto start = reinterpret_cast<std::uintptr_t>(args.tokens);
    const auto pointer = reinterpret_cast<std::uintptr_t>(args.argv[1]);
    if (pointer >= start && pointer < start + sizeof(args.tokens) &&
        std::memchr(args.argv[1], 0, start + sizeof(args.tokens) - pointer))
      argument = args.argv[1];
  }
  std::string name = argument ? argument : "";
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (name != "all" && name != "decals" &&
      std::find(std::begin(kCategories), std::end(kCategories), name) == std::end(kCategories)) {
    retail_.print(actor, 2, "Usage: sc_clean <all|ammo|debris|decals|gibs|powerups|ragdolls|small|weapons>\n");
    return;
  }
  clean(actor, name);
}
}  // namespace gm
