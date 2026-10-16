#pragma once
#include "native_roster_metadata.h"
#include "native_roster_catalog.h"
#include "native_roster_model_ids.h"
#include <filesystem>
#include <string>
namespace legodimensions::native_roster {
inline const metadata::Record* MetadataFor(const std::filesystem::path& path) {
  const auto filename = path.filename().string();
  for (const auto& r : metadata::kRecords)
    if (filename == "resource_" + std::to_string(r.resource) + ".bin") return &r;
  const char* alias = filename=="batman.bin" ? "Batman" : filename=="gandalf.bin" ? "Gandalf" :
                      filename=="wyldstyle.bin" ? "Wyldstyle" : filename=="batmobile.bin" ? "Batmobile" : nullptr;
  if (alias) for (const auto& r : metadata::kRecords) if (r.name == std::string_view(alias)) return &r;
  return nullptr;
}
// Filenames choose optional metadata only after decoded model validation.
inline const metadata::Record* MetadataFor(const Tag& tag) {
  if (!tag.unavailable.empty()) return nullptr;
  const auto* record = MetadataFor(tag.path);
  return record && PermittedModel(ExpectedModel(record->resource), tag.initial_id) ? record : nullptr;
}
inline bool KnownResource(int id) {
  for (const auto& r : metadata::kRecords) if (r.resource==id) return true;
  return false;
}
} // namespace legodimensions::native_roster
