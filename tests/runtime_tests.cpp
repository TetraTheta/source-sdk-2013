#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "../src/cheat_commands.hpp"
#include "../src/profiles.generated.hpp"
#include "../src/runtime_patches.hpp"
#include "../src/sprint_movement.hpp"

namespace {

void check(bool condition, const char* reason) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", reason);
    std::exit(1);
  }
}

class Fixture {
 public:
  Fixture() {
    memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x4000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(memory != nullptr, "allocate test image");
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(memory);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(memory + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.SizeOfImage = 0x4000;
    auto* sections = IMAGE_FIRST_SECTION(nt);
    sections[0].VirtualAddress = 0x1000;
    sections[0].Misc.VirtualSize = 0x1000;
    sections[0].Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    sections[1].VirtualAddress = 0x2000;
    sections[1].Misc.VirtualSize = 0x1000;
    sections[1].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    std::memcpy(memory + 0x1100, "\x55\x8b\xec\xc3", 4);
    set(0x2010, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory + 0x1100)));
    set(0x1200, 0x41000000);
    set(0x1210, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory + 0x2100)));
    set(0x2100, 0x40000000);
    set(0x2110, 0x3f800000);
  }
  ~Fixture() { VirtualFree(memory, 0, MEM_RELEASE); }
  void set(std::uint32_t rva, std::uint32_t value) { std::memcpy(memory + rva, &value, 4); }
  HMODULE module() const { return reinterpret_cast<HMODULE>(memory); }
  unsigned char* memory = nullptr;
};

void testRetailFields() {
  Fixture fixture;
  const auto address = [&](std::uint32_t rva) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(fixture.memory + rva));
  };
  fixture.set(0x2200, address(0x2300));
  fixture.set(0x2204, 1);
  fixture.set(0x220c, 0);
  fixture.set(0x2300, 5);
  fixture.set(0x2304, address(0x2400));
  fixture.set(0x2308, 0x4b0);
  std::memcpy(fixture.memory + 0x2400, "m_iClip2", 9);
  const gm::RetailMap map{"Fixture", 0, 0x2200};
  const gm::RetailField descriptor{"m_iClip2", 5, 0x4b0};
  gm::RetailSpec spec{};
  spec.maps = &map;
  spec.mapCount = 1;
  spec.fields = &descriptor;
  spec.fieldCount = 1;
  std::string error;
  check(gm::validateRetailFields(fixture.module(), spec, error), "registered retail datamap validated");
  fixture.set(0x2308, 0x4ac);
  bool rejected = false;
  try {
    gm::validateRetailFields(fixture.module(), spec, error);
  } catch (const std::exception&) {
    rejected = true;
  }
  check(rejected, "changed registered offset rejected");
  fixture.set(0x2308, 0x4b0);
  fixture.set(0x220c, address(0x2200));
  rejected = false;
  try {
    gm::validateRetailFields(fixture.module(), spec, error);
  } catch (const std::exception&) {
    rejected = true;
  }
  check(rejected, "cyclic datamap rejected");
}

void testPatches() {
  Fixture fixture;
  gm::Target target{"fixture", 0x1100, "55 8B ?? C3", 0x2010, 0};
  gm::PatchSpec specs[] = {
    {0x1200, gm::PatchKind::Scalar, 0x41000000, 0x40800000},
    {0x2000, gm::PatchKind::Cone, 0x3d32aae3, 0x3c0f0824},
    {0x1210, gm::PatchKind::Pointer, 0x2100, 0x2110},
  };
  gm::Profile profile{"fixture", "", "", "", &target, 1, specs, std::size(specs)};
  gm::Patches patches;
  gm::Image image(fixture.module());
  std::string error;
  const bool installed = patches.install(fixture.module(), profile, error);
  check(installed, error.c_str());
  check(patches.intact(), "all installed words intact");
  check(image.read(0x1200) == 0x40800000, "scalar patch");
  check(image.read(0x1210) == image.base() + 0x2110, "ASLR-relative pointer patch");
  check(patches.restore(), "restore");
  check(image.read(0x1200) == 0x41000000, "scalar restored");
  check(image.read(0x2000) == 0x3d32aae3, "uninitialized cone restores stock, not zero after initialization");
  check(image.read(0x1210) == image.base() + 0x2100, "pointer restored");

  std::memcpy(fixture.memory + 0x1300, fixture.memory + 0x1100, 4);
  check(!patches.install(fixture.module(), profile, error), "ambiguous signature rejected");
  check(image.read(0x1200) == 0x41000000, "ambiguous signature wrote nothing");
  std::memset(fixture.memory + 0x1300, 0, 4);
  fixture.memory[0x1100] = 0x90;
  check(!patches.install(fixture.module(), profile, error), "missing signature rejected");
  fixture.memory[0x1100] = 0x55;
  fixture.set(0x2010, 0);
  check(!patches.install(fixture.module(), profile, error), "wrong virtual slot rejected");
  fixture.set(0x2010, static_cast<std::uint32_t>(image.base() + 0x1100));
  fixture.set(0x1210, 123);
  check(!patches.install(fixture.module(), profile, error), "wrong operand rejected");
  check(image.read(0x1200) == 0x41000000, "late validation failure wrote nothing");
  fixture.set(0x1210, static_cast<std::uint32_t>(image.base() + 0x2100));
  check(patches.install(fixture.module(), profile, error), "reinstall");
  fixture.set(0x1200, 42);
  check(!patches.intact(), "foreign write detected");
  check(!patches.restore(), "foreign write reported while other owned patches are restored");
  check(image.read(0x1200) == 42, "foreign write not overwritten");
  target.slot = 0xffffffff;
  check(!patches.install(fixture.module(), profile, error), "overflowing slot rejected");
  check(!gm::Image(nullptr).valid(), "null module rejected");
  check(!gm::findProfile(std::string(64, '0')), "unknown hash rejected");
}

void testHash() {
  FILE* file = nullptr;
  check(fopen_s(&file, "hash-fixture.txt", "wb") == 0 && file, "hash fixture file");
  std::fputs("abc", file);
  std::fclose(file);
  std::string hash;
  check(gm::hashFile(L"hash-fixture.txt", hash), "hash file read");
  check(hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known vector");
  check(!gm::hashFile(L"does-not-exist.dll", hash), "missing file rejected");
  std::remove("hash-fixture.txt");
}

void testConVarFlags() {
  Fixture fixture;
  gm::Image image(fixture.module());
  fixture.set(0x2004, 0x400c);
  fixture.set(0x2014, static_cast<std::uint32_t>(image.base() + 0x2100));
  const gm::Target target{"fixture", 0x1100, "55 8B EC C3", 0x2010, 0};
  const gm::PatchSpec specs[] = {
    {0x2014, gm::PatchKind::Pointer, 0x2100, 0x2100},
    {0x2004, gm::PatchKind::ClearFlags, 0x4000, 0},
  };
  const gm::Profile profile{"fixture", "", "", "", &target, 1, specs, std::size(specs)};
  gm::Patches patches;
  std::string error;
  check(patches.install(fixture.module(), profile, error), "clear cheat flag after identity check");
  check(image.read(0x2004) == 0xc, "unrelated registered ConVar flags preserved");
  check(patches.restore() && image.read(0x2004) == 0x400c, "ConVar flags restored");
  fixture.set(0x2014, 0);
  check(!patches.install(fixture.module(), profile, error), "wrong ConVar identity rejected");
  check(image.read(0x2004) == 0x400c, "identity failure made no flag writes");
  fixture.set(0x2014, static_cast<std::uint32_t>(image.base() + 0x2100));
  fixture.set(0x2004, 0xc);
  check(!patches.install(fixture.module(), profile, error), "unexpected pre-cleared flags rejected");
}

void testConstructorClassAnchors() {
  Fixture fixture;
  gm::Image image(fixture.module());
  constexpr unsigned char code[] = {0xc7, 0x06, 0, 0, 0, 0, 0x55, 0xc3};
  std::memcpy(fixture.memory + 0x1100, code, sizeof(code));
  std::memcpy(fixture.memory + 0x1300, code, sizeof(code));
  fixture.set(0x1102, static_cast<std::uint32_t>(image.base() + 0x2010));
  fixture.set(0x1302, static_cast<std::uint32_t>(image.base() + 0x2020));
  const gm::Target target{"constructor", 0x1100, "C7 06 ?? ?? ?? ?? 55 C3", 0, 0, 0x2010};
  std::string error;
  check(image.matches(target, error), "constructor pattern must bind the correct retail class");
  fixture.set(0x1302, static_cast<std::uint32_t>(image.base() + 0x2010));
  check(!image.matches(target, error), "two constructors for the same class still rejected");
}

void testCommands() {
  using Action = gm::ToggleAction;
  for (bool on : {false, true}) {
    check(gm::toggleAction(1, nullptr, on) == Action::Toggle, "bare command keeps toggle behavior");
    check(gm::toggleAction(2, "on", on) == (on ? Action::Keep : Action::Toggle), "on is idempotent");
    check(gm::toggleAction(2, "OFF", on) == (on ? Action::Toggle : Action::Keep),
          "off is case insensitive and idempotent");
    check(gm::toggleAction(3, "on", on) == Action::Usage, "extra arguments rejected");
    check(gm::toggleAction(2, "1", on) == Action::Usage, "unknown argument rejected");
    check(gm::toggleAction(2, nullptr, on) == Action::Usage, "invalid argument rejected");
  }
  Fixture fixture;
  gm::Image image(fixture.module());
  const auto base = static_cast<std::uint32_t>(image.base());
  gm::CommandsSpec spec{{0x2200, 0x2300, 0x1100}, {0x2240, 0x2310, 0x1100}, 0x2400, 0x1100, 0x1100, 0x104, 0x13a};
  for (const auto& command : {spec.god, spec.noclip}) {
    fixture.set(command.object, base + spec.table);
    fixture.set(command.object + 12, base + command.name);
    fixture.set(command.object + 20, 0x4004);
    fixture.set(command.object + 24, base + command.callback);
  }
  fixture.set(spec.table + 48, base + spec.dispatch);
  gm::Commands commands;
  std::string error;
  fixture.set(spec.noclip.object + 24, 0);
  check(!commands.install(fixture.module(), spec, error), "command identity mismatch rejects hook");
  check(image.read(spec.table + 48) == base + spec.dispatch, "identity rejection writes nothing");
  fixture.set(spec.noclip.object + 24, base + spec.noclip.callback);
  check(commands.install(fixture.module(), spec, error), error.c_str());
  check(commands.intact(), "console hook installed and monitored");
  fixture.set(spec.god.object + 12, 0);
  check(!commands.intact(), "registered command drift detected");
  check(commands.restore(), "console hook removed despite command identity drift");
  check(image.read(spec.table + 48) == base + spec.dispatch, "original dispatcher restored");
}

int gDispatchCount = 0;
void* gDispatchedCommand = nullptr;
void __fastcall syntheticDispatch(void* command, void*, const gm::CommandArgs&) {
  ++gDispatchCount;
  gDispatchedCommand = command;
}

void testCommandDispatch() {
  Fixture fixture;
  gm::Image image(fixture.module());
  const auto base = static_cast<std::uint32_t>(image.base());
  const gm::CommandsSpec spec{{0x2200, 0x2300, 0x1100}, {0x2240, 0x2310, 0x1100}, 0x2400, 0x1300, 0x1400, 0x104, 0x13a};
  for (const auto& command : {spec.god, spec.noclip}) {
    fixture.set(command.object, base + spec.table);
    fixture.set(command.object + 12, base + command.name);
    fixture.set(command.object + 20, 0x4004);
    fixture.set(command.object + 24, base + command.callback);
  }
  fixture.set(spec.table + 48, base + spec.dispatch);
  fixture.memory[0x1300] = 0xe9;
  fixture.set(0x1301, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&syntheticDispatch)) - base - 0x1305);
  fixture.memory[0x1400] = 0xb8;
  fixture.set(0x1401, base + 0x3000);
  fixture.memory[0x1405] = 0xc3;
  DWORD previous = 0;
  check(VirtualProtect(fixture.memory + 0x1000, 0x1000, PAGE_EXECUTE_READ, &previous) != FALSE,
        "synthetic command helpers executable");
  FlushInstructionCache(GetCurrentProcess(), fixture.memory + 0x1000, 0x1000);
  gm::Commands hooks;
  std::string error;
  check(hooks.install(fixture.module(), spec, error), error.c_str());
  using Dispatch = void(__thiscall*)(void*, const gm::CommandArgs&);
  auto dispatch = reinterpret_cast<Dispatch>(image.read(spec.table + 48));
  gm::CommandArgs args{};
  args.count = 2;
  args.argv[1] = args.tokens;
  std::memcpy(args.tokens, "on", 3);
  gDispatchCount = 0;
  dispatch(fixture.memory + spec.god.object, args);
  check(gDispatchCount == 1 && gDispatchedCommand == fixture.memory + spec.god.object,
        "god on forwards to original dispatcher when disabled");
  fixture.set(0x3000 + spec.playerFlags, 0x4000);
  dispatch(fixture.memory + spec.god.object, args);
  check(gDispatchCount == 1, "god on does not toggle an already enabled player");
  std::memcpy(args.tokens, "off", 4);
  dispatch(fixture.memory + spec.god.object, args);
  check(gDispatchCount == 2, "god off invokes original callback when enabled");
  fixture.memory[0x3000 + spec.playerMoveType] = 2;
  dispatch(fixture.memory + spec.noclip.object, args);
  check(gDispatchCount == 2, "noclip off leaves ordinary movement unchanged");
  fixture.memory[0x3000 + spec.playerMoveType] = 8;
  dispatch(fixture.memory + spec.noclip.object, args);
  check(gDispatchCount == 3, "noclip off exits existing noclip through Valve dispatcher");
  args.count = 1;
  dispatch(fixture.memory + spec.noclip.object, args);
  check(gDispatchCount == 4, "bare command still toggles");
  args.count = 3;
  dispatch(fixture.memory + spec.god.object, args);
  check(gDispatchCount == 4, "invalid argument count never invokes native toggle");
  dispatch(fixture.memory + 0x2280, args);
  check(gDispatchCount == 5 && gDispatchedCommand == fixture.memory + 0x2280,
        "unrelated commands retain native dispatch and arguments");
  check(hooks.restore(), "dispatcher ABI test restores hook");
}

void __fastcall syntheticCrop(void* object, void*) {
  auto* movement = static_cast<unsigned char*>(object);
  std::uint32_t flags = 0;
  unsigned char* move = nullptr;
  std::memcpy(&flags, movement + 0x688, 4);
  std::memcpy(&move, movement + 8, sizeof(move));
  if ((flags & 1) || !move[0xa5]) return;
  for (unsigned int axis = 0; axis < 3; ++axis) {
    float value = 0;
    std::memcpy(&value, move + 0x2c + axis * 4, 4);
    value /= 3.0f;
    std::memcpy(move + 0x2c + axis * 4, &value, 4);
  }
  flags |= 1;
  std::memcpy(movement + 0x688, &flags, 4);
}

void testMovement() {
  Fixture server, client;
  const auto base = static_cast<std::uint32_t>(gm::Image(server.module()).base());
  gm::MovementSpec spec{{"server crop", 0x1300, "E9 ?? ?? ?? ??", 0x2500, 47},
                        {"client crop", 0x1300, "E9 ?? ?? ?? ??", 0x2500, 47},
                        {"server probe", 0x1100, "55 8B EC C3", 0, 0},
                        {"client probe", 0x1100, "55 8B EC C3", 0, 0},
                        0x2800,
                        0x2900,
                        8,
                        0x688,
                        0xa4,
                        0x2c,
                        0x3c};
  // An authored jump to a test helper exercises the x86 callback ABI without Valve code.
  for (auto* fixture : {&server, &client}) {
    const auto address = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(fixture->memory));
    fixture->memory[0x1300] = 0xe9;
    fixture->set(0x1301,
                 static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&syntheticCrop)) - address - 0x1305);
    fixture->set(0x2500 + 47 * 4, address + 0x1300);
    DWORD previous = 0;
    check(VirtualProtect(fixture->memory + 0x1000, 0x1000, PAGE_EXECUTE_READ, &previous) != FALSE,
          "synthetic executable helper");
    FlushInstructionCache(GetCurrentProcess(), fixture->memory + 0x1300, 5);
  }
  server.set(spec.normSpeed + 12, base + spec.normName);
  server.set(spec.normSpeed + 28, base + spec.normSpeed);
  const float normal = 190;
  std::memcpy(server.memory + spec.normSpeed + 44, &normal, 4);
  gm::Movement hooks;
  std::string error;
  client.set(spec.clientCrop.vtable + spec.clientCrop.slot * 4, 0);
  check(!hooks.install(server.module(), client.module(), spec, error), "bad client slot prevents both movement hooks");
  check(gm::Image(server.module()).read(0x2500 + 47 * 4) == base + 0x1300, "client rejection made no server write");
  client.set(0x2500 + 47 * 4, static_cast<std::uint32_t>(gm::Image(client.module()).base() + 0x1300));
  check(hooks.install(server.module(), client.module(), spec, error), error.c_str());
  check(hooks.intact(), "paired movement hooks monitored");
  for (auto* fixture : {&server, &client}) {
    std::array<unsigned char, 0x700> object{};
    std::array<unsigned char, 0xb0> move{};
    auto* movePointer = move.data();
    std::memcpy(object.data() + 8, &movePointer, sizeof(movePointer));
    const float maximum = 320;
    std::memcpy(move.data() + 0x3c, &maximum, 4);
    move[0xa4] = move[0xa5] = 1;
    std::memcpy(move.data() + 0x2c, &maximum, 4);
    using Crop = void(__thiscall*)(void*);
    auto crop = reinterpret_cast<Crop>(gm::Image(fixture->module()).read(0x2500 + 47 * 4));
    crop(object.data());
    float actual = 0;
    std::memcpy(&actual, move.data() + 0x2c, 4);
    check(std::abs(actual - 193.33333f) < 0.001f, "server/client ABI and movement adjustment agree");
    crop(object.data());
    float twice = 0;
    std::memcpy(&twice, move.data() + 0x2c, 4);
    check(actual == twice, "repeated callback does not crop twice");
  }
  const auto slot = spec.clientCrop.vtable + spec.clientCrop.slot * 4;
  const auto owned = gm::Image(client.module()).read(slot);
  client.set(slot, 0x1234);
  check(!hooks.intact(), "foreign movement slot detected");
  check(!hooks.restore(), "foreign hook reports incomplete rollback and retains callbacks");
  check(gm::Image(client.module()).read(slot) == 0x1234, "foreign movement hook not overwritten");
  client.set(slot, owned);
  check(hooks.restore(), "retained hooks can finish rollback once ownership restored");
  check(gm::Image(server.module()).read(slot) == base + 0x1300, "original server crop restored");
}

void testPlugin(const char* path) {
  HMODULE plugin = LoadLibraryA(path);
  check(plugin != nullptr, "plugin loads without SDK libraries");
  using Factory = void*(__cdecl*)(const char*, int*);
  auto factory = reinterpret_cast<Factory>(GetProcAddress(plugin, "CreateInterface"));
  check(factory != nullptr, "undecorated CreateInterface export");
  int result = -1;
  check(!factory("UnknownInterface", &result) && result == 1, "unknown interface rejected");
  void* instance = factory("ISERVERPLUGINCALLBACKS003", &result);
  check(instance && result == 0, "VSP003 ABI exported");
  auto** table = *reinterpret_cast<void***>(instance);
  using Load = bool(__thiscall*)(void*, void*, void*);
  using Unload = void(__thiscall*)(void*);
  using Description = const char*(__thiscall*)(void*);
  check(std::strstr(reinterpret_cast<Description>(table[4])(instance), "GunMaster") != nullptr, "ABI description slot");
  check(!reinterpret_cast<Load>(table[0])(instance, nullptr, nullptr), "missing game context rejected safely");
  reinterpret_cast<Unload>(table[1])(instance);
  FreeLibrary(plugin);
}

void testOfflineImage(const char* path) {
  // Map bytes and relocate pointers without loading imports or running Valve code.
  std::wstring wide(path, path + std::strlen(path));
  std::string hash;
  check(gm::hashFile(wide, hash), "offline image hash");
  const gm::Profile* profile = gm::findProfile(hash);
  const bool serverImage = profile != nullptr;
  int auxiliary = 0;
  if (!profile) {
    for (const auto& candidate : gm::kProfiles) {
      if (hash == candidate.clientHash) {
        profile = &candidate;
        break;
      }
      if (hash == candidate.engineHash) {
        profile = &candidate;
        auxiliary = 1;
        break;
      }
      if (candidate.retail && hash == candidate.retail->vstdlibHash) {
        profile = &candidate;
        auxiliary = 2;
        break;
      }
    }
  }
  check(profile != nullptr, "offline image must have an approved SHA-256");
  std::ifstream input(path, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  check(bytes.size() >= sizeof(IMAGE_DOS_HEADER), "offline DOS header");
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
  check(dos->e_lfanew > 0 && static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) < bytes.size(),
        "offline NT header");
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
  auto* memory = static_cast<unsigned char*>(
    VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  check(memory != nullptr, "allocate offline image");
  check(nt->OptionalHeader.SizeOfHeaders <= bytes.size(), "offline headers bounded");
  std::memcpy(memory, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
  const auto* sections = IMAGE_FIRST_SECTION(nt);
  for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const auto& section = sections[i];
    check(static_cast<std::size_t>(section.PointerToRawData) + section.SizeOfRawData <= bytes.size(),
          "offline section file bounds");
    check(section.VirtualAddress <= nt->OptionalHeader.SizeOfImage &&
            section.SizeOfRawData <= nt->OptionalHeader.SizeOfImage - section.VirtualAddress,
          "offline section image bounds");
    std::memcpy(memory + section.VirtualAddress, bytes.data() + section.PointerToRawData, section.SizeOfRawData);
  }
  const auto delta =
    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory)) - nt->OptionalHeader.ImageBase;
  const auto reloc = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  check(reloc.VirtualAddress <= nt->OptionalHeader.SizeOfImage &&
          reloc.Size <= nt->OptionalHeader.SizeOfImage - reloc.VirtualAddress,
        "relocation directory bounds");
  std::size_t offset = 0;
  while (offset < reloc.Size) {
    const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(memory + reloc.VirtualAddress + offset);
    check(block->SizeOfBlock >= sizeof(*block) && block->SizeOfBlock <= reloc.Size - offset, "relocation block bounds");
    const auto* entries = reinterpret_cast<const WORD*>(block + 1);
    for (std::size_t i = 0; i < (block->SizeOfBlock - sizeof(*block)) / sizeof(WORD); ++i) {
      const auto type = entries[i] >> 12;
      check(type == IMAGE_REL_BASED_ABSOLUTE || type == IMAGE_REL_BASED_HIGHLOW, "x86 relocation type");
      if (type == IMAGE_REL_BASED_HIGHLOW) {
        const auto rva = block->VirtualAddress + (entries[i] & 0xfff);
        check(rva <= nt->OptionalHeader.SizeOfImage - 4, "relocation target bounds");
        std::uint32_t value = 0;
        std::memcpy(&value, memory + rva, 4);
        value += delta;
        std::memcpy(memory + rva, &value, 4);
      }
    }
    offset += block->SizeOfBlock;
  }
  gm::Patches patches;
  std::string error;
  gm::Image image(reinterpret_cast<HMODULE>(memory));
  if (auxiliary) {
    const auto* targets = auxiliary == 1 ? profile->retail->engineTargets : profile->retail->cvarTargets;
    const auto count = auxiliary == 1 ? profile->retail->engineTargetCount : profile->retail->cvarTargetCount;
    for (std::size_t i = 0; i < count; ++i) check(image.matches(targets[i], error), error.c_str());
    VirtualFree(memory, 0, MEM_RELEASE);
    std::puts("PASS: auxiliary retail signatures; no Valve code executed.");
    return;
  }
  check(profile->movement != nullptr, "reviewed movement metadata present");
  const auto& crop = serverImage ? profile->movement->serverCrop : profile->movement->clientCrop;
  const auto& probe = serverImage ? profile->movement->serverSprintProbe : profile->movement->clientSprintProbe;
  check(image.matches(crop, error), error.c_str());
  check(image.matches(probe, error), error.c_str());
  if (!serverImage) {
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("PASS: %s client crop/probe signatures and virtual slot; no Valve code executed.\n", profile->name);
    return;
  }
  for (std::size_t i = 0; i < profile->retail->functionCount; ++i)
    check(image.matches(profile->retail->functions[i].target, error), error.c_str());
  // A raw image has not run ConVar constructors/registration. Seed only the
  // reviewed name/parent probes and registered flag bits in this private copy.
  for (std::size_t i = 0; i < profile->patchCount; ++i) {
    const auto& spec = profile->patches[i];
    std::uint32_t value = 0;
    if (spec.kind == gm::PatchKind::ClearFlags)
      value = spec.before | 4;
    else if (spec.kind == gm::PatchKind::Pointer && spec.before == spec.after)
      value = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory)) + spec.before;
    else
      continue;
    std::memcpy(memory + spec.rva, &value, 4);
  }
  const bool installed = patches.install(reinterpret_cast<HMODULE>(memory), *profile, error);
  check(installed, error.c_str());
  check(patches.intact(), "offline installed patches intact");
  check(patches.restore(), "offline rollback");
  check(!patches.active(), "offline all patches removed");
  VirtualFree(memory, 0, MEM_RELEASE);
  std::printf(
    "PASS: %s profile, %zu patch targets, %zu retail APIs and %zu memory patches/rollback; no Valve code executed.\n",
    profile->name, profile->targetCount, profile->retail->functionCount, profile->patchCount);
}

void testConstructorInitializers() {
  unsigned int tested = 0;
  for (const auto& profile : gm::kProfiles) {
    for (std::size_t i = 0; i < profile.targetCount; ++i) {
      const auto& target = profile.targets[i];
      if (!target.classVtable) continue;
      std::array<unsigned char, 256> patched{};
      const std::string pattern(target.pattern);
      std::size_t at = 0;
      for (std::size_t pos = 0; pos < pattern.size(); pos += 3) {
        patched[at++] = pattern[pos] == '?'
                          ? 0
                          : static_cast<unsigned char>(std::strtoul(pattern.substr(pos, 2).c_str(), nullptr, 16));
      }
      for (std::size_t j = 0; j < profile.patchCount; ++j) {
        const auto& spec = profile.patches[j];
        if (spec.kind == gm::PatchKind::Scalar && spec.rva >= target.rva &&
            spec.rva + 4 <= target.rva + patched.size()) {
          std::memcpy(patched.data() + spec.rva - target.rva, &spec.after, 4);
        }
      }
      constexpr unsigned char primary[] = {0xc6, 0x86, 0xb4, 0x04, 0x00, 0x00, 0x01};
      const auto* initializer =
        std::search(patched.data(), patched.data() + patched.size(), std::begin(primary), std::end(primary));
      check(initializer != patched.data() + patched.size(), "constructor sets primary underwater flag");
      const bool both = std::strcmp(target.name, "357.Factory") != 0;
      const std::size_t length = both ? 14 : 7;
      if (both) {
        constexpr unsigned char alternate[] = {0xc6, 0x86, 0xb5, 0x04, 0x00, 0x00, 0x01};
        check(std::memcmp(initializer + 7, alternate, 7) == 0, "constructor also sets secondary flag");
      }
      auto* code =
        static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
      check(code != nullptr, "allocate initializer test");
      std::memcpy(code, "\x56\x8b\xf1", 3);
      std::memcpy(code + 3, initializer, length);
      std::memcpy(code + 3 + length, "\x5e\xc3", 2);
      FlushInstructionCache(GetCurrentProcess(), code, length + 5);
      using Initialize = void(__thiscall*)(void*);
      std::array<unsigned char, 0x600> weapon{};
      constexpr std::array<std::uint16_t, 4> states{0x0000, 0x0001, 0x0100, 0x0101};
      for (const auto previous : states) {
        std::memcpy(weapon.data() + 0x4b4, &previous, 2);
        reinterpret_cast<Initialize>(code)(weapon.data());
        check(weapon[0x4b4] == 1, "new weapon fires primary underwater");
        check(weapon[0x4b5] == (both ? 1 : (previous >> 8)), ".357 alternate unchanged; others enabled");
      }
      VirtualFree(code, 0, MEM_RELEASE);
      ++tested;
    }
  }
  check(tested == 12, "all four constructor initializers in all three profiles executed");
}
}  // namespace

int main(int argc, char** argv) {
  static_assert(sizeof(void*) == 4, "Tests and plugin require x86");
  if (argc == 3 && std::strcmp(argv[1], "--image") == 0) {
    testOfflineImage(argv[2]);
    return 0;
  }
  testRetailFields();
  testPatches();
  testConstructorInitializers();
  testHash();
  testConVarFlags();
  testConstructorClassAnchors();
  testCommands();
  testCommandDispatch();
  testMovement();
  if (argc == 2) testPlugin(argv[1]);
  std::puts("PASS: memory patches, rollback, signatures, slots, hashes, and plugin ABI. HL2 was not launched.");
  return 0;
}
