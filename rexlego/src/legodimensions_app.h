// legodimensions - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

#include <rex/logging.h>
#include <rex/input/input_system.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/cvar.h>
#include <rex/ui/flags.h>
#include <rex/ui/keybinds.h>

#include "cheat_menu.h"
#include "discord_presence.h"
#ifdef LEGODIMENSIONS_DEV_PROBES
#include "area_watch.h"
#endif
#ifdef LEGODIMENSIONS_DEV_PROBES
#include "hub_portrait.h"
#endif
#include "main_menu_extras.h"
#include "native_roster_backend.h"
#include <rex/input/input.h>
#include <rex/input/portal/emulated_toypad.h>
#include "mod_menu.h"
#include "skip_cutscenes.h"
#include "toypad_app.h"
#include "ui_theme.h"
#include "updates.h"

class LegodimensionsApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<LegodimensionsApp>(new LegodimensionsApp(ctx, "legodimensions",
        PPCImageConfig));
  }

  // Publish Discord Rich Presence for as long as the game is up. Started here
  // rather than earlier so a Discord that is not running yet costs nothing:
  // the connection lives on its own thread and simply keeps retrying.
  // The update check goes out here too: by this point the window is up, so a
  // slow network cannot be mistaken for a slow launch. It costs nothing when
  // updates_check is off, which is the point of the setting.
  void OnPostSetup() override {
    if (native_mode_ && runtime() && runtime()->input_system()) {
      auto* portal = runtime()->input_system()->GetPortal();
      roster_portal_.store(portal ? portal->AsEmulatedToypad() : nullptr);
      rex::input::InputSystem::ConfigureNativeRosterChord(
          rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER, rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
    }
#ifdef LEGODIMENSIONS_DEV_PROBES
    // Guest functions are registered by now, which is what the patch needs.
    legodimensions::hub_portrait::Install();
    // Must come before the presence starts: the presence asks it where the
    // player is, and it can only answer once the accessor is wrapped.
    legodimensions::area_watch::Install();
#endif
    legodimensions::cheats::InstallGraphicsToggles();
    legodimensions::skip_cutscenes::Start();
    legodimensions::discord::Start();
    // Same way out as Quit Game in the main menu.
    if (!native_mode_) legodimensions::updates::CheckAtStartup([this] {
      StopRoster();
      legodimensions::toypad_app::StopIfStarted();
      rex::FlushLogging();
      std::_Exit(0);
    });
    if (!native_mode_) legodimensions::toypad_app::StartIfEnabled();
  }

  // The window closing is where the app must be let go: ReXApp::OnClosing then
  // hard-exits with std::_Exit, so OnShutdown never runs. This asks nicely; the
  // job object is the backstop if the game dies without getting here.
  // The watchdog covers the gap before ReXApp::OnClosing's hard-exit. That
  // hard-exit exists precisely because subsystem teardown can deadlock, but the
  // window layer between "close requested" and OnClosing can wedge too, and
  // then the process never dies and the player is left killing it by hand.
  // Give it three seconds and take the same way out.
  bool OnWindowCloseRequested() override {
    legodimensions::toypad_app::StopIfStarted();
    StartShutdownWatchdog();
    StopRoster();
    return true;
  }

  // Dropping the pipe is what clears the presence; Discord does the rest.
  void OnShutdown() override {
    StopRoster();
    // OnShutdown runs after the UI loop, before the SDK destroys the drawer.
    roster_menu_.reset();
    legodimensions::discord::Stop();
    legodimensions::toypad_app::StopIfStarted();
    // Drops the freeze thread before the guest memory it writes into goes away.
    legodimensions::cheats::Shutdown();
  }

  // The SDK inherits xenia's green ImGui theme; both hooks exist so a game can
  // restyle the overlays without forking the shared UI code.
  void OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) override {
    legodimensions::ui::ConfigureStyle(imgui_style, ui_style);
    legodimensions::ui::ConfigureSettings();
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    legodimensions::ui::ConfigureFonts(atlas);
  }

  // Runs before the runtime is built, which is the only point where the update
  // folder can still be swapped for the modded copy.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    legodimensions::updates::RelocateMovedInstall(paths);
    legodimensions::mods::ResolveUpdateRoot(paths);
  }

  // F8 mirrors how the SDK toggles its own overlays: the bind owns the dialog
  // and destroying it closes the menu.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    rex::ui::RegisterBind("bind_mods", "F8", "Toggle mod menu", [this, drawer] {
      if (roster_modal_) return;
      if (mod_menu_) {
        mod_menu_.reset();
      } else {
        mod_menu_ = legodimensions::mods::CreateMenu(drawer);
      }
    });
    // DEL opens the cheat menu. Only the dialog is thrown away on close - the
    // scanner and any frozen values live on in the engine behind it.
    rex::ui::RegisterBind("bind_cheats", "Delete", "Toggle cheat menu", [this, drawer] {
      if (roster_modal_) return;
      if (cheat_menu_) {
        cheat_menu_.reset();
      } else {
        cheat_menu_ = legodimensions::cheats::CreateMenu(drawer);
      }
    });
    // Credits and Quit Game in the main menu. Always up; it draws only when
    // one of the two entries is picked.
    legodimensions::main_menu::Host host;
    // The game keeps running under the prompt; only its input is withheld.
    // Suspending its threads crashed 0.1.21 for players: a thread frozen
    // inside the input system's own device refresh, and this overlay polling
    // the same unlocked state from the UI thread.
    host.set_paused = [](bool paused) { rex::input::InputSystem::SetGuestInputBlocked(paused); };
    host.pad_buttons = []() -> uint16_t { return rex::input::InputSystem::HeldButtons(); };
    host.quit = [this] {
      StopRoster();
      legodimensions::toypad_app::StopIfStarted();
      rex::FlushLogging();
      std::_Exit(0);
    };
    // The cursor is only for the overlays: hidden over the game in a window
    // and in fullscreen alike, shown while any menu is open.
    host.on_frame = [this](bool prompt_open) {
      main_prompt_open_ = prompt_open;
      auto* win = window();
      if (!win) {
        return;
      }
      const bool menu_open = prompt_open || roster_modal_ || AnyOverlayOpen() || mod_menu_ || cheat_menu_;
      win->SetCursorVisibility(menu_open ? rex::ui::Window::CursorVisibility::kVisible
                                         : rex::ui::Window::CursorVisibility::kHidden);
    };
    main_menu_ = legodimensions::main_menu::CreateOverlay(drawer, std::move(host));
    if (native_mode_) {
      namespace nr = legodimensions::native_roster;
      const auto roster_catalog = nr::ConfiguredTags();
      roster_controller_ = std::make_shared<nr::Controller>(
          nr::MakeBackend([this] { return roster_portal_.load(); }, roster_catalog), roster_catalog);
      nr::Host roster_host;
      roster_host.token_status = [this, cached = std::array<rex::input::EmulatedToypad::FigureStatus, 7>{}]() mutable {
        if (auto* portal = roster_portal_.load()) portal->TryGetFigureStatus(cached);
        constexpr size_t indices[] = {0,3,4,1,2,5,6};
        std::array<nr::TokenStatus, 7> out{};
        for (size_t i = 0; i < out.size(); ++i) {
          const auto& f = cached[indices[i]];
          out[i] = {f.model, f.reserved, f.lifted, f.save_failed};
        }
        return out;
      };
      roster_host.pad_lights = [this, cached = std::array<rex::input::ToypadRgb, 3>{}]() mutable {
        if (auto* portal = roster_portal_.load()) portal->TryGetPadLightColors(cached);
        return cached;
      };
      roster_host.take_open_request = [this] {
        uint64_t ticket = 0;
        const int user = rex::input::InputSystem::ConsumeNativeRosterOpenRequest(&ticket);
        if (user < 0) return false;
        if (!roster_portal_.load() || main_prompt_open_ || AnyOverlayOpen() || mod_menu_ || cheat_menu_) {
          rex::input::InputSystem::ReleaseNativeRosterInput(ticket);
          return false;
        }
        roster_owner_ = static_cast<uint32_t>(user);
        roster_ticket_ = ticket;
        roster_modal_ = true;
        return true;
      };
      roster_host.pad_buttons = [this] { return rex::input::InputSystem::HeldButtons(roster_owner_); };
      roster_host.pad_axes = [this] {
        const uint32_t packed = rex::input::InputSystem::HeldLeftStick(roster_owner_);
        return std::array<float, 2>{int16_t(packed & 0xffff) / 32768.f,
                                    int16_t(packed >> 16) / 32768.f};
      };
      roster_host.set_input_blocked = [this](bool blocked) {
        // Opening already acquired its separate SDK owner before guest delivery.
        if (!blocked) {
          rex::input::InputSystem::ReleaseNativeRosterInput(roster_ticket_);
          roster_modal_ = false;
        }
      };
      roster_host.on_frame = [this](bool open) { roster_modal_ = open; };
      roster_host.other_overlay_open = [this] {
        return main_prompt_open_ || AnyOverlayOpen() || mod_menu_ || cheat_menu_;
      };
      roster_menu_ = nr::CreateOverlay(drawer, roster_controller_, std::move(roster_host),
                                       immediate_drawer(), roster_catalog);
      rex::ui::RegisterBind("bind_roster", "F6", "Open character roster", [this] {
        if (roster_portal_.load() && !roster_modal_ && !main_prompt_open_ &&
            !AnyOverlayOpen() && !mod_menu_ && !cheat_menu_) {
          rex::input::InputSystem::RequestNativeRosterOpen(0);
        }
      });
    }
  }

 private:
  void StopRoster() {
    REXLOG_INFO("Native roster shutdown: disabling shortcut");
    rex::input::InputSystem::ConfigureNativeRosterChord(0, 0);
    REXLOG_INFO("Native roster shutdown: joining controller worker");
    if (roster_controller_) roster_controller_->Shutdown();
    REXLOG_INFO("Native roster shutdown: releasing input");
    // Do not destroy a registered dialog from another dialog's draw callback.
    // Normal app teardown owns its destruction; hard-exit paths need none.
    roster_portal_.store(nullptr);
    rex::input::InputSystem::ReleaseNativeRosterInput(roster_ticket_);
    roster_modal_ = false;
    REXLOG_INFO("Native roster shutdown: complete");
  }
  bool native_mode_ = false;
  bool roster_modal_ = false;
  bool main_prompt_open_ = false;
  uint32_t roster_owner_ = 0;
  uint64_t roster_ticket_ = 0;
  std::atomic<rex::input::EmulatedToypad*> roster_portal_{nullptr};
  std::shared_ptr<legodimensions::native_roster::Controller> roster_controller_;
  std::unique_ptr<rex::ui::ImGuiDialog> roster_menu_;

  // Last-resort exit if the close path wedges. Detached on purpose: if the
  // process gets where it is going first, _Exit takes this thread with it.
  static void StartShutdownWatchdog() {
    static std::once_flag once;
    std::call_once(once, [] {
      std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        REXLOG_WARN("Shutdown wedged past the close request; hard-exiting.");
        rex::FlushLogging();
        std::_Exit(0);
      }).detach();
    });
  }

  std::unique_ptr<rex::ui::ImGuiDialog> mod_menu_;
  std::unique_ptr<rex::ui::ImGuiDialog> cheat_menu_;
  std::unique_ptr<rex::ui::ImGuiDialog> main_menu_;

 public:
  // Override virtual hooks for customization:
  // FSR 2/3 run on synthesized depth and motion and smear busy scenes (the
  // Endless Sea of Possibilities report); people picked them and filed bugs.
  // Hidden from F4, and an install that already has one gets FSR 1 instead.
  // Here because the config is loaded but the presenter is not built yet.
  void OnPostInitLogging() override {
    native_mode_ = legodimensions::native_roster::ConfigureMode();
    const std::string effect = REXCVAR_GET(present_effect);
    if (effect == "fsr2" || effect == "fsr3") {
      rex::cvar::SetFlagByName("present_effect", "fsr");
      REXLOG_INFO("present_effect={} is no longer offered; using fsr", effect);
    }
    // Batched memexport readback put stale GPU data back over what the game
    // wrote since (exploded geometry on Endless Sea of Possibilities). Locked
    // off, whatever an older install wrote into the config.
    rex::cvar::SetFlagByName("readback_memexport_batched", "false");
    // On-demand readback (0.1.26) tore the Simpsons world geometry; it stays
    // off by default (SDK default + installer), but is no longer locked here so
    // the pre-draw sync fix (2026-10-04) can be tested from the config.
  }
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
