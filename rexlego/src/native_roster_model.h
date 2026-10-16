#pragma once
#include "native_roster.h"
#include <cmath>
#include <algorithm>

namespace legodimensions::native_roster {
// Deliberate second press within four seconds; release edges are supplied by the UI.
struct ClearConfirmation {
  float remaining = 0;
  bool armed() const { return remaining > 0; }
  void Reset() { remaining = 0; }
  void Tick(float dt) { remaining = std::max(0.f, remaining - std::max(0.f, dt)); }
  bool Press() { if (armed()) { Reset(); return true; } remaining = 4; return false; }
};

// Pure interaction model: previews never mutate a Toy Pad.
struct PadModel {
  enum class Mode { Browse, Picker, Carrying, Pending };
  enum class Operation { Add, Move, Remove, Lift, Restore, Clear };
  Operation operation = Operation::Move;
  enum class Action { None, Place, Lift, Cancel, Close };
  Mode mode = Mode::Browse;
  int focus = 3, entry = 0, source = -1;
  int entry_count = 4;
  static constexpr int kPageSize = 4;
  void Page(int delta) {
    if (mode != Mode::Picker || entry_count <= 0) return;
    const int pages = (entry_count + kPageSize - 1) / kPageSize;
    const int page = ((entry / kPageSize) + (delta < 0 ? pages - 1 : 1)) % pages;
    entry = std::min(entry_count - 1, page * kPageSize + entry % kPageSize);
  }
  bool cancel_requested = false;
  static int Owner(const Snapshot& s, int slot) {
    for (int i = 0; i < static_cast<int>(s.slots.size()); ++i) if (s.slots[i] == slot) return i;
    return -1;
  }
  void Navigate(int dx, int dy) {
    if (mode == Mode::Pending) return;
    if (mode == Mode::Picker) {
      const int step = dy ? dy : dx;
      if (step && entry_count > 0) entry = (entry + (step > 0 ? 1 : entry_count - 1)) % entry_count;
      return;
    }
    // Logical order: upper left, lower left pair, center, upper right, lower right pair.
    constexpr int left[7]  = {0, 1, 1, 0, 3, 3, 5};
    constexpr int right[7] = {3, 2, 3, 4, 4, 6, 6};
    constexpr int up[7]    = {0, 0, 0, 3, 4, 4, 4};
    constexpr int down[7]  = {2, 1, 2, 3, 5, 5, 6};
    if (dx) focus = dx < 0 ? left[focus] : right[focus];
    else if (dy) focus = dy < 0 ? up[focus] : down[focus];
  }
  Action Activate(const Snapshot& s) {
    if (s.busy || s.faulted || !s.occupancy_known || mode == Mode::Pending) return Action::None;
    if (mode == Mode::Picker) {
      if (entry < 0 || entry >= static_cast<int>(s.slots.size())) return Action::None;
      source = s.slots[entry];
      if (source >= 0) { focus = source; mode = Mode::Browse; }
      else mode = Mode::Carrying;
      return Action::None;
    }
    if (mode == Mode::Browse) {
      entry = Owner(s, focus);
      if (entry >= 0) { source = focus; return Action::Lift; }
      else if (!s.occupied[focus] && !s.slots.empty()) { entry = 0; source = -1; mode = Mode::Picker; }
      return Action::None;
    }
    if (focus == source) return Action::Cancel;
    if (s.occupied[focus]) return Action::None;
    return Action::Place;
  }
  void Submitted(Operation kind = Operation::Move) { operation = kind; mode = Mode::Pending; cancel_requested = false; }
  void Observe(const Snapshot& s) {
    if (mode != Mode::Pending || s.busy) return;
    if (s.lifted_entry >= 0) {
      entry = s.lifted_entry; source = s.slots[entry];
      mode = Mode::Carrying; cancel_requested = false; return;
    }
    // Reflect the real outcome, including a commit that beat cancellation.
    if (entry >= 0 && entry < static_cast<int>(s.slots.size()) && s.slots[entry] >= 0) focus = s.slots[entry];
    mode = Mode::Browse;
    cancel_requested = false;
  }
  Action Back() {
    if (mode == Mode::Pending) {
      if (cancel_requested || (operation != Operation::Move && operation != Operation::Lift && operation != Operation::Clear)) return Action::None;
      cancel_requested = true;
      return Action::Cancel;
    }
    if (mode == Mode::Carrying) {
      if (source >= 0) return Action::Cancel;
      mode = Mode::Browse;
      return Action::None;
    }
    if (mode == Mode::Picker) { mode = Mode::Browse; return Action::None; }
    return Action::Close;
  }
};

// Normalized XInput left stick: positive Y means up. Require neutral on open.
struct StickNavigation {
  int x = 0, y = 0;
  float until_repeat = 0;
  bool armed = false;
  void Reset() { *this = {}; }
  std::array<int, 2> Read(float lx, float ly, float dt) {
    const float magnitude = std::max(std::abs(lx), std::abs(ly));
    if (magnitude < .35f) { x = y = 0; armed = true; return {}; }
    if (!armed) return {};
    if (magnitude < .55f) return {};
    const int nx = std::abs(lx) >= std::abs(ly) ? (lx > 0 ? 1 : -1) : 0;
    const int ny = nx ? 0 : (ly > 0 ? -1 : 1);
    if (nx != x || ny != y) { x = nx; y = ny; until_repeat = .35f; return {x, y}; }
    until_repeat -= std::max(0.f, dt);
    if (until_repeat <= 0) { until_repeat = .12f; return {x, y}; }
    return {};
  }
};
} // namespace legodimensions::native_roster
