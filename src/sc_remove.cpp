#include "player_tools_runtime.hpp"

namespace gm {
void* PlayerTools::player() const {
  using Player = void*(__cdecl*)();
  auto* actor = reinterpret_cast<Player>(retail_.server() + commands_->getPlayer)();
  if (actor) return actor;
  auto* engine = retail_.engine();
  using Dedicated = bool(__thiscall*)(void*);
  const auto* globals = field<void*>(retail_.server(), retail_.value("GlobalsPtr"));
  if (retail_.method<Dedicated>(engine, 2)(engine) || field<int>(globals, 20) != 1) return nullptr;
  using LocalPlayer = void*(__cdecl*)(int);
  return retail_.function<LocalPlayer>(RetailFn::PlayerByIndex)(1);
}

bool PlayerTools::protectedEntity(void* entity, bool mapProtection, std::unordered_set<std::uint32_t>& visited) const {
  std::vector<void*> pending{entity};
  while (!pending.empty()) {
    auto* current = pending.back();
    pending.pop_back();
    if (!current || visited.size() >= 4096 || !visited.insert(retail_.handle(current)).second) return true;
    std::unordered_set<std::uint32_t> parents;
    for (auto* ancestor = current; ancestor;) {
      if (parents.size() >= 64 || !parents.insert(retail_.handle(ancestor)).second) return true;
      if (retail_.text(ancestor, "m_iClassname") == "worldspawn" || retail_.isPlayer(ancestor) ||
          retail_.hasClass(ancestor, "CBaseViewModel"))
        return true;
      auto* owner = retail_.resolve(field<std::uint32_t>(ancestor, retail_.offset("m_hOwnerEntity")));
      if (retail_.isPlayer(owner)) return true;
      if (retail_.hasClass(ancestor, "CBaseCombatWeapon")) {
        using Owner = void*(__thiscall*)(void*);
        if (retail_.isPlayer(retail_.function<Owner>(RetailFn::GetOwner)(ancestor))) return true;
      }
      if (mapProtection && (!retail_.text(ancestor, "m_iName").empty() || retail_.hasOutputs(ancestor))) return true;
      ancestor = retail_.resolve(field<std::uint32_t>(ancestor, retail_.offset("m_hMoveParent")));
    }
    std::unordered_set<std::uint32_t> peers;
    auto* child = retail_.resolve(field<std::uint32_t>(current, retail_.offset("m_hMoveChild")));
    while (child) {
      if (peers.size() >= 4096 || !peers.insert(retail_.handle(child)).second) return true;
      pending.push_back(child);
      child = retail_.resolve(field<std::uint32_t>(child, retail_.offset("m_hMovePeer")));
    }
  }
  return false;
}

void PlayerTools::removeTarget(void* player) {
  const auto eye = position(player), aim = direction(player);
  const Vector end{eye.x + aim.x * 56756, eye.y + aim.y * 56756, eye.z + aim.z * 56756};
  Trace trace;
  retail_.trace(eye, end, 0x600400f, player, trace);
  auto* entity = field<void*>(trace.bytes.data(), 76);
  if (!entity || retail_.marked(entity)) return;
  std::unordered_set<std::uint32_t> visited;
  if (protectedEntity(entity, false, visited)) {
    retail_.print(player, 2, "[sc_remove] Protected entity.\n");
    return;
  }
  const auto cls = retail_.text(entity, "m_iClassname");
  if (cls == "func_door" || cls == "func_door_rotating" || cls == "prop_door_rotating") {
    const auto name = retail_.text(entity, "m_iName");
    if (!name.empty()) {
      for (void* portal = retail_.next(nullptr); portal; portal = retail_.next(portal)) {
        if (!retail_.marked(portal) && retail_.text(portal, "m_iClassname") == "func_areaportal") {
          using GetKey = bool(__thiscall*)(void*, const char*, char*, int);
          std::array<char, 1024> target{};
          retail_.method<GetKey>(portal, 32)(portal, "target", target.data(), static_cast<int>(target.size()));
          if (_stricmp(target.data(), name.c_str()) == 0) retail_.input(portal, "Open", player);
        }
      }
    }
  }
  retail_.remove(entity);
  retail_.print(player, 2, "[sc_remove] Removed " + cls + ".\n");
}

}  // namespace gm
