#include "PCH.h"
#include "Bindings.h"
#include "capture/CaptureSession.h"
#include "capture/CaptureRequest.h"
#include "capture/TempFileGuard.h"
#include "ui/MenuEventSink.h"
#include "ui/UIController.h"
#include "ConsoleCommandQueue.h"
#include "stringutils.h"

// ============================================================
// Script name (must match the Papyrus .psc file)
// ============================================================
static constexpr auto kScriptName = "Printscreen_Formula_script";

// ============================================================
// Helpers
// ============================================================
namespace {

// Escape a string for embedding in a JSON string literal.
// Without this, Windows paths ("C:\\Games\\...") and any quotes in error
// messages produced INVALID JSON (\G is not a legal JSON escape), which
// breaks JSON parsing/validation on the Papyrus side.
static std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// ------------------------------------------------------------
// Completion status protocol (shared with Printscreen_MainQuest_script.psc)
//
// The mod event carries the machine-readable outcome in numArg so Papyrus
// never has to parse the JSON string (StringUtil.Find is case-sensitive and
// substring matching over a payload that contains a user-chosen path is
// fragile). numArg packs two integers:
//
//     numArg = sequence * 10 + status
//     status:   0 = success, 1 = cancelled, 2 = error
//     sequence: the value returned by TakePhoto as "Started:<sequence>"
//
// Papyrus decodes with  status = code % 10,  sequence = code / 10  and drops
// any event whose sequence is not the capture it is currently waiting on, so
// a late event from a cancelled or reload-interrupted capture cannot clear
// the state of the capture that replaced it. A float holds integers exactly
// up to 2^24, so the packing stays exact for 1.6 million captures per
// game session.
//
// strArg still carries the JSON for logging and for the error message:
//     {"status":"success"|"cancelled"|"error","seq":N,"message":"...","path":"..."}
// ------------------------------------------------------------
enum CompletionStatus : int {
    kStatusSuccess   = 0,
    kStatusCancelled = 1,
    kStatusError     = 2
};

static std::atomic<std::uint32_t> g_captureSequence{0};

static const char* StatusName(int status) {
    switch (status) {
        case kStatusSuccess:   return "success";
        case kStatusCancelled: return "cancelled";
        default:               return "error";
    }
}

static float PackCompletionCode(std::uint32_t sequence, int status) {
    return static_cast<float>(sequence * 10u + static_cast<std::uint32_t>(status));
}

// Map the worker's CALLBACK_* result string to a status code and a
// human-readable message. Shared by every completion callback so the two
// TakePhoto entry points cannot drift apart.
static void ClassifyWorkerResult(const std::string& r, int& status, std::string& message) {
    if (r == "CALLBACK_SUCCESS") {
        status  = kStatusSuccess;
        message = "Capture completed successfully";
    } else if (r == "CALLBACK_CANCELLED") {
        status  = kStatusCancelled;
        message = "Capture was cancelled";
    } else if (r.rfind("CALLBACK_ERROR:", 0) == 0) {
        status  = kStatusError;
        message = r.substr(15);
        if (!message.empty() && message.front() == ' ')
            message.erase(0, 1);  // strip the space after "CALLBACK_ERROR:"
    } else {
        // The worker only emits CALLBACK_* strings; anything else is a bug in
        // the session and should surface as an error, not vanish.
        status  = kStatusError;
        message = r;
    }
}

// Queue a SKSE mod-callback event to notify Papyrus asynchronously.
// See "Completion status protocol" above for the numArg/strArg layout.
static void QueueModEvent(const std::string& eventName,
                          std::uint32_t sequence,
                          int status,
                          const std::string& message,
                          const std::string& outputPath = "") {
    auto* task = SKSE::GetTaskInterface();
    if (!task) return;

    // Build JSON payload (simple concatenation, no external dependency)
    std::string json = "{\"status\":\"" + std::string(StatusName(status)) + "\"," +
                       "\"seq\":" + std::to_string(sequence) + "," +
                       "\"message\":\"" + EscapeJson(message) + "\"";
    if (!outputPath.empty()) {
        json += ",\"path\":\"" + EscapeJson(outputPath) + "\"";
    }
    json += "}";

    const float code = PackCompletionCode(sequence, status);

    task->AddTask([eventName, json, code]() {
        auto* src = SKSE::GetModCallbackEventSource();
        if (!src) return;
        SKSE::ModCallbackEvent ev{
            RE::BSFixedString(eventName.c_str()),
            RE::BSFixedString(json.c_str()),
            code, nullptr
        };
        src->SendEvent(&ev);
    });
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

// Build a CaptureRequest from the flat Papyrus arguments.
static CaptureRequest BuildRequest(
    const std::string& basePath,
    const std::string& imageType,
    float jpgQuality,
    const std::string& compressionMode,
    float duration,
    float fps,
    int   loopCount,
    int   deltaMode,
    int   optimize,
    int   pngCompression,
    float videoDuration,
    int   targetResolution,
    int   videoFrameRate,
    int   qualityPreset,
    int   videoBitrateKbps,
    float keyframeIntervalSec,
    int   encoderPreference,
    int   rateControl,
    int   videoContainer)
{
    CaptureRequest req;
    req.outputDir        = SKSE::stl::utf8_to_utf16(basePath).value_or(L"");
    req.format           = ParseImageFormat(ToLower(imageType));
    req.jpegQuality      = std::clamp(jpgQuality, 0.0f, 100.0f);
    req.ddsMode          = ParseDDSMode(compressionMode);
    req.tiffMode         = ParseTiffMode(compressionMode);  // was missing — TIFF
                                                            // compression was always NONE
    req.animDuration     = std::clamp(duration, 0.1f, 60.0f);
    req.animFPS          = std::clamp(fps, 1.0f, 60.0f);
    req.loopCount        = std::clamp(loopCount, 0, 100);
    req.deltaMode        = std::clamp(deltaMode, 0, 2);
    req.optimize         = (optimize != 0) ? 1 : 0;
    req.pngCompression   = std::clamp(pngCompression, 0, 9);
    req.videoDuration    = videoDuration;
    req.targetResolution = std::clamp(targetResolution, 0, 4);
    req.videoFrameRate   = std::clamp(videoFrameRate, 30, 60);
    req.qualityPreset    = std::clamp(qualityPreset, 0, 4);
    req.videoBitrateKbps = std::clamp(videoBitrateKbps, 1000, 50000);
    req.keyframeIntervalSec = std::clamp(keyframeIntervalSec, 0.5f, 10.0f);
    req.encoderPreference   = std::clamp(encoderPreference, 0, 2);  // 0=Auto,1=HW,2=SW
    req.rateControl         = std::clamp(rateControl, 0, 2);        // 0=CBR,1=VBR,2=CQP
    req.videoContainer      = std::clamp(videoContainer, 0, 0); // only MP4 for now
    return req;
}

} // namespace

// ---------------------------------------------------------------------------
// JSON-native bridge -- parse a JSON string into a CaptureRequest.
// Uses nlohmann/json (assumes vcpkg dependency is present).
// ---------------------------------------------------------------------------
#include <nlohmann/json.hpp>

static CaptureRequest ParseRequestJson(const std::string& jsonStr) {
    CaptureRequest req;
    try {
        auto j = nlohmann::json::parse(jsonStr);

        if (j.contains("basePath") && j["basePath"].is_string()) {
            req.outputDir = SKSE::stl::utf8_to_utf16(j["basePath"].get<std::string>()).value_or(L"");
        }
        if (j.contains("imageType") && j["imageType"].is_string()) {
            req.format = ParseImageFormat(j["imageType"].get<std::string>());
        }
        if (j.contains("jpgCompression")) {
            req.jpegQuality = std::clamp(j["jpgCompression"].get<float>(), 0.0f, 100.0f);
        }
        if (j.contains("mode") && j["mode"].is_string()) {
            req.ddsMode = ParseDDSMode(j["mode"].get<std::string>());
            req.tiffMode = ParseTiffMode(j["mode"].get<std::string>());
        }
        if (j.contains("duration")) {
            req.animDuration = std::clamp(j["duration"].get<float>(), 0.1f, 60.0f);
            req.videoDuration = std::clamp(j["duration"].get<float>(), 1.0f, 120.0f);
        }
        if (j.contains("fps")) {
            req.animFPS = std::clamp(j["fps"].get<float>(), 1.0f, 60.0f);
        }
        if (j.contains("loopCount")) {
            req.loopCount = std::clamp(j["loopCount"].get<int>(), 0, 100);
        }
        if (j.contains("optimize")) {
            req.optimize = j["optimize"].get<int>() != 0 ? 1 : 0;
        }
        if (j.contains("deltaMode")) {
            req.deltaMode = std::clamp(j["deltaMode"].get<int>(), 0, 2);
        }
        if (j.contains("compression")) {
            req.pngCompression = std::clamp(j["compression"].get<int>(), 0, 9);
        }
        if (j.contains("targetResolution")) {
            req.targetResolution = std::clamp(j["targetResolution"].get<int>(), 0, 4);
        }
        if (j.contains("videoFrameRate")) {
            req.videoFrameRate = (j["videoFrameRate"].get<int>() == 60) ? 60 : 30;
        }
        if (j.contains("qualityPreset")) {
            req.qualityPreset = std::clamp(j["qualityPreset"].get<int>(), 0, 4);
        }
        if (j.contains("videoBitrate")) {
            req.videoBitrateKbps = std::clamp(j["videoBitrate"].get<int>(), 1000, 50000);
        }
        if (j.contains("keyframeInterval")) {
            req.keyframeIntervalSec = std::clamp(j["keyframeInterval"].get<float>(), 0.5f, 10.0f);
        }
        if (j.contains("encoderPreference")) {
            req.encoderPreference = std::clamp(j["encoderPreference"].get<int>(), 0, 4);
        }
        if (j.contains("rateControl")) {
            req.rateControl = std::clamp(j["rateControl"].get<int>(), 0, 4);
        }
        if (j.contains("videoContainer")) {
            req.videoContainer = std::clamp(j["videoContainer"].get<int>(), 0, 0);
        }
        if (j.contains("autoUI") && j["autoUI"].is_boolean()) {
            req.autoUI = j["autoUI"].get<bool>();
        }
    } catch (const std::exception& e) {
        logger::error("ParseRequestJson failed: {}", e.what());
    }
    return req;
}

static std::string TakePhoto_Internal_Json(RE::StaticFunctionTag*,
                                          std::string jsonConfig) {
    CaptureRequest req = ParseRequestJson(jsonConfig);
    // Same completion protocol as TakePhoto (see QueueModEvent), so a script
    // driving this entry point gets the same numArg code and sequence.
    const std::uint32_t seq = ++g_captureSequence;
    auto startResult = CaptureSession::GetSingleton().Start(
        req,
        [seq, outputDir = req.outputDir](const std::string& r) {
            int status;
            std::string message;
            ClassifyWorkerResult(r, status, message);
            QueueModEvent("PrintScreenComplete", seq, status, message,
                          util::wstring_to_utf8(outputDir));
        });

    switch (startResult) {
        case CaptureSession::StartResult::Accepted:
            return "Started:" + std::to_string(seq);
        case CaptureSession::StartResult::BusyCancelled:
            return "Previous capture cancelled";
        default:
            return "Error: Unable to start capture";
    }
}
static bool IsGamePaused_Cached(RE::StaticFunctionTag*) {
    // Return the value cached by MenuEventSink on menu open/close events.
    // The previous implementation walked the whole pause-menu list and called
    // RE::UI::GetMenu from the Papyrus VM thread on every poll — exactly what
    // the sink's cache was added to avoid (and it touched menu objects from a
    // non-game thread).
    return MenuEventSink::GetSingleton()->IsGamePaused();
}

[[maybe_unused]] static bool IsTextInputActive() {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return false;
    // Check for console or text input menus
    if (ui->IsMenuOpen("Console")) return true;
    if (ui->IsMenuOpen("Console Native UI Menu")) return true;
    return false;
}

// ============================================================
// UI visibility helpers (direct Scaleform — no console commands)
// ============================================================
namespace {

// Queue UI hide to game thread via SKSE task interface.
// Safe to call from any thread.
static void HideUIAsync() {
    auto* task = SKSE::GetTaskInterface();
    if (task) {
        task->AddTask([]() {
            UIController::GetSingleton().HideAll();
        });
    }
}

// Queue UI restore to game thread via SKSE task interface.
// Safe to call from any thread.
static void RestoreUIAsync() {
    auto* task = SKSE::GetTaskInterface();
    if (task) {
        task->AddTask([]() {
            UIController::GetSingleton().RestoreAll();
        });
    }
}

} // namespace

// ============================================================
// Papyrus-bound functions
// ============================================================

static std::string TakePhoto(
    RE::StaticFunctionTag*,
    std::string basePath, std::string imageType,
    float jpgQuality, std::string compressionMode,
    float duration, float fps,
    int loopCount, int deltaMode, int optimize, int pngCompression,
    float videoDuration, int targetResolution, int videoFrameRate,
    int qualityPreset, int videoBitrateKbps,
    float keyframeIntervalSec, int encoderPreference,
    int rateControl, int videoContainer,
    bool autoUI)
{
    logger::info("Papyrus TakePhoto: type={}, autoUI={}", imageType, autoUI);
    auto req = BuildRequest(basePath, imageType, jpgQuality, compressionMode,
                            duration, fps, loopCount, deltaMode, optimize,
                            pngCompression,
                            videoDuration, targetResolution, videoFrameRate,
                            qualityPreset, videoBitrateKbps,
                            keyframeIntervalSec, encoderPreference,
                            rateControl, videoContainer);
    req.autoUI = autoUI;

    auto& session = CaptureSession::GetSingleton();

    // Queue the UI hide before starting the worker thread. CaptureSession::Start()
    // holds its mutex until the thread is created, so the worker cannot call
    // SetResult (and queue UI restore) until after Start() returns. Queuing
    // HideUIAsync here guarantees [Hide, Restore] order in the SKSE task
    // queue even when a still capture completes before the calling frame ends.
    if (autoUI) {
        HideUIAsync();
        MenuEventSink::GetSingleton()->SetMenusHidden(true);
    }

    // Sequence number for this capture. Returned to Papyrus in the start
    // string and echoed back in the completion event so the script can tell
    // this capture's completion apart from a late one (see the protocol note
    // above QueueModEvent). Burned if Start() rejects the request — harmless.
    const std::uint32_t seq = ++g_captureSequence;

    auto startResult = session.Start(
        req,
        // Completion callback — fires after encoding (or on error/cancel).
        // Sends the PrintScreenComplete event with the packed status code in
        // numArg and the JSON payload in strArg.
        // Executes on the worker thread; UI restore is marshalled to game thread.
        [autoUI, seq, outputDir = req.outputDir](const std::string& r) {
            int status;
            std::string message;
            ClassifyWorkerResult(r, status, message);
            QueueModEvent("PrintScreenComplete", seq, status, message,
                          util::wstring_to_utf8(outputDir));
            MenuEventSink::GetSingleton()->ClearCaptureToken();
            if (autoUI) {
                RestoreUIAsync();
                MenuEventSink::GetSingleton()->SetMenusHidden(false);
            }
        },
        // Acquisition callback — fires after all frames are captured, before encoding.
        // Restores HUD immediately so slow DDS/AGIF/APNG encodes don't suppress UI.
        // Executes on the worker thread; marshalled to game thread via task queue.
        autoUI ? CaptureSession::AcquisitionCallback([]() {
            RestoreUIAsync();
            MenuEventSink::GetSingleton()->SetMenusHidden(false);
        }) : CaptureSession::AcquisitionCallback{});

    switch (startResult) {
        case CaptureSession::StartResult::Accepted:
            break;
        case CaptureSession::StartResult::BusyCancelled:
            if (autoUI) {
                RestoreUIAsync();
                MenuEventSink::GetSingleton()->SetMenusHidden(false);
            }
            return "Previous capture cancelled";
        default:
            if (autoUI) {
                RestoreUIAsync();
                MenuEventSink::GetSingleton()->SetMenusHidden(false);
            }
            return "Error: Unable to start capture";
    }

    if (autoUI) {
        MenuEventSink::GetSingleton()->SetCaptureToken(session.GetToken());
    }
    // "Started:<sequence>" — Papyrus stores the number and matches it against
    // the completion event's sequence.
    return "Started:" + std::to_string(seq);
}

static std::string GetResult(RE::StaticFunctionTag*) {
    // DEPRECATED: Polling is no longer supported. Use the PrintScreenComplete
    // mod event instead. This function now always returns "Deprecated".
    // Kept for backward compatibility with old Papyrus scripts that may
    // call it during migration to event-driven architecture.
    return "Deprecated";
}

static std::string Cancel(RE::StaticFunctionTag*) {
    auto& s = CaptureSession::GetSingleton();
    if (s.IsIdle()) return "Nothing to cancel";
    s.RequestCancel();
    return "Cancelled";
}

static std::string MYReset(RE::StaticFunctionTag*, bool force = true) {
    if (!force && !CaptureSession::GetSingleton().IsIdle())
        return "Cannot reset while active";
    CaptureSession::GetSingleton().ForceReset();
    // Restore UI directly via Scaleform (game thread)
    RestoreUIAsync();
    MenuEventSink::GetSingleton()->ClearCaptureToken();
    MenuEventSink::GetSingleton()->SetMenusHidden(false);
    return "Reset complete";
}

static bool CheckPath(RE::StaticFunctionTag*, std::string path) {
    if (path.empty() || path.size() < 3) return false;
    if (!((path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
          (path[0] == '\\' && path[1] == '\\'))) return false;
    try {
        std::filesystem::path fp(path);
        std::error_code ec;
        if (!std::filesystem::exists(fp, ec)) std::filesystem::create_directories(fp, ec);
        if (ec) return false;
        std::filesystem::path test = fp / "printscreen_test.tmp";
        std::ofstream f(test, std::ios::binary);
        if (!f.is_open()) return false;
        f.write("test", 4); f.close();
        std::filesystem::remove(test, ec);
        return true;
    } catch (...) { return false; }
}

// ============================================================
// Register
// ============================================================
bool PapyrusBindings::Register(RE::BSScript::IVirtualMachine* vm) {
    logger::info("=== Registering Papyrus functions ===");
    Initialize();

    int ok = 0, fail = 0;
    auto reg = [&](const char* name, auto fn) {
        try { vm->RegisterFunction(name, kScriptName, fn, true); ++ok; }
        catch (...) { logger::error("Failed to register '{}'", name); ++fail; }
    };

    reg("CheckPath",           CheckPath);
    reg("TakePhoto",           TakePhoto);
    // Get_Result is deprecated — Papyrus should use PrintScreenComplete mod event.
    // Kept registered so old scripts don't crash; returns "Deprecated".
    reg("Get_Result",          GetResult);
    reg("Cancel",              Cancel);
    reg("MYReset",             MYReset);
    reg("IsGamePaused",        IsGamePaused_Cached);

    logger::info("=== {} functions registered ({} failed) ===", ok, fail);
    return (fail == 0);
}

void PapyrusBindings::Initialize() {
    // Nothing extra needed — singletons are lazy-initialized
}

void PapyrusBindings::OnDataLoaded() {
    logger::info("PapyrusBindings::OnDataLoaded — registering MenuEventSink");
    auto* sink = MenuEventSink::GetSingleton();
    sink->Register();

    // Wire sink's cancel callback to the session
    // (The session sets the token on Start; sink holds a weak_ptr)
}

// ============================================================
// C++-driven hotkey re-initialization (save-load reliability)
// ============================================================
//
// WHY THIS EXISTS
// ---------------
// The only Papyrus path that re-arms the TakePhoto hotkey on load is:
//   Printscreen_PlayerRef_Script (ReferenceAlias)::OnPlayerLoadGame()
//     -> Printscreen_MainQuest_script::OnPlayerLoadGame()
//       -> InitializePrintscreen()   (UnregisterForAllKeys + RegisterForKey + mod-event re-reg)
// That chain depends on alias/property state BAKED INTO THE SAVE when the quest
// first started. A save created before the alias script (or its MainQuest
// property) existed never dispatches OnPlayerLoadGame, so the hotkey stays dead
// and cannot be recovered from the MCM -- no new .pex on disk can fix that save.
//
// The C++ plugin receives kPostLoadGame / kNewGame on EVERY load regardless of
// Papyrus save state (see the messaging listener in plugin.cpp). From there we
// locate the main quest and dispatch InitializePrintscreen() straight through
// the VM, which makes hotkey re-arming immune to stale-save alias problems.
// InitializePrintscreen() is idempotent (Unregister-then-Register), so running
// it from BOTH the C++ path and the Papyrus path on a healthy save is harmless.
namespace {

// Script that owns InitializePrintscreen(). Must match the Scriptname header in
// "Papyrus Scripts/Printscreen_MainQuest_script.psc" (VM class lookups are
// case-insensitive, but we match the source casing here).
static constexpr const char* kMainQuestScript = "Printscreen_MainQuest_script";

// OPTIONAL hard fallback -- only consulted if the script-attachment scan below
// fails. The scan is the primary mechanism and needs NONE of these: it finds
// the quest that actually has kMainQuestScript attached, which survives FormID,
// load-order and .esp-rename changes. These are intentionally left empty so the
// fallback stays INERT until you fill them in; while kFallbackPluginEsp is empty
// it never runs, so a placeholder can never resolve the wrong quest.
// TODO(William, optional): confirm against the .esp in xEdit / CK only if you
// ever want a deterministic FormID-based fallback.
static constexpr std::string_view kFallbackPluginEsp   = "";   // e.g. "Printscreen.esp"
static constexpr RE::FormID       kFallbackQuestFormID = 0;    // e.g. 0x000800

// Resolve the VM handle of the quest carrying kMainQuestScript.
// Returns policy->EmptyHandle() if it cannot be found.
static RE::VMHandle ResolveMainQuestHandle(
    RE::BSScript::Internal::VirtualMachine* a_vm,
    RE::BSScript::IObjectHandlePolicy*      a_policy)
{
    const RE::VMHandle empty = a_policy->EmptyHandle();

    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        return empty;
    }

    // --- Primary: scan every quest for the one with our script attached. ---
    for (auto* quest : dataHandler->GetFormArray<RE::TESQuest>()) {
        if (!quest) {
            continue;
        }
        const auto handle = a_policy->GetHandleForObject(quest->GetFormType(), quest);
        if (handle == empty) {
            continue;
        }
        RE::BSTSmartPointer<RE::BSScript::Object> obj;
        if (a_vm->FindBoundObject(handle, kMainQuestScript, obj) && obj) {
            return handle;
        }
    }

    // --- Optional fallback: explicit FormID + .esp (inert unless configured). ---
    if (!kFallbackPluginEsp.empty() && kFallbackQuestFormID != 0) {
        if (auto* quest =
                dataHandler->LookupForm<RE::TESQuest>(kFallbackQuestFormID, kFallbackPluginEsp)) {
            const auto handle = a_policy->GetHandleForObject(quest->GetFormType(), quest);
            if (handle != empty) {
                return handle;
            }
        }
    }

    return empty;
}

// Dispatch InitializePrintscreen() on the main quest, deferred one frame onto
// the game thread via the SKSE task queue. kPostLoadGame can arrive while the VM
// is still thawing save data; queuing through the task interface runs us on the
// game thread after the load completes. No new threads are created.
static void ReinitializePapyrusConfig() {
    auto* task = SKSE::GetTaskInterface();
    if (!task) {
        logger::error("PrintScreen: no SKSE task interface; cannot re-init hotkey");
        return;
    }

    task->AddTask([]() {
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            logger::error("PrintScreen: no Papyrus VM; cannot re-init hotkey");
            return;
        }
        auto* policy = vm->GetObjectHandlePolicy();
        if (!policy) {
            logger::error("PrintScreen: no object handle policy; cannot re-init hotkey");
            return;
        }

        const RE::VMHandle handle = ResolveMainQuestHandle(vm, policy);
        if (handle == policy->EmptyHandle()) {
            // Not fatal: on a brand-new game the quest script may not be attached
            // yet, and Printscreen_MainQuest_script::OnInit() handles first-time
            // registration there. On a load this means the script is genuinely
            // absent from the save -- log and leave the game untouched.
            logger::warn("PrintScreen: quest with {} not found; relying on Papyrus "
                         "registration for this load", kMainQuestScript);
            return;
        }

        // InitializePrintscreen() takes no arguments. The dispatch packs the
        // (empty) argument list synchronously, so the pointer does not outlive
        // this call (matches the standard CommonLibSSE-NG dispatch idiom).
        auto* args = RE::MakeFunctionArguments();
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        const bool ok = vm->DispatchMethodCall(
            handle, kMainQuestScript, "InitializePrintscreen", args, callback);
        logger::info("PrintScreen: InitializePrintscreen dispatch {}",
                     ok ? "succeeded" : "FAILED");
    });
}

} // namespace

void PapyrusBindings::OnPostLoadGame() {
    logger::info("PapyrusBindings::OnPostLoadGame — cleaning up stale state");

    // Cancel any in-flight capture FIRST. Previously the registered temp
    // directories were deleted before the session was cancelled, so a worker
    // still writing frames could have its temp dir removed out from under it.
    //
    // No synthetic "cancelled" event is sent from here. ForceReset() cancels
    // the token and the worker's own completion callback then delivers a
    // CALLBACK_CANCELLED event for that capture's sequence number. A second
    // event from here produced a duplicate, and the Papyrus side has already
    // reset its capture state in OnPlayerLoadGame anyway.
    auto& session = CaptureSession::GetSingleton();
    if (!session.IsIdle()) {
        logger::info("  Session was active — cancelling; worker will report CALLBACK_CANCELLED");
    }
    session.ForceReset();

    // Clean up any orphaned temp directories from interrupted captures.
    // This handles the case where a player enters a load door or dies
    // mid-capture, leaving temp frame files behind.
    logger::info("  Cleaning up orphaned temp directories from interrupted captures");
    TempFileGuard::CleanupAllRegistered();

    auto* sink = MenuEventSink::GetSingleton();

    // Only restore UI if it was left hidden by a prior capture session.
    // Restore UI directly via Scaleform (game thread).
    if (sink && sink->AreMenusHidden()) {
        logger::info("  Menus were hidden — restoring UI");
        RestoreUIAsync();
        sink->SetMenusHidden(false);
    }

    if (sink) {
        sink->ClearCaptureToken();
    }

    // Re-arm the TakePhoto hotkey from C++ on every load. This is the
    // reliability guarantee that survives stale-save alias state; the Papyrus
    // OnPlayerLoadGame path is kept as belt-and-braces (InitializePrintscreen()
    // is idempotent, so both paths firing is harmless).
    ReinitializePapyrusConfig();
}
