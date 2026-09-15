#include "PCH.h"
#include "MenuEventSink.h"
#include "UIController.h"

// ============================================================
// Ignored menu list
// ============================================================
const std::set<std::string> MenuEventSink::kIgnoredMenus = {
    "Console", "Console Native UI Menu", "TweenMenu", "Fader Menu",
    "LoadWaitSpinner", "Cursor Menu", "HUD Menu", "Main Menu",
    "Loading Menu", "Mist Menu", "Overlay Menu",
    "Overlay Interaction Menu", "Debug Text Menu", "Safety Zone Menu"
};

// Menus that indicate the game is paused or text input is active
const std::set<std::string> MenuEventSink::kPauseOrInputMenus = {
    "InventoryMenu", "MagicMenu", "Journal Menu", "MapMenu",
    "FavoritesMenu", "Book Menu", "BarterMenu", "ContainerMenu",
    "Dialogue Menu", "GiftMenu", "Lockpicking Menu", "Sleep/Wait Menu",
    "StatsMenu", "Training Menu", "Tutorial Menu", "Console",
    "Console Native UI Menu"
};

// ============================================================
// Singleton
// ============================================================
MenuEventSink* MenuEventSink::GetSingleton() {
    static MenuEventSink instance;
    return &instance;
}

// ============================================================
// Register / Unregister
// ============================================================
void MenuEventSink::Register() {
    if (registered_.load()) return;
    if (auto* ui = RE::UI::GetSingleton()) {
        ui->AddEventSink(this);
        registered_.store(true);
        logger::info("MenuEventSink: registered");
    } else {
        logger::error("MenuEventSink::Register: no UI singleton");
    }
}

void MenuEventSink::Unregister() {
    if (!registered_.load()) return;
    if (auto* ui = RE::UI::GetSingleton()) {
        ui->RemoveEventSink(this);
        registered_.store(false);
        logger::info("MenuEventSink: unregistered");
    }
}

// ============================================================
// Token management
// ============================================================
void MenuEventSink::SetCaptureToken(CancellationToken::WeakPtr token) {
    std::lock_guard lock(tokenMutex_);
    captureToken_ = std::move(token);
}

void MenuEventSink::ClearCaptureToken() {
    std::lock_guard lock(tokenMutex_);
    captureToken_.reset();
}

// ============================================================
// ShouldIgnoreMenu
// ============================================================
bool MenuEventSink::ShouldIgnoreMenu(const RE::BSFixedString& name) const {
    std::string n = name.c_str();
    if (kIgnoredMenus.count(n)) return true;
    if (n.find("Cursor")  != std::string::npos) return true;
    if (n.find("Fader")   != std::string::npos) return true;
    if (n.find("Debug")   != std::string::npos) return true;
    if (n.find("Tween")   != std::string::npos) return true;
    return false;
}

// ============================================================
// IsPauseOrInputMenu
// ============================================================
bool MenuEventSink::IsPauseOrInputMenu(const RE::BSFixedString& name) const {
    std::string n = name.c_str();
    if (kPauseOrInputMenus.count(n)) return true;
    // Also check menu flags for pause/input
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return false;
    auto menu = ui->GetMenu(name);
    if (!menu) return false;
    auto* im = static_cast<RE::IMenu*>(menu.get());
    if (!im) return false;
    if (im->menuFlags.any(RE::UI_MENU_FLAGS::kPausesGame)) return true;
    // NOTE: the old kRequiresUpdate check was removed — that flag is set on
    // many ordinary menus (it just means the menu wants AdvanceMovie calls),
    // so it caused nearly every menu to be classified as "pause/input".
    return false;
}

// ============================================================
// ProcessEvent
// ============================================================
RE::BSEventNotifyControl MenuEventSink::ProcessEvent(
    const RE::MenuOpenCloseEvent* event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
    if (!event)
        return RE::BSEventNotifyControl::kContinue;

    // Maintain the cached pause state for BOTH open and close events.
    // The old code returned early on every non-opening event, which made the
    // "else { isPaused_ = false; }" branch unreachable — once a pause menu
    // opened, IsGamePaused() stayed true forever, even after the menu closed.
    const bool pauseMenu = IsPauseOrInputMenu(event->menuName);
    if (pauseMenu) {
        isPaused_.store(event->opening);
    }

    if (!event->opening)
        return RE::BSEventNotifyControl::kContinue;

    if (!menusHidden_.load())
        return RE::BSEventNotifyControl::kContinue;

    if (ShouldIgnoreMenu(event->menuName))
        return RE::BSEventNotifyControl::kContinue;

    // Look up the active capture via the weak token
    CancellationToken::Ptr tok;
    {
        std::lock_guard lock(tokenMutex_);
        tok = captureToken_.lock();
    }

    if (!tok) {
        // No active capture — restore UI to be safe (direct Scaleform, game thread)
        logger::warn("MenuEventSink: menu '{}' opened with no active token, restoring UI",
                     event->menuName.c_str());
        UIController::GetSingleton().RestoreAllAsync();
        return RE::BSEventNotifyControl::kContinue;
    }

    // Only a menu that pauses the game or takes input cancels the capture.
    // Those are the menus the player deliberately opened (inventory, map,
    // journal, dialogue, a container...). Anything else that opens on its
    // own while the HUD is hidden — QuickLoot's LootMenu whenever the
    // crosshair crosses a container, HUD overlay mods and the like — is not
    // in the capture anyway (showMenus is off) and does not interrupt
    // gameplay, so it must not interrupt a recording either. Before this
    // check every such popup silently cut a video short, so the success
    // notification and the shot counter never fired.
    if (!pauseMenu) {
        logger::info("MenuEventSink: menu '{}' opened during capture — non-pausing, capture continues",
                     event->menuName.c_str());
        return RE::BSEventNotifyControl::kContinue;
    }

    logger::warn("MenuEventSink: menu '{}' opened during capture — cancelling and restoring UI",
                 event->menuName.c_str());
    tok->Cancel();
    UIController::GetSingleton().RestoreAllAsync();

    return RE::BSEventNotifyControl::kContinue;
}
