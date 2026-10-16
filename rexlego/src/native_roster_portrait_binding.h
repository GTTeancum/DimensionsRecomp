#pragma once
#include "native_roster_portrait_resources.h"
#include <filesystem>
#include <string>
namespace legodimensions::native_roster::portrait_file {
// resource_<TAG resource ID>.bin is paired with resource_<TAG resource ID>.png,
// whose digest is that tag's separate PORTRAIT resource ID. No arbitrary names.
inline int PortraitForTagPath(const std::filesystem::path& tag) {
  const auto filename = tag.filename().string();
  constexpr const char* prefix = "resource_";
  if (filename.compare(0, 9, prefix) || filename.size() < 14 ||
      filename.compare(filename.size() - 4, 4, ".bin")) return -1;
  const std::string digits = filename.substr(9, filename.size() - 13);
  if (digits.empty() || digits.size() > 6 || (digits.size() > 1 && digits[0] == '0')) return -1;
  int id = 0;
  for (char c : digits) { if (c < '0' || c > '9') return -1; id = id * 10 + c - '0'; }
  return PortraitForTagResource(id);
}
}
