#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#include "native_roster_vehicle_ids.h"

namespace legodimensions::native_roster {
inline constexpr size_t kMaxCatalogEntries = 4096;
using TagBytes = std::array<uint8_t, 180>;
using TagUid = std::array<uint8_t, 7>;
struct Tag {
  std::filesystem::path path;
  std::string name;
  TagUid uid{};
  uint32_t initial_id = 0;
  int portrait = -1; // Only an explicitly trusted starter artwork binding.
  std::string unavailable;
};
using TagCatalog = std::vector<Tag>;
inline TagUid ReadUid(const TagBytes& b) { return {b[0],b[1],b[2],b[4],b[5],b[6],b[7]}; }
inline bool ReadWritableTag(const std::filesystem::path& path, TagBytes& out) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
  std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
  if (!f) return false;
  f.read(reinterpret_cast<char*>(out.data()), out.size());
  if (f.gcount() != static_cast<std::streamsize>(out.size())) return false;
  char extra;
  return !f.get(extra) && f.eof();
}
inline bool ValidUid(const TagUid& uid) {
  return std::any_of(uid.begin(), uid.end(), [](uint8_t b) { return b != 0; }) &&
         std::any_of(uid.begin(), uid.end(), [](uint8_t b) { return b != 255; });
}
inline bool SameToken(const Tag& tag, const TagBytes& bytes, uint32_t id) {
  if (!tag.unavailable.empty() || !ValidUid(tag.uid) || ReadUid(bytes) != tag.uid) return false;
  // Character identities are immutable. Vehicle builds/upgrades are game-owned;
  // the token UID, rather than the current vehicle model, is its stable identity.
  return PermittedModel(tag.initial_id, id);
}
inline std::string DisplayName(const std::filesystem::path& path) {
  std::string s = path.stem().string();
  for (char& c : s) if (static_cast<unsigned char>(c) < 32 || c == 127 || c == '#') c = ' ';
  if (s.size() > 96) s.resize(96);
  return s.empty() ? "Unnamed token" : s;
}
inline TagCatalog StarterCatalog(const std::array<std::filesystem::path, 4>& paths) {
  constexpr const char* names[] = {"Batman", "Gandalf", "Wyldstyle", "Batmobile"};
  TagCatalog result;
  for (size_t i = 0; i < paths.size(); ++i) {
    Tag tag; tag.path = paths[i]; tag.name = names[i]; tag.portrait = static_cast<int>(i);
    result.push_back(std::move(tag));
  }
  return result;
}
// Nonrecursive, bounded discovery. Missing starter placeholders retain the old
// four indices. Existing files are read only; nothing is created or rewritten.
inline TagCatalog DiscoverCatalog(const std::filesystem::path& root,
                                 const std::function<uint32_t(const TagBytes&)>& inspect,
                                 std::string* warning = nullptr) {
  TagCatalog result = StarterCatalog({root/"batman.bin",root/"gandalf.bin",root/"wyldstyle.bin",root/"batmobile.bin"});
  std::vector<std::filesystem::path> candidates;
  std::error_code ec;
  auto it = std::filesystem::directory_iterator(root, ec);
  if (ec && warning) *warning = "Tag folder unavailable; configure existing writable .bin tags.";
  // Abort discovery if directory enumeration cannot be bounded. Do not silently
  // select a nondeterministic subset from an oversized directory.
  size_t scanned = 0;
  for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    if (++scanned > kMaxCatalogEntries * 4) {
      if (warning) *warning = "Tag folder too large; use a smaller dedicated directory.";
      candidates.clear(); break;
    }
    auto path = it->path();
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".bin") candidates.push_back(std::move(path));
  }
  std::sort(candidates.begin(), candidates.end());
  auto validate = [&](Tag& tag, int starter) {
    TagBytes bytes{};
    if (!ReadWritableTag(tag.path, bytes)) { tag.unavailable = "Requires an existing writable 180-byte tag."; return; }
    tag.uid = ReadUid(bytes); tag.initial_id = inspect(bytes);
    if (!ValidUid(tag.uid) || tag.initial_id > 65535) {
      tag.unavailable = "Unrecognized token identity or UID."; return;
    }
    if (starter >= 0 && (starter < 3 ? tag.initial_id != uint32_t(starter + 1) :
                                      tag.initial_id < 1006 || tag.initial_id > 1008)) {
      tag.unavailable = "Starter filename contains a different token."; return;
    }
    for (const auto& previous : result) {
      if (&previous == &tag) break;
      if (!previous.unavailable.empty() || !ValidUid(previous.uid)) continue;
      ec.clear();
      if (std::filesystem::equivalent(tag.path, previous.path, ec) && !ec) {
        tag.unavailable = "Writable file aliases another catalog entry."; return;
      }
    }
  };
  for (int i = 0; i < 4; ++i) validate(result[i], i);
  for (const auto& path : candidates) {
    if (std::any_of(result.begin(), result.end(), [&](const Tag& t) { return t.path == path; })) continue;
    Tag tag; tag.path = path; tag.name = DisplayName(path);
    validate(tag, -1);
    if (!tag.unavailable.empty()) continue;
    if (result.size() >= kMaxCatalogEntries) {
      if (warning) *warning = "Catalog limit reached (4096 entries). Use a smaller tag folder.";
      break;
    }
    if (tag.initial_id == 0) tag.name += " (blank gadget)";
    result.push_back(std::move(tag));
  }
  return result;
}
} // namespace legodimensions::native_roster
