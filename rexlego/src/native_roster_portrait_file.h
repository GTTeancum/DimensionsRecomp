#pragma once
// Private, optional PNGs only. No artwork or tag data is bundled or modified.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace legodimensions::native_roster::portrait_file {
inline constexpr size_t kMaxBytes = 4 * 1024 * 1024;
inline constexpr uint32_t kMaxDimension = 2048;
struct Dimensions { uint32_t width = 0, height = 0; };
inline uint32_t BigEndian(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
// Allocation preflight, not an image decoder. The SDK still validates/decodes PNG.
inline bool ValidateHeader(const uint8_t* bytes, size_t size, Dimensions& out) {
  out = {};
  constexpr std::array<uint8_t, 8> signature{137,80,78,71,13,10,26,10};
  if (!bytes || size < 33 || size > kMaxBytes ||
      !std::equal(signature.begin(), signature.end(), bytes) ||
      BigEndian(bytes + 8) != 13 || bytes[12] != 'I' || bytes[13] != 'H' ||
      bytes[14] != 'D' || bytes[15] != 'R') return false;
  const uint32_t width = BigEndian(bytes + 16), height = BigEndian(bytes + 20);
  if (!width || !height || width > kMaxDimension || height > kMaxDimension) return false;
  const unsigned depth = bytes[24], color = bytes[25];
  const bool valid_depth = color == 0 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) :
                           color == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8) :
                           (color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16);
  if (!valid_depth || bytes[26] != 0 || bytes[27] != 0 || bytes[28] > 1) return false;
  out = {width, height};
  return true;
}
inline std::filesystem::path Sibling(const std::filesystem::path& tag) {
  if (tag.empty()) return {};
  auto path = tag;
  path.replace_extension(".png");
  return path;
}
inline std::vector<uint8_t> Read(const std::filesystem::path& path, Dimensions& out) {
  out = {};
  std::error_code ec;
  if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec) return {};
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) return {};
  const auto length = stream.tellg();
  if (length < 33 || length > static_cast<std::streamoff>(kMaxBytes)) return {};
  stream.seekg(0);
  std::array<uint8_t, 33> header{};
  Dimensions expected;
  if (!stream.read(reinterpret_cast<char*>(header.data()), header.size()) ||
      !ValidateHeader(header.data(), header.size(), expected)) return {};
  std::vector<uint8_t> bytes(static_cast<size_t>(length));
  std::copy(header.begin(), header.end(), bytes.begin());
  if (!stream.read(reinterpret_cast<char*>(bytes.data() + header.size()),
                   static_cast<std::streamsize>(bytes.size() - header.size()))) return {};
  // Reject growth during read too; bounded reads never follow an unbounded stream.
  if (stream.peek() != std::ifstream::traits_type::eof()) return {};
  if (!ValidateHeader(bytes.data(), bytes.size(), out)) return {};
  return bytes;
}
} // namespace legodimensions::native_roster::portrait_file
