#pragma once

#include <cstddef>

#include "runtime_patches.hpp"

namespace gm {

// Retail CCommand::Arg uses argv at 0x408; no game-entity layout is compiled here.
struct CommandArgs {
  int count;
  int argv0Size;
  char text[512];
  char tokens[512];
  const char* argv[64];
};
static_assert(offsetof(CommandArgs, argv) == 0x408, "");

enum class ToggleAction { Toggle, Keep, Usage };
ToggleAction toggleAction(int count, const char* argument, bool enabled);

// One shared ConCommand dispatch slot; all other commands keep Valve's dispatcher.
class Commands {
 public:
  bool install(HMODULE module, const CommandsSpec& spec, std::string& error);
  bool intact() const;
  bool restore();
  void dispatch(void* command, const CommandArgs& args);

 private:
  bool identities() const;
  HMODULE module_ = nullptr;
  HMODULE retained_ = nullptr;
  CommandsSpec spec_{};
  bool enabled_ = false;
};

}  // namespace gm
