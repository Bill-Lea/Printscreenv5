#include "PCH.h"
#include "UIController.h"
#include "ConsoleCommandQueue.h"

// ============================================================
// Scaleform helpers (file-scope)
// Retained for the granular per-element API.  These all require
// game-thread execution — call via SKSE::GetTaskInterface().
// ============================================================
namespace {

static bool GFxSetBool(RE::GFxMovieView* v, const char* path, bool val) {
    if (!v) return false;
    RE::GFxValue gv; gv.SetBoolean(val);
    return v->SetVariable(path, gv);
}

static bool GFxSetNum(RE::GFxMovieView* v, const char* path, double val) {
    if (!v) return false;
    RE::GFxValue gv; gv.SetNumber(val);
    return v->SetVariable(path, gv);
}

static bool GFxGetBool(RE::GFxMovieView* v, const char* path, bool def = true) {
    if (!v) return def;
    RE::GFxValue gv;
    if (v->GetVariable(&gv, path) && gv.IsBool()) return gv.GetBool();
    return def;
}

static double GFxGetNum(RE::GFxMovieView* v, const char* path, double def = 100.0) {
    if (!v) return def;
    RE::GFxValue gv;
    if (v->GetVariable(&gv, path) && gv.IsNumber()) return gv.GetNumber();
    return def;
}

static RE::GFxMovieView* GetHUDView() {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return nullptr;
    auto menu = ui->GetMenu(RE::HUDMenu::MENU_NAME);
    if (!menu) return nullptr;
    auto* hud = static_cast<RE::HUDMenu*>(menu.get());
    return (hud && hud->uiMovie) ? hud->uiMovie.get() : nullptr;
}

} // namespace

// ============================================================
// Singleton
// ============================================================
UIController& UIController::GetSingleton() {
    static UIController instance;
    return instance;
}

// ============================================================
// HideAll / RestoreAll — showMenus approach
// ============================================================
bool UIController::HideAll() {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        logger::error("UIController::HideAll: no UI singleton");
        return false;
    }

    // Atomically claim the hidden state. This is callable from the worker
    // thread, the game thread, and the menu-event sink concurrently — the
    // old load-then-store pattern let two overlapping calls both pass the
    // "already hidden" check and clobber savedShowMenus_.
    if (hidden_.exchange(true)) {
        logger::info("UIController::HideAll: already hidden");
        return true;
    }

    // Save current state (another mod may have already toggled it)
    savedShowMenus_.store(ui->IsShowingMenus());

    // Toggle off — this is what the 'tm' console command does.
    // It's a simple bool checked at the top of the engine's UI
    // render pass, so the entire HUD/menu layer is skipped.
    ui->ShowMenus(false);

    logger::info("UIController::HideAll: showMenus set to false (was {})",
                 savedShowMenus_.load());
    return true;
}

bool UIController::RestoreAll() {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        logger::error("UIController::RestoreAll: no UI singleton");
        return false;
    }

    // Atomically release the hidden state (see HideAll).
    if (!hidden_.exchange(false)) {
        logger::info("UIController::RestoreAll: already visible");
        return true;
    }

    // Restore the value we saved — almost always true, but
    // handles the edge case where a mod had it off already.
    ui->ShowMenus(savedShowMenus_.load());

    logger::info("UIController::RestoreAll: showMenus restored to {}",
                 savedShowMenus_.load());
    return true;
}

// ============================================================
// Async wrappers
// showMenus is a plain bool so these are safe from any thread.
// No SKSE::AddTask marshalling needed, but we keep the API
// shape so call-sites don't have to change.
// ============================================================
void UIController::HideAllAsync()   { HideAll(); }
void UIController::RestoreAllAsync() { RestoreAll(); }

bool UIController::AreHidden() const {
    return hidden_.load();
}

// ============================================================
// Granular Scaleform helpers
// These still manipulate GFx movie views so they MUST run on
// the game thread.  They work independently of showMenus —
// if showMenus is false the engine won't render anything
// regardless of these values.
// ============================================================
bool UIController::SetHUDAlpha(float alpha) {
    alpha = std::clamp(alpha, 0.0f, 100.0f);
    auto* v = GetHUDView(); if (!v) return false;
    return GFxSetNum(v, "_root._alpha", static_cast<double>(alpha));
}

float UIController::GetHUDAlpha() const {
    auto* v = GetHUDView(); if (!v) return -1.0f;
    return static_cast<float>(GFxGetNum(v, "_root._alpha"));
}

bool UIController::HideHUD()     { return SetHUDAlpha(0.0f); }
bool UIController::ShowHUD()     { return SetHUDAlpha(100.0f); }
bool UIController::IsHUDVisible() const { float a = GetHUDAlpha(); return (a < 0.0f) || (a > 0.0f); }

// ============================================================
// Individual element control (game-thread only)
// ============================================================
bool UIController::SetSubtitlesVisible(bool v) {
    auto* view = GetHUDView(); if (!view) return false;
    return GFxSetBool(view, "_root.HUDMovieBaseInstance.SubtitleTextHolder._visible", v);
}
bool UIController::SetNPCNamesVisible(bool v) {
    auto* view = GetHUDView(); if (!view) return false;
    return GFxSetBool(view, "_root.HUDMovieBaseInstance.ActorInfoHolder._visible", v);
}
bool UIController::SetEnemyHealthVisible(bool v) {
    auto* view = GetHUDView(); if (!view) return false;
    return GFxSetBool(view, "_root.HUDMovieBaseInstance.EnemyHealthHolder._visible", v);
}
bool UIController::SetCompassVisible(bool v) {
    auto* view = GetHUDView(); if (!view) return false;
    return GFxSetBool(view, "_root.HUDMovieBaseInstance.CompassShoutMeterHolder._visible", v);
}
bool UIController::SetCrosshairVisible(bool v) {
    auto* view = GetHUDView(); if (!view) return false;
    return GFxSetBool(view, "_root.HUDMovieBaseInstance.CrosshairInstance._visible", v);
}
bool UIController::SetQuestMarkersVisible(bool v) {
    auto* view = GetHUDView(); if (!view) return false;
    auto* ui = RE::UI::GetSingleton(); if (!ui) return false;
    GFxSetBool(view, "_root.HUDMovieBaseInstance.QuestUpdateBaseInstance._visible", v);
    auto fqm = ui->GetMenu("FloatingQuestMarkers Menu");
    if (fqm) {
        auto* im = static_cast<RE::IMenu*>(fqm.get());
        if (im && im->uiMovie) GFxSetNum(im->uiMovie.get(), "_root._alpha", v ? 100.0 : 0.0);
    }
    return true;
}
bool UIController::SetPlayerStatsVisible(bool v) {
    auto* view = GetHUDView(); if (!view) return false;
    return GFxSetBool(view, "_root.HUDMovieBaseInstance.HealthMagickaStaminaHolder._visible", v);
}

// ============================================================
// Console command execution (delegated to queue)
// ============================================================
bool UIController::ExecuteConsoleAsync(std::string command) {
    ConsoleCommandQueue::GetSingleton().EnqueueAndDrainAsync(std::move(command));
    return true;
}
