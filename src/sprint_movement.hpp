#pragma once

#include "runtime_patches.hpp"

namespace gm {

// Apply only the extra sprint boost after Valve has performed its ordinary crop.
float crouchSprintScale(bool alreadyCropped, bool nowCropped, bool sprinting, float normal, float maximum);

class Movement {
 public:
  bool install(HMODULE server, HMODULE client, const MovementSpec& spec, std::string& error);
  bool intact() const;
  bool restore();
  void crop(void* movement, bool client);

 private:
  HMODULE server_ = nullptr;
  HMODULE client_ = nullptr;
  HMODULE retained_ = nullptr;
  MovementSpec spec_{};
  bool enabled_ = false;
};

}  // namespace gm
