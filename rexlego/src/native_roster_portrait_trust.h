#pragma once
#include "native_roster_portrait_file.h"
#include "native_roster_portrait_resources.h"
#include <string_view>
#include "native_roster_sha256.h"

namespace legodimensions::native_roster::portrait_file {
// Exact private starter artwork verified from the supplied companion resources.
// Per-entry binding prevents a valid image from silently impersonating another token.
inline constexpr std::array<std::string_view, 4> kTrustedSha256{
    "8ec691de1ac67db5ff68a230ac3b890f3f73cf3662a739ab4dbd78de81f905ba",
    "3deb7bb8f7f1f2f6f90e0cf74942b9f1b63d25a7bf6f452d48f2b83a44a7952c",
    "4997d7221562e39cf602f255170a1f2907c9b8f352a13ee738ed912a058b0dba",
    "043612028f3e59715c6b39f3f0264e22026b6dfb3a3de1ad3eee635e59ec6436"};
inline constexpr std::array<size_t, 4> kTrustedSizes{110583,107852,145681,24775};
inline bool MatchesDigest(size_t entry, size_t size, std::string_view digest) {
  if (entry < kTrustedSha256.size())
    return size == kTrustedSizes[entry] && digest == kTrustedSha256[entry];
  for (const auto& resource : kTrustedResources)
    if (entry == static_cast<size_t>(resource.resource_id))
      return size == resource.bytes && digest == resource.sha256;
  return false;
}
inline bool IsTrusted(size_t entry, const std::vector<uint8_t>& bytes) {
  bool known_size = entry < kTrustedSizes.size() && bytes.size() == kTrustedSizes[entry];
  if (!known_size) for (const auto& resource : kTrustedResources)
    if (entry == static_cast<size_t>(resource.resource_id) && bytes.size() == resource.bytes) known_size = true;
  if (!known_size) return false;
  portrait_sha256::SHA256 hash;
  return MatchesDigest(entry, bytes.size(), hash(bytes.data(), bytes.size()));
}
// Hash the same in-memory bytes subsequently decoded: no reopen/TOCTOU gap.
inline std::vector<uint8_t> ReadTrusted(const std::filesystem::path& path, size_t entry,
                                       Dimensions& out) {
  auto bytes = Read(path, out);
  if (!IsTrusted(entry, bytes)) { out = {}; return {}; }
  return bytes;
}
} // namespace legodimensions::native_roster::portrait_file
