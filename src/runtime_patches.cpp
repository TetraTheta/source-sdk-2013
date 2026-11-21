#include "runtime_patches.hpp"

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "profiles.generated.hpp"

namespace gm {

Image::Image(HMODULE module) {
  if (!module) return;
  auto* data = reinterpret_cast<std::uint8_t*>(module);
  MEMORY_BASIC_INFORMATION region{};
  if (!VirtualQuery(data, &region, sizeof(region)) || region.State != MEM_COMMIT ||
      (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || region.RegionSize < sizeof(IMAGE_DOS_HEADER))
    return;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
      static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > region.RegionSize)
    return;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(data + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.SizeOfImage < region.RegionSize ||
      !nt->FileHeader.NumberOfSections)
    return;
  const auto* sections = IMAGE_FIRST_SECTION(nt);
  const auto headerEnd = reinterpret_cast<const std::uint8_t*>(sections + nt->FileHeader.NumberOfSections);
  if (headerEnd > data + region.RegionSize) return;
  data_ = data;
  size_ = nt->OptionalHeader.SizeOfImage;
  for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    if (!contains(sections[i].VirtualAddress, sections[i].Misc.VirtualSize)) {
      data_ = nullptr;
      size_ = 0;
      sections_.clear();
      return;
    }
    sections_.push_back(sections[i]);
  }
}

bool Image::valid() const { return data_ != nullptr; }
bool Image::contains(std::uint32_t rva, std::size_t length) const {
  return valid() && rva < size_ && length <= size_ - rva;
}
bool Image::executable(std::uint32_t rva, std::size_t length) const {
  for (const auto& section : sections_) {
    if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && rva >= section.VirtualAddress &&
        rva - section.VirtualAddress < section.Misc.VirtualSize &&
        length <= section.Misc.VirtualSize - (rva - section.VirtualAddress))
      return true;
  }
  return false;
}
std::uint32_t Image::read(std::uint32_t rva) const {
  std::uint32_t value = 0;
  if (contains(rva, sizeof(value))) std::memcpy(&value, at(rva), sizeof(value));
  return value;
}
std::uint8_t* Image::at(std::uint32_t rva) const { return data_ + rva; }
std::uintptr_t Image::base() const { return reinterpret_cast<std::uintptr_t>(data_); }

bool Image::matches(const Target& target, std::string& error) const {
  std::vector<int> pattern;
  const std::string text(target.pattern);
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == ' ') {
      ++i;
      continue;
    }
    if (i + 1 >= text.size()) {
      error = "Malformed signature";
      return false;
    }
    const auto token = text.substr(i, 2);
    if (token == "??")
      pattern.push_back(-1);
    else {
      char* end = nullptr;
      const long value = std::strtol(token.c_str(), &end, 16);
      if (end != token.c_str() + 2 || value < 0 || value > 255) {
        error = "Malformed signature";
        return false;
      }
      pattern.push_back(static_cast<int>(value));
    }
    i += 2;
    if (i < text.size() && text[i] != ' ') {
      error = "Malformed signature";
      return false;
    }
  }
  if (pattern.empty() || !executable(target.rva, pattern.size()) ||
      (target.classVtable && !contains(target.classVtable, 4)) ||
      (target.vtable && (target.slot > (std::numeric_limits<std::uint32_t>::max() - target.vtable) / 4 ||
                         !contains(target.vtable + target.slot * 4, 4) ||
                         read(target.vtable + target.slot * 4) != base() + target.rva))) {
    error = std::string(target.name) + ": invalid function or vtable slot";
    return false;
  }
  std::size_t count = 0;
  std::uint32_t found = 0;
  for (const auto& section : sections_) {
    if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) || section.Misc.VirtualSize < pattern.size()) continue;
    for (std::uint32_t i = 0; i <= section.Misc.VirtualSize - pattern.size(); ++i) {
      bool match = true;
      for (std::size_t j = 0; j < pattern.size(); ++j) {
        if (pattern[j] >= 0 && at(section.VirtualAddress + i)[j] != pattern[j]) {
          match = false;
          break;
        }
      }
      if (match && target.classVtable) {
        std::array<std::uint8_t, 6> marker{0xc7, 0x06};
        const auto pointer = static_cast<std::uint32_t>(base() + target.classVtable);
        std::memcpy(marker.data() + 2, &pointer, 4);
        const auto* begin = at(section.VirtualAddress + i);
        match = std::search(begin, begin + pattern.size(), marker.begin(), marker.end()) != begin + pattern.size();
      }
      if (match) {
        found = section.VirtualAddress + i;
        if (++count > 1) break;
      }
    }
    if (count > 1) break;
  }
  if (count != 1 || found != target.rva) {
    error = std::string(target.name) + ": missing, ambiguous, or relocated signature";
    return false;
  }
  return true;
}

bool writeWord(void* address, std::uint32_t value) {
  DWORD old = 0;
  if (!VirtualProtect(address, sizeof(value), PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(address, &value, sizeof(value));
  const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, sizeof(value)) != FALSE;
  DWORD ignored = 0;
  const bool restored = VirtualProtect(address, sizeof(value), old, &ignored) != FALSE;
  return flushed && restored;
}

bool Patches::install(HMODULE module, const Profile& profile, std::string& error) {
  if (active()) {
    error = "Already patched";
    return false;
  }
  Image image(module);
  if (!image.valid()) {
    error = "Invalid x86 module image";
    return false;
  }
  for (std::size_t i = 0; i < profile.targetCount; ++i) {
    if (!image.matches(profile.targets[i], error)) return false;
  }
  std::vector<Entry> prepared;
  for (std::size_t i = 0; i < profile.patchCount; ++i) {
    const auto& spec = profile.patches[i];
    if (!image.contains(spec.rva, 4) ||
        (spec.kind == PatchKind::Pointer && (!image.contains(spec.before, 4) || !image.contains(spec.after, 4)))) {
      error = "Patch address outside module";
      return false;
    }
    auto before =
      spec.kind == PatchKind::Pointer ? static_cast<std::uint32_t>(image.base() + spec.before) : spec.before;
    auto after = spec.kind == PatchKind::Pointer ? static_cast<std::uint32_t>(image.base() + spec.after) : spec.after;
    const auto actual = image.read(spec.rva);
    if (spec.kind == PatchKind::ClearFlags) {
      if (!spec.before || spec.after || (actual & spec.before) != spec.before) {
        error = "ConVar flags differ from the reviewed registration";
        return false;
      }
      before = actual;
      after = actual & ~spec.before;
    }
    if (actual != before && !(spec.kind == PatchKind::Cone && actual == 0)) {
      error = "Original patch operand changed; refusing all patches";
      return false;
    }
    prepared.push_back({spec.rva, before, after});
  }
  if (prepared.empty()) {
    error = "Empty patch profile";
    return false;
  }
  module_ = module;
  for (const auto& entry : prepared) {
    // Track even a failed write: page protection restoration can fail after the copy.
    entries_.push_back(entry);
    if (entry.before == entry.after) continue;  // Read-only name/parent identity guard.
    if (!writeWord(image.at(entry.rva), entry.after)) {
      error = "Memory patch failed; rolling back";
      restore();
      return false;
    }
  }
  return true;
}

bool Patches::intact() const {
  if (!active()) return true;
  Image image(module_);
  if (!image.valid()) return false;
  for (const auto& entry : entries_) {
    MEMORY_BASIC_INFORMATION region{};
    if (!image.contains(entry.rva, 4) || !VirtualQuery(image.at(entry.rva), &region, sizeof(region)) ||
        region.AllocationBase != module_ || region.State != MEM_COMMIT || image.read(entry.rva) != entry.after)
      return false;
  }
  return true;
}

bool Patches::restore() {
  bool success = true;
  Image image(module_);
  // Undo the transaction in reverse installation order.
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
    const auto& entry = *it;
    if (entry.before == entry.after) continue;
    MEMORY_BASIC_INFORMATION region{};
    if (!image.contains(entry.rva, 4) || !VirtualQuery(image.at(entry.rva), &region, sizeof(region)) ||
        region.AllocationBase != module_ || region.State != MEM_COMMIT)
      continue;
    // Do not overwrite another plugin's subsequent changes or a newly loaded image.
    const auto actual = image.read(entry.rva);
    if (actual == entry.after)
      success = writeWord(image.at(entry.rva), entry.before) && success;
    else if (actual != entry.before)
      success = false;
  }
  entries_.clear();
  module_ = nullptr;
  return success;
}

bool Patches::active() const { return !entries_.empty(); }

std::wstring modulePath(HMODULE module) {
  std::array<wchar_t, 32768> path{};
  const DWORD size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  return size && size < path.size() ? std::wstring(path.data(), size) : std::wstring();
}

bool hashFile(const std::wstring& path, std::string& hash) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE state = nullptr;
  std::array<unsigned char, 32> digest{};
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
  if (ok) ok = BCryptCreateHash(algorithm, &state, nullptr, 0, nullptr, 0, 0) >= 0;
  std::array<unsigned char, 65536> buffer{};
  while (ok) {
    DWORD count = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) {
      ok = false;
      break;
    }
    if (!count) break;
    ok = BCryptHashData(state, buffer.data(), count, 0) >= 0;
  }
  if (ok) ok = BCryptFinishHash(state, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
  if (state) BCryptDestroyHash(state);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  hash.clear();
  if (ok) {
    constexpr char hex[] = "0123456789abcdef";
    for (auto byte : digest) {
      hash += hex[byte >> 4];
      hash += hex[byte & 15];
    }
  }
  return ok;
}

const Profile* findProfile(const std::string& serverHash) {
  for (const auto& profile : kProfiles)
    if (serverHash == profile.serverHash) return &profile;
  return nullptr;
}

}  // namespace gm
