#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace legodimensions::native_roster {
// UI-thread-only bookkeeping. Eviction/service MUST precede the frame's first
// portrait draw command; do not destroy textures referenced by queued ImGui draws.
class PortraitCacheBudget {
 public:
  static constexpr size_t kMaxTextures = 32;
  static constexpr size_t kMaxRgbaBytes = 32 * 1024 * 1024;
  explicit PortraitCacheBudget(size_t count) : entries_(count) {}
  void BeginFrame(const std::vector<int>& wanted) {
    ++frame_;
    for (auto& e : entries_) e.wanted = false;
    for (int i : wanted) if (i >= 0 && size_t(i) < entries_.size()) {
      entries_[i].wanted = true;
      entries_[i].last_use = frame_;
    }
  }
  int Next(const std::vector<int>& wanted) {
    for (size_t n = 0; n < wanted.size(); ++n) {
      const size_t pos = (cursor_ + n) % wanted.size();
      const int i = wanted[pos];
      if (i < 0 || size_t(i) >= entries_.size()) continue;
      const auto& e = entries_[i];
      if (!e.bytes && !e.failed) { cursor_ = pos + 1; return i; }
    }
    return -1;
  }
  void Fail(size_t i) { entries_.at(i).failed = true; }
  // Release idle textures before allocation, counting candidate memory too.
  // Pinned entries are never evicted; insufficient budget means label fallback.
  template<class Release> bool Reserve(size_t bytes, Release release) {
    if (!bytes || bytes > kMaxRgbaBytes) return false;
    while (count_ >= kMaxTextures || bytes_ > kMaxRgbaBytes - bytes) {
      size_t victim = entries_.size();
      for (size_t i = 0; i < entries_.size(); ++i) {
        if (!entries_[i].bytes || entries_[i].wanted) continue;
        if (victim == entries_.size() || entries_[i].last_use < entries_[victim].last_use) victim = i;
      }
      if (victim == entries_.size()) return false;
      release(victim);
      bytes_ -= entries_[victim].bytes;
      entries_[victim].bytes = 0;
      --count_;
    }
    return true;
  }
  void Commit(size_t i, size_t bytes) {
    entries_.at(i).bytes = bytes;
    bytes_ += bytes; ++count_;
  }
  size_t Count() const { return count_; }
  size_t Bytes() const { return bytes_; }
 private:
  struct Entry { size_t bytes = 0; uint64_t last_use = 0; bool wanted = false, failed = false; };
  std::vector<Entry> entries_;
  size_t cursor_ = 0;
  uint64_t frame_ = 0;
  size_t bytes_ = 0, count_ = 0;
};
} // namespace legodimensions::native_roster
