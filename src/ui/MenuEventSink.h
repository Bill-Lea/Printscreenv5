#pragma once
#include "capture/CancellationToken.h"
#include <atomic>
#include <mutex>   // tokenMutex_ — was relying on the PCH pulling this in
#include <set>
#include <string>

// ============================================================
// MenuEventSink
// Listens for MenuOpenCloseEvent and cancels an active capture
// when the player opens a game menu (inventory, map, etc.).
// Holds a *weak* reference to the capture token so expired
// sessions are silently ignored.
// ============================================================
class MenuEventSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    static MenuEventSink* GetSingleton();

    void Register();
    void Unregister();
    bool IsRegistered() const { return registered_.load(); }

    // Called by CaptureSession when a capture begins/ends.
    void SetCaptureToken(CancellationToken::WeakPtr token);
    void ClearCaptureToken();

    // Mirror of UIController::AreHidden() — sink only fires when true.
    void SetMenusHidden(bool hidden) { menusHidden_.store(hidden); }
    bool AreMenusHidden() const noexcept { return menusHidden_.load(); }

    // Cached game-pause state -- updated on every menu open/close event.
    // Use this instead of querying C++ menu state repeatedly from Papyrus.
    bool IsGamePaused() const noexcept { return isPaused_.load(); }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent*,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

private:
    MenuEventSink() = default;
    ~MenuEventSink() = default;
    MenuEventSink(const MenuEventSink&)            = delete;
    MenuEventSink& operator=(const MenuEventSink&) = delete;

    bool ShouldIgnoreMenu(const RE::BSFixedString& name) const;
    bool IsPauseOrInputMenu(const RE::BSFixedString& name) const;

    std::atomic<bool>            registered_{false};
    std::atomic<bool>            menusHidden_{false};
    mutable std::mutex           tokenMutex_;
    CancellationToken::WeakPtr   captureToken_;
    std::atomic<bool>            isPaused_{false};

    // Menus that should NOT cancel a capture
    static const std::set<std::string> kIgnoredMenus;

    // Menus that indicate pause or text input — trigger UI restore
    static const std::set<std::string> kPauseOrInputMenus;
};
