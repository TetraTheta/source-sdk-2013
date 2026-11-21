#include "keyvalues_settings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace gm {
KeyValues parseKeyValues(const std::string& text) {
  if (text.size() > 1024 * 1024) throw std::runtime_error("GunMaster settings exceed 1 MiB");
  std::vector<std::string> tokens;
  for (std::size_t i = text.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0; i < text.size();) {
    if (std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
      continue;
    }
    if (text.compare(i, 2, "//") == 0) {
      i = text.find('\n', i);
      if (i == std::string::npos) break;
      continue;
    }
    if (text[i] == '{' || text[i] == '}') {
      tokens.emplace_back(1, text[i++]);
      continue;
    }
    std::string token;
    if (text[i] == '"') {
      ++i;
      while (i < text.size() && text[i] != '"') {
        char character = text[i++];
        if (character == '\\') {
          if (i == text.size()) throw std::runtime_error("Unterminated settings escape");
          character = text[i++];
          if (character == 'n') character = '\n';
        }
        token += character;
      }
      if (i == text.size()) throw std::runtime_error("Unterminated settings string");
      ++i;
    } else {
      while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '{' && text[i] != '}')
        token += text[i++];
    }
    tokens.push_back(token);
  }
  KeyValues result;
  std::vector<std::string> path;
  for (std::size_t i = 0; i < tokens.size();) {
    if (tokens[i] == "}") {
      if (path.empty()) throw std::runtime_error("Unexpected settings closing brace");
      path.pop_back();
      ++i;
      continue;
    }
    if (tokens[i].empty() || tokens[i] == "{" || i + 1 == tokens.size() || tokens[i + 1] == "}")
      throw std::runtime_error("Missing settings key/value");
    const auto key = tokens[i++];
    const auto value = tokens[i++];
    if (value == "{") {
      if (path.size() >= 16) throw std::runtime_error("Settings nesting exceeds 16 levels");
      path.push_back(key);
    } else {
      std::string full;
      for (const auto& parent : path) full += parent + "/";
      result.emplace_back(full + key, value);
    }
  }
  if (!path.empty()) throw std::runtime_error("Unclosed settings block");
  return result;
}

bool matchesModel(std::string model, std::string pattern) {
  for (auto* text : {&model, &pattern}) {
    for (auto& c : *text) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  std::size_t at = 0, part = 0, star = std::string::npos, retry = 0;
  while (at < model.size()) {
    if (part < pattern.size() && pattern[part] == '*') {
      star = part++;
      retry = at;
    } else if (part < pattern.size() && pattern[part] == model[at]) {
      ++part;
      ++at;
    } else if (star != std::string::npos) {
      part = star + 1;
      at = ++retry;
    } else
      return false;
  }
  while (part < pattern.size() && pattern[part] == '*') ++part;
  return part == pattern.size();
}

float setting(const KeyValues& values, const std::string& key, float fallback, float minimum, float maximum) {
  for (auto it = values.rbegin(); it != values.rend(); ++it) {
    if (it->first != key) continue;
    char* end = nullptr;
    const float value = std::strtof(it->second.c_str(), &end);
    if (end == it->second.c_str() || end != it->second.c_str() + it->second.size() || !std::isfinite(value))
      return fallback;
    return std::clamp(value, minimum, maximum);
  }
  return fallback;
}
}  // namespace gm
