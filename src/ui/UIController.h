#pragma once
#include <mutex>
#include <string>
#include <atomic>

// ============================================================
// UIController
// Manages UI visibility for screenshot/video capture.
//
// Primary mechanism: RE::UI::showMenus (the bool that the
// console "tm" command toggles).  This is a single flag checked
// early in the engine render loop; when false the entire UI
// rendering pass is skipped — HUD, compass, crosshair, mod
// HUDs, everything.
//
// Because showMenus is a plain bool write it is effectively
// thread-safe and can be toggled directly from the worker
// thread with no marshalling.  The Async helpers are kept for
// API consistency but now just do the write inline.
//
// The previous Scaleform per-element approach is retained as
// optional granular helpers (SetSubtitlesVisible, etc.) for
// cases where selective hiding is desired (e.g. MCM options).
// These still require game-thread execution (GFx is not
// thread-safe) and should be called via AddTask.
// ============================================================
class UIController {
public:
    static UIController& GetSingleton();

    // --------------------------------------------------------
    // Primary hide / restore  (showMenus flag)
    // Safe to call from ANY thread.
    // --------------------------------------------------------
    bool HideAll();
    bool RestoreAll();

    // Convenience wrappers — identical behaviour, kept for
    // call-site readability when invoked from a worker thread.
    void HideAllAsync();
    void RestoreAllAsync();

    bool AreHidden() const;

    // --------------------------------------------------------
    // Granular Scaleform helpers  (game-thread only)
    // These operate independently of showMenus.  If showMenus
    // is false the engine won't render any of this anyway, so
    // these are mainly useful when showMenus is still true and
    // you want to hide individual elements.
    // --------------------------------------------------------
    bool  SetHUDAlpha(float alpha);
    float GetHUDAlpha() const;
    bool  HideHUD();
    bool  ShowHUD();
    bool  IsHUDVisible() const;

    bool SetSubtitlesVisible(bool v);
    bool SetNPCNamesVisible(bool v);
    bool SetEnemyHealthVisible(bool v);
    bool SetCompassVisible(bool v);
    bool SetCrosshairVisible(bool v);
    bool SetQuestMarkersVisible(bool v);
    bool SetPlayerStatsVisible(bool v);

    // Console commands (queued to main thread)
    bool ExecuteConsoleAsync(std::string command);

private:
    UIController() = default;
    UIController(const UIController&)            = delete;
    UIController& operator=(const UIController&) = delete;

    // We only need to remember the previous showMenus value
    // so we can restore it correctly (it's almost always true,
    // but another mod could have toggled it).
    std::atomic<bool> hidden_{false};
    std::atomic<bool> savedShowMenus_{true};
};
