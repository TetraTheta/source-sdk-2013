#pragma once

#include <string>
#include <utility>
#include <vector>

namespace gm {
using KeyValues = std::vector<std::pair<std::string, std::string>>;
KeyValues parseKeyValues(const std::string& text);
bool matchesModel(std::string model, std::string pattern);
float setting(const KeyValues& values, const std::string& key, float fallback, float minimum, float maximum);
}  // namespace gm
