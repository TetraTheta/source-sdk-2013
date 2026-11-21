#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gm {

struct RetailSpec;

struct Target {
  const char* name;
  std::uint32_t rva;
  const char* pattern;
  std::uint32_t vtable;
  std::uint32_t slot;
  std::uint32_t classVtable = 0;
};

enum class PatchKind { Scalar, Pointer, Cone, ClearFlags };

struct PatchSpec {
  std::uint32_t rva;
  PatchKind kind;
  std::uint32_t before;
  std::uint32_t after;
};

struct CommandSpec {
  std::uint32_t object;
  std::uint32_t name;
  std::uint32_t callback;
};

struct CommandsSpec {
  CommandSpec god;
  CommandSpec noclip;
  std::uint32_t table;
  std::uint32_t dispatch;
  std::uint32_t getPlayer;
  std::uint32_t playerFlags;
  std::uint32_t playerMoveType;
};

struct MovementSpec {
  Target serverCrop;
  Target clientCrop;
  Target serverSprintProbe;
  Target clientSprintProbe;
  std::uint32_t normSpeed;
  std::uint32_t normName;
  std::uint32_t move;
  std::uint32_t cropped;
  std::uint32_t sprint;
  std::uint32_t inputs;
  std::uint32_t maxSpeed;
};

struct Profile {
  const char* name;
  const char* serverHash;
  const char* clientHash;
  const char* engineHash;
  const Target* targets;
  std::size_t targetCount;
  const PatchSpec* patches;
  std::size_t patchCount;
  const CommandsSpec* commands = nullptr;
  const MovementSpec* movement = nullptr;
  const RetailSpec* retail = nullptr;
};

// Module memory is owned by the game; every address must stay within its image.
class Image {
 public:
  explicit Image(HMODULE module);
  bool valid() const;
  bool contains(std::uint32_t rva, std::size_t length) const;
  bool executable(std::uint32_t rva, std::size_t length) const;
  std::uint32_t read(std::uint32_t rva) const;
  std::uint8_t* at(std::uint32_t rva) const;
  std::uintptr_t base() const;
  bool matches(const Target& target, std::string& error) const;

 private:
  std::uint8_t* data_ = nullptr;
  std::uint32_t size_ = 0;
  std::vector<IMAGE_SECTION_HEADER> sections_;
};

// Patches contain no pointers into this plugin, so unloading cannot leave callbacks behind.
class Patches {
 public:
  bool install(HMODULE module, const Profile& profile, std::string& error);
  bool intact() const;
  bool restore();
  bool active() const;

 private:
  struct Entry {
    std::uint32_t rva;
    std::uint32_t before;
    std::uint32_t after;
  };
  HMODULE module_ = nullptr;
  std::vector<Entry> entries_;
};

bool hashFile(const std::wstring& path, std::string& hash);
std::wstring modulePath(HMODULE module);
const Profile* findProfile(const std::string& serverHash);
bool writeWord(void* address, std::uint32_t value);

}  // namespace gm
