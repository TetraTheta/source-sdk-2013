#include "retail_entity_api.hpp"

#include <algorithm>
#include <limits>

#include "cheat_commands.hpp"

namespace gm {
namespace {
std::string stringAt(const char* pointer, std::size_t maximum = 1024) {
  if (!pointer) return {};
  MEMORY_BASIC_INFORMATION region{};
  if (!VirtualQuery(pointer, &region, sizeof(region)) || region.State != MEM_COMMIT ||
      (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
    throw std::runtime_error("Unreadable retail string");
  const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
  const auto size = std::min<std::size_t>(maximum, end - reinterpret_cast<std::uintptr_t>(pointer));
  const auto* zero = static_cast<const char*>(std::memchr(pointer, 0, size));
  if (!zero) throw std::runtime_error("Unterminated retail string");
  return std::string(pointer, zero);
}

template <typename Visitor>
void visitMap(HMODULE module, const void* first, Visitor visitor) {
  Image image(module);
  auto* map = static_cast<const unsigned char*>(first);
  for (unsigned int depth = 0; map && depth < 64; ++depth) {
    const auto rva = reinterpret_cast<std::uintptr_t>(map) - image.base();
    if (rva > std::numeric_limits<std::uint32_t>::max() || !image.contains(static_cast<std::uint32_t>(rva), 16))
      throw std::runtime_error("Invalid retail datamap");
    const auto* fields = field<unsigned char*>(map, 0);
    const int count = field<int>(map, 4);
    if (count < 0 || count > 4096) throw std::runtime_error("Invalid retail field count");
    const auto start = reinterpret_cast<std::uintptr_t>(fields) - image.base();
    if (count && (start > std::numeric_limits<std::uint32_t>::max() ||
                  !image.contains(static_cast<std::uint32_t>(start), static_cast<std::size_t>(count) * 52)))
      throw std::runtime_error("Invalid retail field array");
    if (!visitor(map, fields, count)) return;
    map = field<unsigned char*>(map, 12);
  }
  if (map) throw std::runtime_error("Cyclic/deep retail datamap");
}
}  // namespace

std::uint32_t Retail::offset(const char* name) const {
  for (std::size_t i = 0; i < spec_->fieldCount; ++i)
    if (std::strcmp(spec_->fields[i].name, name) == 0) return spec_->fields[i].offset;
  throw std::runtime_error(std::string("Missing reviewed field: ") + name);
}
std::uint32_t Retail::value(const char* name) const {
  for (std::size_t i = 0; i < spec_->valueCount; ++i)
    if (std::strcmp(spec_->values[i].name, name) == 0) return spec_->values[i].rva;
  throw std::runtime_error(std::string("Missing reviewed global: ") + name);
}
const RetailMap& Retail::map(const char* name) const {
  for (std::size_t i = 0; i < spec_->mapCount; ++i)
    if (std::strcmp(spec_->maps[i].name, name) == 0) return spec_->maps[i];
  throw std::runtime_error(std::string("Missing reviewed class: ") + name);
}

bool validateRetailFields(HMODULE module, const RetailSpec& spec, std::string& error) {
  const auto* server = reinterpret_cast<unsigned char*>(module);
  for (std::size_t i = 0; i < spec.fieldCount; ++i) {
    const auto& expected = spec.fields[i];
    bool found = false;
    for (std::size_t j = 0; j < spec.mapCount; ++j) {
      visitMap(module, server + spec.maps[j].map, [&](const unsigned char*, const unsigned char* fields, int count) {
        for (int n = 0; n < count; ++n) {
          const auto* descriptor = fields + n * 52;
          if (stringAt(field<const char*>(descriptor, 4)) != expected.name) continue;
          if (field<int>(descriptor, 0) != expected.type || field<std::uint32_t>(descriptor, 8) != expected.offset)
            throw std::runtime_error(std::string("Retail field layout changed: ") + expected.name);
          found = true;
        }
        return true;
      });
    }
    if (!found) {
      error = std::string("Missing registered retail field: ") + expected.name;
      return false;
    }
  }
  return true;
}

bool Retail::initialize(HMODULE server, HMODULE client, HMODULE engineModule, HMODULE vstdlib, const RetailSpec& spec,
                        std::string& error) {
  server_ = reinterpret_cast<unsigned char*>(server);
  client_ = reinterpret_cast<unsigned char*>(client);
  engine_ = reinterpret_cast<unsigned char*>(engineModule);
  spec_ = &spec;
  Image image(server), clientImage(client), engineImage(engineModule), cvarImage(vstdlib);
  for (std::size_t i = 0; i < spec.functionCount; ++i)
    if (!image.matches(spec.functions[i].target, error)) return false;
  for (std::size_t i = 0; i < spec.engineTargetCount; ++i)
    if (!engineImage.matches(spec.engineTargets[i], error)) return false;
  for (std::size_t i = 0; i < spec.cvarTargetCount; ++i)
    if (!cvarImage.matches(spec.cvarTargets[i], error)) return false;
  for (std::size_t i = 0; i < spec.valueCount; ++i) {
    if (!image.contains(spec.values[i].rva, 4)) {
      error = "Retail global outside server image";
      return false;
    }
  }
  const auto list = reinterpret_cast<std::uintptr_t>(field<void*>(server_, value("EntityListPtr")));
  if (list < image.base() || list - image.base() > std::numeric_limits<std::uint32_t>::max() ||
      !image.contains(static_cast<std::uint32_t>(list - image.base()), 0x10014)) {
    error = "Retail entity-list storage outside server image";
    return false;
  }
  const auto& cleanup = spec.clientCleanup;
  if (!clientImage.contains(cleanup.listInstance, cleanup.listOffset + 4) ||
      clientImage.read(cleanup.listInstance) != clientImage.base() + cleanup.listPrimary ||
      clientImage.read(cleanup.listInstance + cleanup.listOffset) != clientImage.base() + cleanup.listSecondary ||
      !clientImage.contains(cleanup.ragdollTable, 4) || cleanup.releaseField > 0xffff) {
    error = "Client ragdoll/entity-list identity changed";
    return false;
  }
  for (std::size_t i = 0; i < spec.mapCount; ++i) {
    const auto& root = spec.maps[i];
    if (!image.contains(root.table, 48) || !image.contains(root.map, 16) ||
        stringAt(field<const char*>(server_ + root.map, 8)) != root.name) {
      error = "Retail class/datamap identity changed";
      return false;
    }
    const auto getter = image.read(root.table + 44) - static_cast<std::uint32_t>(image.base());
    if (!image.executable(getter, 6) || image.at(getter)[0] != 0xb8 || image.at(getter)[5] != 0xc3 ||
        image.read(getter + 1) != image.base() + root.map) {
      error = "Retail datamap getter changed";
      return false;
    }
  }
  using Factory = void*(__cdecl*)(const char*, int*);
  auto factory = reinterpret_cast<Factory>(GetProcAddress(vstdlib, "CreateInterface"));
  cvar_ = factory ? factory("VEngineCvar004", nullptr) : nullptr;
  if (!cvar_ || field<void*>(server_, value("CvarPtr")) != cvar_ ||
      field<void*>(cvar_, 0) != cvarImage.at(spec.cvarTable)) {
    error = "ICvar004 instance mismatch";
    return false;
  }
  for (const auto& pair : {std::pair<std::size_t, const char*>{6, "Register"}, {7, "Unregister"}, {10, "Find"}}) {
    const auto* target = std::find_if(spec.cvarTargets, spec.cvarTargets + spec.cvarTargetCount,
                                      [&](const Target& t) { return std::strcmp(t.name, pair.second) == 0; });
    if (target == spec.cvarTargets + spec.cvarTargetCount ||
        cvarImage.read(spec.cvarTable + static_cast<std::uint32_t>(pair.first * 4)) != cvarImage.base() + target->rva) {
      error = "ICvar004 virtual slots changed";
      return false;
    }
  }
  auto engineFactory = reinterpret_cast<Factory>(GetProcAddress(engineModule, "CreateInterface"));
  modelRender_ = engineFactory ? engineFactory("VEngineModel016", nullptr) : nullptr;
  if (!modelRender_ || field<void*>(modelRender_, 0) != engineImage.at(spec.modelTable)) {
    error = "Model-render interface identity changed";
    return false;
  }
  const auto* clear = std::find_if(spec.engineTargets, spec.engineTargets + spec.engineTargetCount,
                                   [](const Target& t) { return std::strcmp(t.name, "ClearModelDecals") == 0; });
  if (clear == spec.engineTargets + spec.engineTargetCount ||
      engineImage.read(spec.modelTable + 40) != engineImage.base() + clear->rva) {
    error = "Model decal virtual slot changed";
    return false;
  }
  return validateRetailFields(server, spec, error);
}

void* Retail::resolve(std::uint32_t id) const {
  if (id == 0xffffffff) return nullptr;
  const auto* list = field<unsigned char*>(server_, value("EntityListPtr"));
  const auto* entry = list + (id & 0xfff) * 16;
  return field<std::uint32_t>(entry, 8) == id >> 12 ? field<void*>(entry, 4) : nullptr;
}
std::uint32_t Retail::handle(void* entity) const {
  if (!entity) return 0xffffffff;
  using Get = const std::uint32_t*(__thiscall*)(void*);
  return *method<Get>(entity, 2)(entity);
}
void* Retail::next(void* entity) const {
  using Next = void*(__thiscall*)(void*, void*);
  return function<Next>(RetailFn::Next)(field<void*>(server_, value("EntityListPtr")), entity);
}
std::string Retail::text(void* entity, const char* name) const {
  return stringAt(field<const char*>(entity, offset(name)));
}
bool Retail::isPlayer(void* entity) const {
  using IsPlayer = bool(__thiscall*)(void*);
  return entity && method<IsPlayer>(entity, 80)(entity);
}
bool Retail::marked(void* entity) const {
  return !entity || (field<std::uint32_t>(entity, offset("m_iEFlags")) & 1) != 0;
}
bool Retail::hasClass(void* entity, const char* name) const {
  using GetMap = const void*(__thiscall*)(void*);
  bool found = false;
  visitMap(reinterpret_cast<HMODULE>(server_), method<GetMap>(entity, 11)(entity),
           [&](const unsigned char* m, const unsigned char*, int) {
             found = stringAt(field<const char*>(m, 8)) == name;
             return !found;
           });
  return found;
}
bool Retail::hasOutputs(void* entity) const {
  using GetMap = const void*(__thiscall*)(void*);
  bool connected = false;
  visitMap(reinterpret_cast<HMODULE>(server_), method<GetMap>(entity, 11)(entity),
           [&](const unsigned char*, const unsigned char* fields, int count) {
             for (int i = 0; i < count; ++i) {
               const auto* descriptor = fields + i * 52;
               if (field<int>(descriptor, 0) != 11 || !(field<std::uint16_t>(descriptor, 18) & 0x10)) continue;
               const auto position = field<std::uint32_t>(descriptor, 8);
               if (position > 0xffff) throw std::runtime_error("Invalid retail output offset");
               // Retail variant_t is 20 bytes; the output's action-list pointer follows it.
               if (field<void*>(entity, position + 20)) {
                 connected = true;
                 return false;
               }
             }
             return true;
           });
  return connected;
}
void Retail::write(void* entity, std::uint32_t position, std::uint32_t word) const {
  if (field<std::uint32_t>(entity, position) == word) return;
  if (field<unsigned char>(entity, 0x54))
    static_cast<unsigned char*>(entity)[0x58] |= 1;
  else if (auto* edict = field<void*>(entity, 24)) {
    using Changed = void(__thiscall*)(void*, unsigned short);
    function<Changed>(RetailFn::NetworkField)(edict, static_cast<unsigned short>(position));
  }
  std::memcpy(static_cast<unsigned char*>(entity) + position, &word, 4);
}
void Retail::writeFloat(void* entity, std::uint32_t position, float number) const {
  std::uint32_t word = 0;
  std::memcpy(&word, &number, 4);
  write(entity, position, word);
}
float Retail::time() const { return field<float>(field<void*>(server_, value("GlobalsPtr")), 12); }
void Retail::print(void* player, int destination, const std::string& message) const {
  using Print = void(__cdecl*)(void*, int, const char*, const char*, const char*, const char*, const char*);
  function<Print>(RetailFn::Print)(player, destination, message.c_str(), nullptr, nullptr, nullptr, nullptr);
}
bool Retail::input(void* entity, const char* name, void* player, Variant data) const {
  using Input = bool(__thiscall*)(void*, const char*, void*, void*, Variant, int);
  return method<Input>(entity, 36)(entity, name, player, player, data, 0);
}
bool Retail::key(void* entity, const char* name, const std::string& data) const {
  using Key = bool(__thiscall*)(void*, const char*, const char*);
  return method<Key>(entity, 31)(entity, name, data.c_str());
}
void Retail::remove(void* entity) const {
  if (marked(entity)) return;
  using Remove = void(__cdecl*)(void*);
  function<Remove>(RetailFn::Remove)(entity);
}
void Retail::trace(const Vector& from, const Vector& to, std::uint32_t mask, void* ignore, Trace& result) const {
  using TraceLine = void(__cdecl*)(const Vector&, const Vector&, std::uint32_t, void*, int, Trace*);
  function<TraceLine>(RetailFn::Trace)(from, to, mask, ignore, 0, &result);
}
void* Retail::engine() const { return field<void*>(server_, value("EnginePtr")); }

int Retail::clearRagdolls() const {
  int count = 0;
  const auto& spec = spec_->clientCleanup;
  const auto table = reinterpret_cast<std::uintptr_t>(client_ + spec.ragdollTable);
  // Client-only slots are above the retail 2048 networked-edict range.
  for (std::uint32_t index = 2048; index < 4096; ++index) {
    auto* entity = field<unsigned char*>(client_ + spec.listInstance, index * 16 + 4);
    if (entity && field<std::uintptr_t>(entity, 0) == table && !entity[spec.releaseField]) {
      entity[spec.releaseField] = 1;
      ++count;
    }
  }
  return count;
}
void Retail::clearDecals() const {
  const auto* clear = std::find_if(spec_->engineTargets, spec_->engineTargets + spec_->engineTargetCount,
                                   [](const Target& t) { return std::strcmp(t.name, "ClearDecals") == 0; });
  if (clear == spec_->engineTargets + spec_->engineTargetCount) throw std::runtime_error("Missing decal callback");
  CommandArgs args{};
  args.count = 1;
  std::memcpy(args.tokens, "r_cleardecals", 13);
  std::memcpy(args.text, "r_cleardecals", 13);
  args.argv[0] = args.tokens;
  using ClearWorld = void(__cdecl*)(const CommandArgs&);
  reinterpret_cast<ClearWorld>(engine_ + clear->rva)(args);
  using ClearModels = void(__thiscall*)(void*);
  method<ClearModels>(modelRender_, 10)(modelRender_);
}
}  // namespace gm
