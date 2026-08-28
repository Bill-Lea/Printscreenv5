Scriptname Printscreen_Formula_script extends Quest

; ==============================================================================
; NATIVE FUNCTION DECLARATIONS - Core Capture Functions
; ==============================================================================

bool Function CheckPath(String path) Global Native

; TakePhoto — full V4 signature with video capture parameters.
; The final AutoUI parameter MUST match the C++ binding.
; It has a default so older Papyrus call sites that pass only the original
; video parameters can still compile.
; Returns immediately: "Started", "Already running", or an error string.
; Completion is notified via the PrintScreenComplete mod event.
; Parameter 8 (DeltaMode): 0=off (full frames), 1=region extraction, 2=true delta (with transparency)
; Parameter 9 (Optimize): 0=off, 1=on (transparency optimization for delta frames)
; Parameter 10 (Compression): PNG zlib level 0-9
String Function TakePhoto(String basePath, String imageType, float jpgCompression, String Mode, float Duration, float Fps, int LoopCount, int DeltaMode, int Optimize, int Compression, float VideoDuration, int TargetResolution, int VideoFrameRate, int QualityPreset, int VideoBitrate, float KeyframeInterval, int EncoderPreference, int RateControl, int VideoContainer, bool AutoUI = true) Global Native

;
String Function Cancel() Global Native

; Reset — force=true hard-aborts any active capture; force=false is a safe reset.
String Function MYReset(bool force = false) Global Native

; UI helpers exposed by Bindings.cpp.
bool Function SaveAndHideAllUI() Global Native
bool Function RestoreAllUI() Global Native
bool Function IsGamePaused() Global Native
;NEED TO ADD FUNCTIONS FOR JSON FILES