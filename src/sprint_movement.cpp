#include "sprint_movement.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gm {
namespace {
Movement* gMovement = nullptr;
void __fastcall serverCrop(void* movement, void*) {
  if (gMovement) gMovement->crop(movement, false);
}
void __fastcall clientCrop(void* movement, void*) {
  if (gMovement) gMovement->crop(movement, true);
}

std::uint32_t hook(bool client) {
  return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(client ? &clientCrop : &serverCrop));
}

template <typename T>
T read(const void* object, std::uint32_t offset) {
  T result{};
  std::memcpy(&result, static_cast<const unsigned char*>(object) + offset, sizeof(result));
  return result;
}
}  // namespace

float crouchSprintScale(bool alreadyCropped, bool nowCropped, bool sprinting, float normal, float maximum) {
  if (alreadyCropped || !nowCropped || !sprinting || !std::isfinite(normal) || !std::isfinite(maximum) || normal <= 0 ||
      maximum <= 0)
    return 1;
  constexpr float stock = 1.0f / 3.0f;
  return std::max(0.0f, 1.0f - normal * (1.0f - stock) / maximum) / stock;
}

bool Movement::install(HMODULE server, HMODULE client, const MovementSpec& spec, std::string& error) {
  if (server_ || gMovement) {
    error = "Movement hook already owned";
    return false;
  }
  Image s(server), c(client);
  if (!s.matches(spec.serverCrop, error) || !c.matches(spec.clientCrop, error) ||
      !s.matches(spec.serverSprintProbe, error) || !c.matches(spec.clientSprintProbe, error) ||
      !s.contains(spec.normSpeed, 48) || s.read(spec.normSpeed + 12) != s.base() + spec.normName ||
      s.read(spec.normSpeed + 28) != s.base() + spec.normSpeed) {
    if (error.empty()) error = "Movement normal-speed identity changed";
    return false;
  }
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&serverCrop), &retained_)) {
    error = "Cannot retain movement-hook code";
    return false;
  }
  server_ = server;
  client_ = client;
  spec_ = spec;
  gMovement = this;
  enabled_ = true;
  if (!writeWord(s.at(spec.serverCrop.vtable + spec.serverCrop.slot * 4), hook(false)) ||
      !writeWord(c.at(spec.clientCrop.vtable + spec.clientCrop.slot * 4), hook(true))) {
    error = "Movement hook write failed; rolling back";
    restore();
    return false;
  }
  return true;
}

bool Movement::intact() const {
  if (!server_) return true;
  Image s(server_), c(client_);
  return enabled_ && s.valid() && c.valid() &&
         s.read(spec_.serverCrop.vtable + spec_.serverCrop.slot * 4) == hook(false) &&
         c.read(spec_.clientCrop.vtable + spec_.clientCrop.slot * 4) == hook(true) &&
         s.read(spec_.normSpeed + 12) == s.base() + spec_.normName &&
         s.read(spec_.normSpeed + 28) == s.base() + spec_.normSpeed;
}

bool Movement::restore() {
  enabled_ = false;
  if (!server_) return true;
  bool restored = true;
  for (bool client : {true, false}) {
    Image image(client ? client_ : server_);
    const auto& target = client ? spec_.clientCrop : spec_.serverCrop;
    const auto slot = target.vtable + target.slot * 4;
    if (!image.valid() || !image.contains(slot, 4)) {
      restored = false;
      continue;
    }
    const auto actual = image.read(slot);
    const auto original = static_cast<std::uint32_t>(image.base() + target.rva);
    if (actual == hook(client))
      restored = writeWord(image.at(slot), original) && restored;
    else if (actual != original)
      restored = false;
  }
  // A foreign detour may retain our callbacks; keep forwarding code resident on failure.
  if (!restored) return false;
  gMovement = nullptr;
  server_ = client_ = nullptr;
  HMODULE retained = retained_;
  retained_ = nullptr;
  if (retained) FreeLibrary(retained);
  return true;
}

void Movement::crop(void* movement, bool client) {
  auto* module = reinterpret_cast<unsigned char*>(client ? client_ : server_);
  const auto& target = client ? spec_.clientCrop : spec_.serverCrop;
  const bool before = (read<std::uint32_t>(movement, spec_.cropped) & 1) != 0;
  using Crop = void(__thiscall*)(void*);
  reinterpret_cast<Crop>(module + target.rva)(movement);
  if (!enabled_) return;
  const bool after = (read<std::uint32_t>(movement, spec_.cropped) & 1) != 0;
  auto* move = read<unsigned char*>(movement, spec_.move);
  if (!move) return;
  const bool sprint = move[spec_.sprint] != 0;
  const auto* normal = reinterpret_cast<unsigned char*>(server_) + spec_.normSpeed;
  const float scale =
    crouchSprintScale(before, after, sprint, read<float>(normal, 44), read<float>(move, spec_.maxSpeed));
  if (scale == 1) return;
  for (std::uint32_t axis = 0; axis < 3; ++axis) {
    const auto offset = spec_.inputs + axis * 4;
    const float value = read<float>(move, offset) * scale;
    std::memcpy(move + offset, &value, sizeof(value));
  }
}
}  // namespace gm
