#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include "native_roster_catalog.h"

namespace rex::ui { class ImGuiDialog; class ImGuiDrawer; class ImmediateDrawer; }

namespace legodimensions::native_roster {

enum class Entry : uint32_t { Batman, Gandalf, Wyldstyle, Batmobile };
// Logical slots, NOT SDK indices. The adapter must explicitly translate them.
enum class Slot : uint8_t { Left1, Left2, Left3, Center, Right1, Right2, Right3 };
enum class Region : uint8_t { Left, Center, Right };
inline constexpr std::array<const char*, 4> kNames{"Batman", "Gandalf", "Wyldstyle", "Batmobile"};
inline constexpr std::array<const char*, 7> kSlotNames{
    "Left 1", "Left 2", "Left 3", "Center", "Right 1", "Right 2", "Right 3"};

struct Result {
  bool ok = false;
  std::string error;
};

// All callbacks run on one owned worker, never the UI thread. The host must
// exclude other portal mutation clients (including SDK UI and companion).
// A failed Result MUST mean no mutation; throw on indeterminate state.
struct Backend {
  std::function<std::array<bool, 7>()> occupied;
  // Open this EXISTING writable tag afresh in r+b, read exactly 180 bytes,
  // validate its intended identity, then pass fresh bytes and the writable
  // path to SDK LoadFigure (the SDK owns persistence by path, not a handle).
  // No cached bytes, generated tag, truncation, or overwriting writable saves.
  std::function<Result(Entry, const std::filesystem::path&, Slot)> load_fresh;
  std::function<Result(Slot)> remove;
  std::function<Result(Slot)> temporary_remove;
  // Source still owns the figure after temporary_remove. Same-slot move
  // restores it on cancellation. Adapter must support this SDK behavior.
  std::function<Result(Slot, Slot)> move;
};

struct Snapshot {
  std::vector<int> slots = std::vector<int>(4, -1);
  std::array<bool, 7> occupied{};
  bool occupancy_known = false;
  int lifted_entry = -1; // Source remains reserved in slots/occupied while absent to game.
  bool busy = false;
  bool faulted = false;
  std::string status = "Select a character or vehicle, then a placement region.";
};

class Controller final {
 public:
  Controller(Backend backend, TagCatalog catalog);
  Controller(Backend backend, std::array<std::filesystem::path, 4> writable_tags);
  const TagCatalog& Catalog() const; // Immutable for controller lifetime.
  ~Controller();
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;
  Snapshot Read() const;
  bool Refresh(); // Worker-only occupancy refresh; rejected while busy.
  bool Lift(Entry entry); // Real remove event; held until Drop/Restore/Remove.
  bool Drop(Slot slot); // Drop the currently lifted entry.
  void Restore(); // Async, also cancels any uncommitted move/lift.
  bool Place(Entry entry, Slot slot); // Adds or moves, never replaces.
  bool PlaceInRegion(Entry entry, Region region); // Chooses a free slot on worker.
  bool Remove(Entry entry);
  bool Clear(); // Remove owned tokens only; stop at first save failure.
  void CancelClear(); // Stop before the next removal; committed removals stay removed.
  void CancelMove();
  // Must be called before SDK/backend teardown, on the owner thread. Cancels,
  // restores a temporarily removed figure, then joins. Idempotent.
  void Shutdown();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct TokenStatus { uint32_t model = 0; bool reserved = false, lifted = false, save_failed = false; };
struct Host {
  std::function<std::array<TokenStatus, 7>()> token_status;
  // Nonblocking live game LED colours in protocol order: center, left, right.
  std::function<std::array<std::array<uint8_t, 3>, 3>()> pad_lights;
  // Consume an atomic open request set by the host input hook/key binding.
  std::function<bool()> take_open_request;
  std::function<uint16_t()> pad_buttons;
  // Owner-specific normalized raw axes, sampled before guest suppression.
  std::function<std::array<float, 2>()> pad_axes;
  // Must combine this reason with other overlays' input-block reasons.
  std::function<void(bool)> set_input_blocked;
  std::function<void(bool)> on_frame;
  std::function<bool()> other_overlay_open;
};

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(
    rex::ui::ImGuiDrawer* drawer, std::shared_ptr<Controller> controller, Host host,
    rex::ui::ImmediateDrawer* textures, TagCatalog tags);
} // namespace legodimensions::native_roster
