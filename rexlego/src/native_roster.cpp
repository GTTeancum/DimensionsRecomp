#include "native_roster.h"
#include "native_roster_model.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#ifndef NATIVE_ROSTER_CONTROLLER_ONLY
#include <imgui.h>
#include "native_roster_browser.h"
#include "native_roster_portrait_trust.h"
#include "native_roster_portrait_cache.h"
#include <rex/ui/image_decode.h>
#include <rex/logging.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/input/input.h>
#include <rex/ui/imgui_dialog.h>
#endif

namespace legodimensions::native_roster {
namespace {
constexpr size_t Index(Entry e) { return static_cast<size_t>(e); }
constexpr size_t Index(Slot s) { return static_cast<size_t>(s); }
}

struct Controller::Impl {
  using Clock = std::chrono::steady_clock;
  enum class Kind { Place, Lift, Clear };
  struct Command { Entry entry; std::optional<Slot> target; uint64_t generation;
                   std::optional<Region> region; Kind kind = Kind::Place; };
  Backend backend;
  TagCatalog catalog;
  mutable std::mutex mutex;
  std::condition_variable cv;
  Snapshot state;
  std::optional<Command> pending;
  bool refresh_requested = false, restore_requested = false;
  uint64_t generation = 0;
  bool stopping = false;
  Clock::time_point lifted_at{}; // Worker-owned. State owns reserved source identity.
  std::thread worker;

  Impl(Backend b, TagCatalog p)
      : backend(std::move(b)), catalog(std::move(p)) {
    if (catalog.size() > kMaxCatalogEntries) catalog.resize(kMaxCatalogEntries);
    state.slots.assign(catalog.size(), -1);
    if (!backend.occupied || !backend.load_fresh || !backend.remove ||
        !backend.temporary_remove || !backend.move) {
      state.faulted = true;
      state.status = "Native Toy Pad backend is not configured.";
    }
    worker = std::thread([this] { Run(); });
  }
  bool Submit(Entry entry, std::optional<Slot> target,
              std::optional<Region> region = {}, Kind kind = Kind::Place) {
    std::lock_guard lock(mutex);
    if (stopping || state.busy || state.faulted || restore_requested) return false;
    if (Index(entry) >= catalog.size() || (target && Index(*target) >= kSlotNames.size()) ||
        (region && static_cast<unsigned>(*region) > 2)) {
      state.status = "Invalid roster entry or pad slot."; return false;
    }
    if (state.lifted_entry >= 0 && state.lifted_entry != int(Index(entry))) {
      state.status = "Drop or restore the lifted token first."; return false;
    }
    state.busy = true;
    state.status = "Updating Toy Pad...";
    pending = Command{entry, target, generation, region, kind};
    cv.notify_one(); return true;
  }
  void Finish(std::string message, std::optional<std::pair<size_t, int>> position = {},
              bool faulted = false) {
    std::array<bool, 7> occupancy{}; bool known = false;
    try { occupancy = backend.occupied(); known = true; } catch (...) {}
    std::lock_guard lock(mutex);
    state.occupied = occupancy; state.occupancy_known = known;
    if (position) state.slots[position->first] = position->second;
    state.status = std::move(message); state.faulted |= faulted;
    // A queued restoration is part of the current transaction for UI closing.
    state.busy = restore_requested || pending.has_value();
  }
  void ClearHeld() { std::lock_guard lock(mutex); state.lifted_entry = -1; }
  bool RestoreHeld() {
    int entry, source;
    { std::lock_guard lock(mutex); entry = state.lifted_entry;
      source = entry < 0 ? -1 : state.slots[entry]; }
    if (entry < 0) return true;
    if (source < 0) return false;
    const auto restored = backend.move(static_cast<Slot>(source), static_cast<Slot>(source));
    if (restored.ok) ClearHeld();
    return restored.ok;
  }
  void ExecuteClear(const Command& command) {
    size_t removed = 0;
    try {
      // Only the controller's owned entries are eligible, never arbitrary SDK occupancy.
      for (size_t entry = 0; entry < catalog.size(); ++entry) {
        int source; bool cancelled;
        { std::lock_guard lock(mutex);
          cancelled = stopping || command.generation != generation;
          source = state.slots[entry];
        }
        if (cancelled) { Finish("Clear cancelled after " + std::to_string(removed) + " removals; remaining tokens retained."); return; }
        if (source < 0) continue;
        if (source >= int(kSlotNames.size()) || !backend.occupied()[size_t(source)]) {
          Finish("Clear stopped: owned token state changed; remaining tokens retained.", {}, true); return;
        }
        // Admission is short; never hold the UI mutex during a possible tag save.
        { std::lock_guard lock(mutex); cancelled = stopping || command.generation != generation; }
        if (cancelled) { Finish("Clear cancelled after " + std::to_string(removed) + " removals; remaining tokens retained."); return; }
        const auto result = backend.remove(static_cast<Slot>(source));
        if (!result.ok) {
          Finish("Clear stopped after " + std::to_string(removed) + " removals: " + catalog[entry].name + ": " + result.error + ". This and remaining tokens were retained."); return;
        }
        { std::lock_guard lock(mutex);
          state.slots[entry] = -1;
          if (state.lifted_entry == int(entry)) state.lifted_entry = -1;
        }
        ++removed;
      }
      Finish("Cleared " + std::to_string(removed) + " owned tokens. Unmanaged slots were left unchanged.");
    } catch (const std::exception& e) {
      Finish(std::string("Clear state uncertain: ") + e.what() + "; stopped without attempting other tokens.", {}, true);
    } catch (...) { Finish("Clear state uncertain; stopped without attempting other tokens.", {}, true); }
  }
  void Execute(Command command) {
    if (command.kind == Kind::Clear) { ExecuteClear(command); return; }
    int source;
    { std::lock_guard lock(mutex);
      source = state.slots[Index(command.entry)];
      if (stopping || command.generation != generation) {
        state.status = "Operation cancelled."; state.busy = restore_requested || pending.has_value(); return;
      }
    }
    try {
      const auto occupied = backend.occupied();
      if (source >= 0 && !occupied[size_t(source)]) {
        Finish("Toy Pad changed outside the roster; stop and resynchronize.", {}, true); return;
      }
      if (command.kind == Kind::Lift) {
        if (source < 0) { Finish("This entry is already off the Toy Pad."); return; }
        bool held; { std::lock_guard lock(mutex); held = state.lifted_entry >= 0; }
        if (!held) {
          // Linearize pickup with cancellation; SDK callback emits one short event.
          Result result;
          { std::lock_guard lock(mutex);
            if (stopping || command.generation != generation) {
              state.busy = restore_requested || pending.has_value(); return;
            }
          }
          // Admission above linearizes this pickup. Never hold the UI snapshot
          // mutex while the SDK may wait for a guest tag save.
          result = backend.temporary_remove(static_cast<Slot>(source));
          { std::lock_guard lock(mutex);
            if (result.ok) { state.lifted_entry = int(Index(command.entry)); lifted_at = Clock::now(); }
          }
          if (!result.ok) { Finish("Lift failed: " + result.error); return; }
        }
        Finish("Token lifted off the Toy Pad. Choose a destination or restore it."); return;
      }
      if (command.region) {
        const int first = *command.region == Region::Left ? 0 : *command.region == Region::Center ? 3 : 4;
        const int last = *command.region == Region::Left ? 2 : *command.region == Region::Center ? 3 : 6;
        if (source >= first && source <= last) command.target = static_cast<Slot>(source);
        else for (int i = first; i <= last; ++i) if (!occupied[i]) { command.target = static_cast<Slot>(i); break; }
        if (!command.target) { Finish("That region is full."); return; }
      }
      if (!command.target) {
        if (source < 0) { Finish("This entry is already off the Toy Pad."); return; }
        // SDK lifted flag suppresses a duplicate remove notification. Save failure
        // leaves the held token recoverable, including its current live bytes.
        const auto result = backend.remove(static_cast<Slot>(source));
        if (!result.ok) { Finish("Remove failed: " + result.error); return; }
        ClearHeld(); Finish("Token removed.", {{Index(command.entry), -1}}); return;
      }
      const Slot target = *command.target;
      if (source == int(Index(target))) {
        if (!RestoreHeld()) { Finish("Restore failed; token remains held.", {}, true); return; }
        Finish("Original position restored."); return;
      }
      if (occupied[Index(target)]) { Finish("That slot is occupied."); return; }
      if (source < 0) {
        const auto& path = catalog[Index(command.entry)].path; std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error) || error ||
            std::filesystem::file_size(path, error) != 180 || error) {
          Finish("Configure an existing writable 180-byte tag file. No tag was created."); return;
        }
        for (size_t i = 0; i < catalog.size(); ++i) {
          if (i == Index(command.entry) || catalog[i].path.empty() || !catalog[i].unavailable.empty()) continue;
          error.clear();
          if (std::filesystem::equivalent(path, catalog[i].path, error) && !error) {
            Finish("Each roster entry requires its own writable tag file."); return;
          }
        }
        const auto result = backend.load_fresh(command.entry, path, target);
        if (!result.ok) { Finish("Placement failed: " + result.error); return; }
      } else {
        bool held; { std::lock_guard lock(mutex); held = state.lifted_entry >= 0; }
        if (!held) {
          const auto result = backend.temporary_remove(static_cast<Slot>(source));
          if (!result.ok) { Finish("Lift failed: " + result.error); return; }
          { std::lock_guard lock(mutex); state.lifted_entry = int(Index(command.entry)); lifted_at = Clock::now(); }
        }
        bool cancelled;
        { std::unique_lock lock(mutex);
          cancelled = cv.wait_until(lock, lifted_at + std::chrono::milliseconds(500), [&] {
            return stopping || command.generation != generation;
          });
        }
        if (cancelled) {
          const bool restored = RestoreHeld();
          Finish(restored ? "Move cancelled; original position restored." : "Restore failed; token remains held.", {}, !restored); return;
        }
        if (backend.occupied()[Index(target)]) {
          // Keep an explicitly lifted token held rather than silently changing its placement.
          Finish("Destination is occupied. Choose another slot or restore the token."); return;
        }
        Result moved;
        { std::lock_guard lock(mutex);
          // This check is commit admission; cancellation after it observes the
          // real outcome, rather than pretending the admitted drop was undone.
          cancelled = stopping || command.generation != generation;
        }
        moved = backend.move(static_cast<Slot>(source), cancelled ? static_cast<Slot>(source) : target);
        { std::lock_guard lock(mutex); if (moved.ok) state.lifted_entry = -1; }
        if (!moved.ok) { Finish("Drop failed: " + moved.error + "; token remains held."); return; }
        if (cancelled) { Finish("Move cancelled; original position restored."); return; }
      }
      Finish("Token placed. Waiting for the game to recognize it.", {{Index(command.entry), int(Index(target))}});
    } catch (const std::exception& e) {
      bool restored = false; try { restored = RestoreHeld(); } catch (...) {}
      Finish(std::string("Toy Pad state uncertain: ") + e.what() + (restored ? "" : "; restoration failed"), {}, true);
    } catch (...) {
      try { RestoreHeld(); } catch (...) {}
      Finish("Toy Pad state uncertain after backend error.", {}, true);
    }
  }
  void Run() {
    for (;;) {
      Command command;
      { std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return stopping || pending || refresh_requested || restore_requested; });
        if (restore_requested) {
          restore_requested = false; lock.unlock();
          bool restored = false; try { restored = RestoreHeld(); } catch (...) {}
          Finish(restored ? "Original position restored." : "Restore failed; token remains held.", {}, !restored);
          continue;
        }
        if (pending) { command = *pending; pending.reset(); }
        else if (stopping) return;
        else { refresh_requested = false; lock.unlock(); Finish("Select a token or an empty slot."); continue; }
      }
      Execute(command);
    }
  }
};

Controller::Controller(Backend b, TagCatalog p)
    : impl_(std::make_unique<Impl>(std::move(b), std::move(p))) {}
Controller::Controller(Backend b, std::array<std::filesystem::path, 4> p)
    : Controller(std::move(b), StarterCatalog(p)) {}
const TagCatalog& Controller::Catalog() const { return impl_->catalog; }
Controller::~Controller() { Shutdown(); }
Snapshot Controller::Read() const { std::lock_guard lock(impl_->mutex); return impl_->state; }
bool Controller::Refresh() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->stopping || impl_->state.busy || impl_->state.faulted) return false;
  impl_->state.busy = true; impl_->state.occupancy_known = false;
  impl_->refresh_requested = true; impl_->cv.notify_one(); return true;
}
bool Controller::Lift(Entry e) { return impl_->Submit(e, {}, {}, Impl::Kind::Lift); }
bool Controller::Drop(Slot slot) {
  int entry; { std::lock_guard lock(impl_->mutex); entry = impl_->state.lifted_entry; }
  return entry >= 0 && impl_->Submit(static_cast<Entry>(entry), slot);
}
void Controller::Restore() { CancelMove(); }
bool Controller::Place(Entry e, Slot s) { return impl_->Submit(e, s); }
bool Controller::PlaceInRegion(Entry e, Region r) { return impl_->Submit(e, {}, r); }
bool Controller::Remove(Entry e) { return impl_->Submit(e, {}); }
bool Controller::Clear() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->stopping || impl_->state.busy || impl_->state.faulted ||
      impl_->restore_requested || !impl_->state.occupancy_known) return false;
  impl_->state.busy = true;
  impl_->state.status = "Clearing owned tokens...";
  impl_->pending = Impl::Command{Entry{}, {}, impl_->generation, {}, Impl::Kind::Clear};
  impl_->cv.notify_one(); return true;
}
void Controller::CancelClear() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->stopping) return;
  ++impl_->generation; impl_->cv.notify_all();
}
void Controller::CancelMove() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->stopping) return;
  ++impl_->generation; impl_->restore_requested = true; impl_->state.busy = true;
  impl_->cv.notify_all();
}
void Controller::Shutdown() {
  { std::lock_guard lock(impl_->mutex);
    if (!impl_->stopping) {
      impl_->stopping = true; ++impl_->generation;
      impl_->restore_requested = true; impl_->state.busy = true; impl_->cv.notify_all();
    }
  }
  if (impl_->worker.joinable()) impl_->worker.join();
}

#ifndef NATIVE_ROSTER_CONTROLLER_ONLY
namespace {
class RosterDialog final : public rex::ui::ImGuiDialog {
 public:
  RosterDialog(rex::ui::ImGuiDrawer* drawer, std::shared_ptr<Controller> controller, Host host,
               rex::ui::ImmediateDrawer* textures, TagCatalog tags)
      : ImGuiDialog(drawer), texture_drawer_(textures), portrait_tags_(std::move(tags)),
        portrait_budget_(portrait_tags_.size()),
        controller_(std::move(controller)), host_(std::move(host)) {
    portraits_.resize(portrait_tags_.size());
    browser_.Initialize(portrait_tags_);
    for (auto& tag : portrait_tags_) if (const auto* m = MetadataFor(tag)) tag.name = m->name;
  }
  ~RosterDialog() override {
    browser_.ShutdownFavorites();
    if (browser_.status.find("not saved") != std::string::npos) REXLOG_WARN("Native roster: {}", browser_.status);
    controller_->CancelMove();
    if (host_.set_input_blocked) host_.set_input_blocked(false);
  }
 protected:
  void OnDraw(ImGuiIO& io) override {
    browser_.ObserveFavorites();
    const uint16_t pad = host_.pad_buttons ? host_.pad_buttons() : 0;
    const uint16_t pressed = pad & ~previous_;
    previous_ = pad;
    const bool request = host_.take_open_request && host_.take_open_request();
    if (host_.pad_lights) lights_ = host_.pad_lights();
    if (host_.token_status) token_status_ = host_.token_status();
    if (state_ == State::Idle && !(host_.other_overlay_open && host_.other_overlay_open())) DrawPadHud(io);
    if (host_.on_frame) host_.on_frame(state_ != State::Idle);
    if (state_ == State::Idle) {
      if (request) {
        state_ = State::Open;
        model_ = PadModel{}; clear_confirm_.Reset();
        stick_.Reset();
        focus_window_ = true;
        closing_ = false;
        controller_->Refresh();
        // take_open_request may select a different controller owner.
        previous_ = host_.pad_buttons ? host_.pad_buttons() : 0;
        if (host_.set_input_blocked) host_.set_input_blocked(true);
      }
      return;
    }
    if (state_ == State::Releasing) {
      // The SDK owns the per-user controller neutral quarantine. Waiting on
      // cached pad state here could wedge forever after controller disconnect.
      if (!ImGui::IsKeyDown(ImGuiKey_Enter) && !ImGui::IsKeyDown(ImGuiKey_Space) &&
          !ImGui::IsKeyDown(ImGuiKey_Escape) && !ImGui::IsKeyDown(ImGuiKey_R) &&
          !ImGui::IsKeyDown(ImGuiKey_LeftArrow) && !ImGui::IsKeyDown(ImGuiKey_RightArrow) &&
          !ImGui::IsKeyDown(ImGuiKey_UpArrow) && !ImGui::IsKeyDown(ImGuiKey_DownArrow) &&
          !ImGui::IsMouseDown(0)) {
        state_ = State::Idle;
        if (host_.set_input_blocked) host_.set_input_blocked(false);
      }
      return;
    }
    if (host_.other_overlay_open && host_.other_overlay_open()) {
      if (!closing_) RequestClose();
      const auto current = controller_->Read();
      if (!current.busy && current.lifted_entry < 0) FinishClose();
      return;
    }
    using namespace rex::input;
    auto down = [&](uint16_t mask, ImGuiKey key) { return (pressed & mask) || ImGui::IsKeyPressed(key, false); };
    auto snapshot = controller_->Read();
    model_.entry_count = static_cast<int>(snapshot.slots.size());
    model_.Observe(snapshot);
    clear_confirm_.Tick(io.DeltaTime);
    if (snapshot.busy || snapshot.faulted || !snapshot.occupancy_known || model_.mode != PadModel::Mode::Browse) clear_confirm_.Reset();
    if (closing_) {
      if (!snapshot.busy && snapshot.lifted_entry < 0) { FinishClose(); return; }
      if (!snapshot.busy && snapshot.lifted_entry >= 0) closing_ = false; // Show restore failure.
      else return;
    }
    if (request) {
      RequestClose();
      return;
    }
    if (down(X_INPUT_GAMEPAD_B, ImGuiKey_Escape)) {
      if (clear_confirm_.armed()) { clear_confirm_.Reset(); return; }
      if (model_.mode == PadModel::Mode::Picker && browser_.filters_open) {
        browser_.filters_open = false; return;
      }
      const auto action = model_.Back();
      if (action == PadModel::Action::Close) {
        RequestClose();
      } else if (action == PadModel::Action::Cancel) {
        if (model_.operation == PadModel::Operation::Clear && snapshot.busy) controller_->CancelClear();
        else { controller_->Restore(); model_.Submitted(PadModel::Operation::Restore); }
      }
      return;
    }
    int dx = 0, dy = 0;
    if (down(X_INPUT_GAMEPAD_DPAD_LEFT, ImGuiKey_LeftArrow)) dx = -1;
    else if (down(X_INPUT_GAMEPAD_DPAD_RIGHT, ImGuiKey_RightArrow)) dx = 1;
    else if (down(X_INPUT_GAMEPAD_DPAD_UP, ImGuiKey_UpArrow)) dy = -1;
    else if (down(X_INPUT_GAMEPAD_DPAD_DOWN, ImGuiKey_DownArrow)) dy = 1;
    const auto axes = host_.pad_axes ? host_.pad_axes() : std::array<float, 2>{};
    const auto stick = stick_.Read(axes[0], axes[1], io.DeltaTime);
    if (!dx && !dy) { dx = stick[0]; dy = stick[1]; }
    const bool remove = down(X_INPUT_GAMEPAD_X, ImGuiKey_R);
    if (!snapshot.busy && model_.mode == PadModel::Mode::Picker) {
      if (down(X_INPUT_GAMEPAD_Y, ImGuiKey_Tab)) browser_.filters_open = !browser_.filters_open;
      if (browser_.filters_open) {
        if (dx) browser_.filter_focus = (browser_.filter_focus + (dx < 0 ? 4 : 1)) % 5;
        if (dy) { browser_.AdjustFilter(dy); browser_.Rebuild(portrait_tags_, model_.entry); }
      } else {
        browser_.Navigate(dy ? dy : dx);
        if (dx || dy) scroll_selected_ = true;
        if (down(X_INPUT_GAMEPAD_LEFT_SHOULDER, ImGuiKey_PageUp)) { browser_.Page(-1); scroll_selected_ = true; }
        if (down(X_INPUT_GAMEPAD_RIGHT_SHOULDER, ImGuiKey_PageDown)) { browser_.Page(1); scroll_selected_ = true; }
        if (remove && browser_.Selected() >= 0) {
          browser_.ToggleFavorite(portrait_tags_[browser_.Selected()]);
          browser_.Rebuild(portrait_tags_, model_.entry);
        }
      }
      model_.entry = browser_.Selected();
    } else if (!snapshot.busy) model_.Navigate(dx, dy);
    std::vector<int> desired_portraits;
    for (int slot = 0; slot < 7; ++slot) desired_portraits.push_back(PadModel::Owner(snapshot, slot));
    if (model_.mode == PadModel::Mode::Carrying) desired_portraits.push_back(model_.entry);
    if (model_.mode == PadModel::Mode::Picker) {
      const int start = (browser_.cursor / CatalogBrowser::kPageSize) * CatalogBrowser::kPageSize;
      for (int row = start; row < std::min(int(browser_.visible.size()), start + CatalogBrowser::kPageSize); ++row)
        desired_portraits.push_back(browser_.visible[row]);
    }
    PreparePortraits(desired_portraits);
    bool clear = model_.mode == PadModel::Mode::Browse && (pressed & X_INPUT_GAMEPAD_Y) != 0;
    bool activate = down(X_INPUT_GAMEPAD_A, ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Space, false);
    if (dx || dy || remove || activate) clear_confirm_.Reset();
    if (model_.mode == PadModel::Mode::Picker && browser_.filters_open) activate = false;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
    const float line = ImGui::GetTextLineHeightWithSpacing();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2), ImGuiCond_Always, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(std::max(1.f, std::min(io.DisplaySize.x - 32, line * 42)),
                                  std::max(1.f, std::min(io.DisplaySize.y - 32, line * 28))), ImGuiCond_Always);
    if (focus_window_) { ImGui::SetNextWindowFocus(); focus_window_ = false; }
    ImGui::Begin("Native Toy Pad", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    DrawLightsRow();
    ImGui::TextUnformatted("Left: 3 slots                 Center: 1                 Right: 3 slots");
    ImGui::BeginDisabled(snapshot.busy || snapshot.faulted || !snapshot.occupancy_known);
    // Optional local portraits keep the same spatial hit targets and named fallback.
    const float cell_width = std::max(1.f, (ImGui::GetContentRegionAvail().x - 4 * ImGui::GetStyle().ItemSpacing.x) / 5);
    constexpr int layout[2][5] = {{-1, 0, 3, 4, -1}, {1, 2, -1, 5, 6}};
    for (int row = 0; row < 2; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (col) ImGui::SameLine();
        const int slot = layout[row][col];
        if (slot < 0) { ImGui::Dummy(ImVec2(cell_width, line * 6)); continue; }
        const int owner = PadModel::Owner(snapshot, slot);
        std::string label = model_.focus == slot ? "> " : "";
        label += kSlotNames[slot]; label += "\n";
        const bool carrying_here = model_.mode == PadModel::Mode::Carrying && model_.focus == slot;
        label += owner >= 0 ? portrait_tags_[owner].name.c_str() : snapshot.occupancy_known ? snapshot.occupied[slot] ? "Occupied" : "Empty" : "Unknown";
        if (model_.mode == PadModel::Mode::Carrying && slot == model_.source) label += "\n(source)";
        label += "##slot" + std::to_string(slot);
        const bool ghost = model_.mode == PadModel::Mode::Carrying && slot == model_.source;
        if (ghost) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180,180,180,140));
        const bool has_portrait = owner >= 0 && portraits_[owner];
        const std::string button_label = has_portrait ? "##slot" + std::to_string(slot) : label;
        if (ImGui::Button(button_label.c_str(), ImVec2(cell_width, line * 6)) && model_.mode != PadModel::Mode::Picker) {
          model_.focus = slot; activate = true;
        }
        if (ghost) ImGui::PopStyleColor();
        if (has_portrait) {
          const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
          auto* draw = ImGui::GetWindowDrawList();
          draw->PushClipRect(min, max, true);
          const ImU32 tint = ImGui::GetColorU32(ImVec4(1, 1, 1, ghost ? .35f : 1.f));
          std::string title = model_.focus == slot ? "> " : "";
          title += kSlotNames[slot];
          draw->AddText(ImVec2(min.x + 4, min.y + 2), tint, title.c_str());
          DrawPortrait(owner, ImVec2(min.x + 5, min.y + line),
                       ImVec2(max.x - 5, max.y - line * (ghost ? 2.f : 1.f)), tint);
          draw->AddText(ImVec2(min.x + 4, max.y - line * (ghost ? 2.f : 1.f)), tint, portrait_tags_[owner].name.c_str());
          if (ghost) draw->AddText(ImVec2(min.x + 4, max.y - line), tint, "(source)");
          draw->PopClipRect();
        }
        if (carrying_here) {
          // Raised token follows focus; the original remains as a source ghost.
          const ImVec2 min = ImGui::GetItemRectMin();
          const ImVec2 max = ImGui::GetItemRectMax();
          auto* draw = ImGui::GetWindowDrawList();
          const ImVec2 a(min.x + 4, min.y - line * .35f);
          const bool has_carried_portrait = portraits_[model_.entry] != nullptr;
          const ImVec2 b(max.x - 4, min.y + line * (has_carried_portrait ? 4.f : .85f));
          draw->AddRectFilled(ImVec2(a.x + 3, a.y + 4), ImVec2(b.x + 3, b.y + 4), IM_COL32(0,0,0,160), 5);
          draw->AddRectFilled(a, b, IM_COL32(45,100,155,255), 5);
          draw->AddRect(a, b, IM_COL32(170,220,255,255), 5);
          draw->PushClipRect(a, b, true);
          if (has_carried_portrait)
            DrawPortrait(model_.entry, ImVec2(a.x + 4, a.y + 4),
                         ImVec2(b.x - 4, b.y - line), ImGui::GetColorU32(ImVec4(1,1,1,1)));
          draw->AddText(ImVec2(a.x + 4, has_carried_portrait ? b.y - line : a.y + 2),
                        ImGui::GetColorU32(ImVec4(1,1,1,1)), portrait_tags_[model_.entry].name.c_str());
          draw->PopClipRect();
        }
      }
    }
    if (model_.mode == PadModel::Mode::Picker) {
      ImGui::Separator();
      ImGui::TextUnformatted("Choose a character or vehicle");
      if (ImGui::Button(browser_.filters_open ? "Hide filters (Y / Tab)" : "Filters (Y / Tab)"))
        browser_.filters_open = !browser_.filters_open;
      if (browser_.filters_open && DrawCatalogFilters()) {
        browser_.Rebuild(portrait_tags_, model_.entry); model_.entry = browser_.Selected();
      }
      const int count = static_cast<int>(browser_.visible.size());
      if (!count) ImGui::TextUnformatted("No matching entries. Change the filters.");
      const int page_start = (browser_.cursor / CatalogBrowser::kPageSize) * CatalogBrowser::kPageSize;
      ImGui::Text("Page %d / %d (%d tokens)", count ? page_start / CatalogBrowser::kPageSize + 1 : 0,
                  (count + CatalogBrowser::kPageSize - 1) / CatalogBrowser::kPageSize, count);
      if (ImGui::Button("Previous page")) { browser_.Page(-1); model_.entry = browser_.Selected(); }
      ImGui::SameLine();
      if (ImGui::Button("Next page")) { browser_.Page(1); model_.entry = browser_.Selected(); }
      for (int row = page_start; row < std::min(count, page_start + CatalogBrowser::kPageSize); ++row) {
        const int i = browser_.visible[row];
        const float icon_size = line * 2;
        ImGui::PushID(i);
        if (portraits_[i]) {
          if (ImGui::Selectable("##portrait_choice", model_.entry == i, 0,
                                ImVec2(std::max(1.f, ImGui::GetContentRegionAvail().x - line * 5), icon_size))) { browser_.Select(i); model_.entry = i; activate = true; }
          const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
          auto* draw = ImGui::GetWindowDrawList();
          draw->PushClipRect(min, max, true);
          DrawPortrait(i, min, ImVec2(min.x + icon_size, min.y + icon_size),
                       ImGui::GetColorU32(ImVec4(1,1,1,1)));
          ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + icon_size + 8, min.y + line * .5f),
                                             ImGui::GetColorU32(ImGuiCol_Text), portrait_tags_[i].name.c_str());
          draw->PopClipRect();
        } else if (ImGui::Selectable(portrait_tags_[i].name.c_str(), model_.entry == i, 0,
                     ImVec2(std::max(1.f, ImGui::GetContentRegionAvail().x - line * 5), 0))) {
          browser_.Select(i); model_.entry = i; activate = true;
        }
        if (scroll_selected_ && model_.entry == i && !browser_.filters_open) { ImGui::SetScrollHereY(.5f); scroll_selected_ = false; }
        ImGui::SameLine();
        if (ImGui::SmallButton(browser_.IsFavorite(portrait_tags_[i]) ? "Unfavorite" : "Favorite")) {
          browser_.ToggleFavorite(portrait_tags_[i]);
          // Defer rebuild until next frame; do not invalidate visible indices in this loop.
          favorites_changed_ = true; activate = false;
        }
        ImGui::PopID();
      }
      if (favorites_changed_) {
        browser_.Rebuild(portrait_tags_, model_.entry); model_.entry = browser_.Selected(); favorites_changed_ = false;
      }
      ImGui::TextWrapped("Y: filters. X: favorite. LB/RB: pages. Abilities are manual filters; no game-puzzle detection.");
      if (!browser_.status.empty()) ImGui::TextWrapped("%s", browser_.status.c_str());
    }
    if (model_.mode == PadModel::Mode::Carrying)
      ImGui::Text("Carrying %s -> %s", portrait_tags_[model_.entry].name.c_str(), kSlotNames[model_.focus]);
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", snapshot.status.c_str());
    for (size_t i = 0; i < token_status_.size(); ++i) {
      const auto& token = token_status_[i];
      if (!token.reserved) continue;
      ImGui::Text("%s: model %u%s%s", kSlotNames[i], token.model,
          token.lifted ? " (lifted)" : "",
          token.save_failed ? " - SAVE FAILED: keep game open; remove to retry" : "");
    }
    if (!snapshot.occupancy_known && !snapshot.busy) ImGui::TextUnformatted("Pad occupancy unavailable. Close and reopen to retry.");
    ImGui::TextWrapped("LS / D-pad: navigate. A: select, lift or drop. B: cancel / back. X: remove focused token.");
    if (snapshot.busy) ImGui::TextUnformatted(closing_ ? "Finishing before closing..." :
      model_.mode == PadModel::Mode::Pending && model_.operation == PadModel::Operation::Clear ?
      "Clearing; B stops before the next token. Removed tokens stay removed." :
      model_.mode == PadModel::Mode::Pending && model_.operation == PadModel::Operation::Move ?
      "Moving; B requests cancellation before commit." : "Finishing update; this operation cannot be rolled back.");
    const bool have_owned = std::any_of(snapshot.slots.begin(), snapshot.slots.end(), [](int slot) { return slot >= 0; });
    ImGui::BeginDisabled(snapshot.busy || snapshot.faulted || !snapshot.occupancy_known || !have_owned || model_.mode != PadModel::Mode::Browse);
    if (ImGui::Button(clear_confirm_.armed() ? "Confirm clear pad (Y again)" : "Clear pad (Y)")) clear = true;
    ImGui::EndDisabled();
    if (clear_confirm_.armed()) ImGui::TextUnformatted("Press Y or click again to remove all owned tokens. B cancels. Tag files stay intact.");
    const bool back = ImGui::Button(model_.mode == PadModel::Mode::Browse ? "Close (B / Escape)" : "Back / Cancel (B / Escape)");
    ImGui::End();
    if (back) {
      if (clear_confirm_.armed()) { clear_confirm_.Reset(); return; }
      const auto action = model_.Back();
      if (action == PadModel::Action::Cancel) { if (model_.operation == PadModel::Operation::Clear && snapshot.busy) controller_->CancelClear();
        else { controller_->Restore(); model_.Submitted(PadModel::Operation::Restore); } }
      else if (action == PadModel::Action::Close) {
        RequestClose();
      }
      return;
    }
    if (clear && model_.mode == PadModel::Mode::Browse && !snapshot.busy && !snapshot.faulted && snapshot.occupancy_known && have_owned) {
      if (clear_confirm_.Press() && controller_->Clear()) model_.Submitted(PadModel::Operation::Clear);
      return;
    }
    if (activate || remove) clear_confirm_.Reset();
    if (remove && (model_.mode == PadModel::Mode::Browse || (model_.mode == PadModel::Mode::Carrying && snapshot.lifted_entry >= 0)) && !snapshot.busy && !snapshot.faulted && snapshot.occupancy_known) {
      const int owner = snapshot.lifted_entry >= 0 ? snapshot.lifted_entry : PadModel::Owner(snapshot, model_.focus);
      if (owner >= 0 && controller_->Remove(static_cast<Entry>(owner))) {
        model_.entry = owner;
        model_.Submitted(PadModel::Operation::Remove);
      }
      return;
    }
    if (activate && !snapshot.busy && !snapshot.faulted && snapshot.occupancy_known) {
      const auto action = model_.Activate(snapshot);
      if (action == PadModel::Action::Lift) {
        if (controller_->Lift(static_cast<Entry>(model_.entry))) model_.Submitted(PadModel::Operation::Lift);
      } else if (action == PadModel::Action::Cancel) {
        controller_->Restore(); model_.Submitted(PadModel::Operation::Restore);
      } else if (action == PadModel::Action::Place) {
        if (controller_->Place(static_cast<Entry>(model_.entry), static_cast<Slot>(model_.focus))) model_.Submitted(model_.source >= 0 ? PadModel::Operation::Move : PadModel::Operation::Add);
      }
    }
  }
 private:
  CatalogBrowser browser_;
  bool favorites_changed_ = false;
  bool scroll_selected_ = false;
  bool DrawCatalogFilters() {
    bool changed = false;
    auto combo = [&](int focus, const char* name, int& value, const auto& options, const char* all) {
      std::string label = browser_.filter_focus == focus ? "> " : ""; label += name;
      const char* current = value ? options[value - 1] : all;
      if (ImGui::BeginCombo(label.c_str(), current)) {
        if (ImGui::Selectable(all, value == 0)) { value = 0; changed = true; }
        for (size_t i = 0; i < options.size(); ++i)
          if (ImGui::Selectable(options[i], value == static_cast<int>(i + 1))) { value = static_cast<int>(i + 1); changed = true; }
        ImGui::EndCombo();
      }
    };
    combo(0, "Franchise", browser_.franchise, metadata::kFranchises, "All franchises");
    constexpr std::array<const char*,2> years{"Year 1 franchises", "Year 2 franchises"};
    combo(1, "Franchise year", browser_.year, years, "All years");
    constexpr std::array<const char*,2> types{"Characters", "Vehicles / gadgets"};
    combo(2, "Type", browser_.type, types, "All types");
    combo(3, "Manual ability", browser_.ability, metadata::kAbilities, "All abilities");
    changed |= ImGui::Checkbox(browser_.filter_focus == 4 ? "> Favorites only" : "Favorites only", &browser_.favorites_only);
    ImGui::TextUnformatted("Left/right: choose filter. Up/down: change. Y/B: return to tokens.");
    return changed;
  }
  std::array<std::array<uint8_t, 3>, 3> lights_{};
  std::array<TokenStatus, 7> token_status_{};
  void DrawLightsRow() {
    constexpr int order[] = {1, 0, 2};
    constexpr const char* names[] = {"Left", "Center", "Right"};
    for (int i = 0; i < 3; ++i) {
      if (i) ImGui::SameLine();
      const auto& c = lights_[order[i]];
      ImGui::ColorButton(names[i], ImVec4(c[0]/255.f,c[1]/255.f,c[2]/255.f,1),
          ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
          ImVec2(ImGui::GetTextLineHeight()*2, ImGui::GetTextLineHeight()));
      ImGui::SameLine(); ImGui::TextUnformatted(names[i]);
    }
  }
  void DrawPadHud(ImGuiIO& io) {
    const float line = ImGui::GetTextLineHeightWithSpacing();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x/2, io.DisplaySize.y-12), ImGuiCond_Always, ImVec2(.5f,1));
    ImGui::SetNextWindowBgAlpha(.7f);
    ImGui::Begin("Toy Pad lights##hud", nullptr, ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    DrawLightsRow();
    for (const auto& token : token_status_) if (token.reserved && token.save_failed) {
      ImGui::TextUnformatted("Toy Pad save failed. Open pad to retry; keep game open."); break;
    }
    const auto snapshot = controller_->Read();
    constexpr int order[] = {0,1,2,3,4,5,6};
    for (int i : order) {
      if (i) ImGui::SameLine();
      const int owner = PadModel::Owner(snapshot, i);
      const auto pos = ImGui::GetCursorScreenPos();
      const int region = i < 3 ? 1 : i == 3 ? 0 : 2;
      const auto& c = lights_[region];
      auto* draw = ImGui::GetWindowDrawList();
      draw->AddCircleFilled(ImVec2(pos.x+line*.35f,pos.y+line*.35f),line*.3f,
          IM_COL32(c[0],c[1],c[2],255));
      draw->AddCircle(ImVec2(pos.x+line*.35f,pos.y+line*.35f),line*.3f,
          owner >= 0 ? IM_COL32(255,255,255,255) : IM_COL32(100,100,100,255));
      ImGui::Dummy(ImVec2(line*.7f,line*.7f));
    }
    ImGui::SameLine(); ImGui::TextUnformatted("LB + RB");
    ImGui::End();
  }
  void PreparePortraits(const std::vector<int>& desired) {
    portrait_budget_.BeginFrame(desired);
    if (!texture_drawer_) return;
    const int candidate = portrait_budget_.Next(desired);
    if (candidate < 0) return;
    const size_t i = static_cast<size_t>(candidate);
    try {
      if (portrait_tags_[i].portrait < 0) { portrait_budget_.Fail(i); return; }
      portrait_file::Dimensions header;
      // Hash exactly the bounded buffer that will be passed to the decoder.
      auto bytes = portrait_file::ReadTrusted(portrait_file::Sibling(portrait_tags_[i].path),
          static_cast<size_t>(portrait_tags_[i].portrait), header);
      if (bytes.empty()) { portrait_budget_.Fail(i); return; }
      const size_t allocation = size_t(header.width) * header.height * 4;
      if (!portrait_budget_.Reserve(allocation, [&](size_t victim) { portraits_[victim].reset(); })) {
        // Temporary budget pressure keeps labels. Round-robin attempts let
        // smaller desired images load and retry this one after the page changes.
        return;
      }
      int width = 0, height = 0;
      auto rgba = rex::ui::DecodeImageRGBA(bytes.data(), bytes.size(), width, height);
      if (width != static_cast<int>(header.width) || height != static_cast<int>(header.height) ||
          rgba.size() != allocation) { portrait_budget_.Fail(i); return; }
      auto texture = texture_drawer_->CreateTexture(header.width, header.height,
          rex::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
      if (!texture) { portrait_budget_.Fail(i); return; }
      portraits_[i] = std::move(texture);
      portrait_budget_.Commit(i, allocation);
    } catch (const std::exception&) {
      portrait_budget_.Fail(i); // Optional art always retains named fallback.
    }
    // Exactly one file/hash/decode/upload attempt per frame; no eager catalog loop.
  }
  void DrawPortrait(int entry, ImVec2 min, ImVec2 max, ImU32 tint) {
    auto* texture = portraits_[entry].get();
    if (!texture || !texture->width || !texture->height || max.x <= min.x || max.y <= min.y) return;
    const float scale = std::min((max.x - min.x) / texture->width,
                                 (max.y - min.y) / texture->height);
    const ImVec2 size(texture->width * scale, texture->height * scale);
    const ImVec2 a(min.x + (max.x - min.x - size.x) * .5f,
                   min.y + (max.y - min.y - size.y) * .5f);
    ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(texture), a,
        ImVec2(a.x + size.x, a.y + size.y), ImVec2(0,0), ImVec2(1,1), tint);
  }
  // UI-thread ownership only. App OnShutdown resets this dialog before drawers.
  rex::ui::ImmediateDrawer* texture_drawer_ = nullptr;
  TagCatalog portrait_tags_;
  std::vector<std::unique_ptr<rex::ui::ImmediateTexture>> portraits_;
  PortraitCacheBudget portrait_budget_;
  void RequestClose() { clear_confirm_.Reset(); controller_->Restore(); closing_ = true; }
  void FinishClose() { closing_ = false; state_ = State::Releasing; }
  enum class State { Idle, Open, Releasing };
  State state_ = State::Idle;
  std::shared_ptr<Controller> controller_;
  Host host_;
  uint16_t previous_ = 0;
  PadModel model_;
  ClearConfirmation clear_confirm_;
  StickNavigation stick_;
  bool focus_window_ = false;
  bool closing_ = false;
};
}
std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(
    rex::ui::ImGuiDrawer* drawer, std::shared_ptr<Controller> controller, Host host,
    rex::ui::ImmediateDrawer* textures, TagCatalog tags) {
  return std::make_unique<RosterDialog>(drawer, std::move(controller), std::move(host),
                                      textures, std::move(tags));
}
#endif
} // namespace legodimensions::native_roster
