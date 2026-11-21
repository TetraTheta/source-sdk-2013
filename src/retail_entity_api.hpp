#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>

#include "runtime_patches.hpp"

namespace gm {

enum class RetailFn {
  Create,
  Spawn,
  Remove,
  Next,
  Trace,
  Origin,
  Angles,
  Velocity,
  Precache,
  AmmoDef,
  AmmoIndex,
  Ball,
  Grenade,
  VectorAngles,
  Punch,
  NetworkField,
  Print,
  BulletInit,
  GetOwner,
  CommandCtor,
  DefaultAccessor,
  WeaponSpawn,
  WeaponPrecache,
  WeaponFrame,
  EntityInput,
  EntityKeyValue,
  EntityGetKeyValue,
  EmitSound,
  PrecacheSound,
  GetOrigin,
  PlayerByIndex,
  WeaponTracer,
  WeaponImpact,
  Tracer,
  FireBullets,
  LookupAttachment
};
struct RetailFunction {
  RetailFn kind;
  Target target;
};
struct RetailValue {
  const char* name;
  std::uint32_t rva;
};
struct RetailField {
  const char* name;
  int type;
  std::uint32_t offset;
};
struct RetailMap {
  const char* name;
  std::uint32_t table;
  std::uint32_t map;
};
struct ClientCleanupSpec {
  std::uint32_t listPrimary, listSecondary, listOffset, listInstance, ragdollTable, releaseField;
};
struct RetailSpec {
  const char* vstdlibHash;
  const RetailFunction* functions;
  std::size_t functionCount;
  const Target* engineTargets;
  std::size_t engineTargetCount;
  const Target* cvarTargets;
  std::size_t cvarTargetCount;
  const RetailValue* values;
  std::size_t valueCount;
  const RetailField* fields;
  std::size_t fieldCount;
  const RetailMap* maps;
  std::size_t mapCount;
  std::uint32_t cvarTable;
  std::uint32_t modelTable;
  ClientCleanupSpec clientCleanup;
};

struct Vector {
  float x = 0, y = 0, z = 0;
};
struct Variant {
  std::array<std::uint32_t, 4> value{};
  int type = 0;
};
static_assert(sizeof(Variant) == 20, "Retail AcceptInput passes five words by value");
struct BulletInfo {
  int shots;
  Vector source, direction, spread;
  float distance;
  int ammo, tracer;
  float damage;
  int playerDamage, flags;
  float force;
  void* attacker;
  void* ignore;
  bool primary;
  unsigned char padding[3];
};
static_assert(sizeof(BulletInfo) == 80 && offsetof(BulletInfo, ammo) == 44);
struct Trace {
  std::array<unsigned char, 84> bytes{};
};

template <typename T>
T field(const void* entity, std::uint32_t offset) {
  T result{};
  std::memcpy(&result, static_cast<const unsigned char*>(entity) + offset, sizeof(result));
  return result;
}

bool validateRetailFields(HMODULE module, const RetailSpec& spec, std::string& error);

class Retail {
 public:
  bool initialize(HMODULE server, HMODULE client, HMODULE engine, HMODULE vstdlib, const RetailSpec& spec,
                  std::string& error);
  template <typename T>
  T function(RetailFn kind) const {
    for (std::size_t i = 0; i < spec_->functionCount; ++i) {
      if (spec_->functions[i].kind == kind) return reinterpret_cast<T>(server_ + spec_->functions[i].target.rva);
    }
    throw std::runtime_error("Missing reviewed retail function");
  }
  template <typename T>
  T method(void* object, std::size_t slot) const {
    auto* table = field<void**>(object, 0);
    return reinterpret_cast<T>(table[slot]);
  }
  std::uint32_t offset(const char* name) const;
  std::uint32_t value(const char* name) const;
  const RetailMap& map(const char* name) const;
  void* resolve(std::uint32_t handle) const;
  std::uint32_t handle(void* entity) const;
  void* next(void* entity) const;
  std::string text(void* entity, const char* name) const;
  bool isPlayer(void* entity) const;
  bool marked(void* entity) const;
  bool hasClass(void* entity, const char* name) const;
  bool hasOutputs(void* entity) const;
  void write(void* entity, std::uint32_t offset, std::uint32_t word) const;
  void writeFloat(void* entity, std::uint32_t offset, float value) const;
  float time() const;
  void print(void* player, int destination, const std::string& message) const;
  bool input(void* entity, const char* name, void* player, Variant value = {}) const;
  bool key(void* entity, const char* name, const std::string& value) const;
  void remove(void* entity) const;
  void trace(const Vector& from, const Vector& to, std::uint32_t mask, void* ignore, Trace& result) const;
  int clearRagdolls() const;
  void clearDecals() const;
  void* cvar() const { return cvar_; }
  void* engine() const;
  unsigned char* server() const { return server_; }
  const RetailSpec& spec() const { return *spec_; }

 private:
  unsigned char* server_ = nullptr;
  unsigned char* client_ = nullptr;
  unsigned char* engine_ = nullptr;
  const RetailSpec* spec_ = nullptr;
  void* cvar_ = nullptr;
  void* modelRender_ = nullptr;
};

}  // namespace gm
