
Scriptname Printscreen_MainQuest_script extends Quest

Import StringUtil
Import Debug
Import Utility
Import Input
Import UI

; ==============================================================================
; VERSION
; ==============================================================================
String Property Version = "4.02" Auto Hidden

; ==============================================================================
; IMAGE CONFIGURATION
; ==============================================================================
String Property Path = "C:/Pictures" Auto
String Property ImageType = "PNG" Auto
float  Property JPG_Compression = 90.0 Auto
float  Property Duration = 5.0 Auto
float  Property Fps = 15.0 Auto
int    Property LoopCount = 0 Auto
int    Property Compression = 9 Auto          ; PNG zlib level 0-9
int    Property DeltaMode = 0 Auto           ; 0=off (full frames), 1=region extraction, 2=true delta (with transparency)
int    Property Optimize = 1 Auto
float  Property Quality = 0.85 Auto           ; Animated quality 0.0-1.0
String Property Tif_Mode = "UNCOMPRESSED" Auto
String Property DDS_Mode = "UNCOMPRESSED" Auto
String Property Mode = "UNCOMPRESSED" Auto     ; General compression mode for supported formats (e.g. BC7 for DDS, or GIF quantization)
; ==============================================================================
; VIDEO CONFIGURATION — mirrors VideoCaptureConfig.h
; ==============================================================================
float  Property VideoDuration       = 10.0 Auto   ; 1.0 – 120.0 seconds
int    Property TargetResolution    = 0    Auto    ; 0=Native, 1=720p, 2=1080p, 3=1440p, 4=4K
int    Property VideoFrameRate      = 30   Auto    ; 30 or 60
int    Property QualityPreset       = 2    Auto    ; 0=Low,1=Medium,2=High,3=VeryHigh,4=Custom
int    Property VideoBitrate        = 8000 Auto    ; kbps (Custom preset only)
float  Property KeyframeInterval    = 2.0  Auto    ; 0.5 – 10.0 seconds
int    Property EncoderPreference   = 0    Auto    ; 0=Auto,1=PreferHW,2=ForceSW
int    Property RateControl         = 1    Auto    ; 0=CBR,1=VBR,2=CQP
int    Property VideoContainer      = 0    Auto    ; 0=MP4
  ;  string jsonkey = ""
; ==============================================================================
; UI / HOTKEY
; ==============================================================================
bool Property Menu = true Auto
bool Property AutoUI = true Auto
int  Property Key_TakePhoto = 183 Auto


; ==============================================================================
; JSON PERSISTENCE
; ==============================================================================
bool   Property UseJsonFile = true Auto
String Property jsonFilename = "PrintScreen" Auto

; ==============================================================================
; INTERNAL STATE
; ==============================================================================
bool   Property bConfigOpen = false Auto Hidden
bool   Property IsLatentScreenshotActive = false Auto Hidden
bool   Property IsStartingCapture = false Auto Hidden
String Property Result = "Ready" Auto Hidden
int    Property Shots = 0 Auto Hidden

float _CaptureStartRealTime = 0.0
float LastKeyPressTime = 0.0
bool  _CompletionHandled = false

; ==============================================================================
; INITIALIZATION
; ==============================================================================
Function InitializePrintscreen()
    ; TARGETED unregistration only -- do NOT use UnregisterForAllKeys() /
    ; UnregisterForAllModEvents() here. Printscreen_MCM_script (SKI_ConfigBase)
    ; is attached to THIS SAME quest, and the "ForAll" calls act on the whole
    ; quest/form handle, so they also wiped SkyUI's own
    ; "SKICP_configManagerReady" registration on the shared quest. With the C++
    ; plugin now driving InitializePrintscreen reliably (and early) on every
    ; load, that scorched-earth reset raced with SkyUI and the MCM stopped
    ; registering -- the Papyrus log showed the menu "INITIALIZED" but never
    ; "Registered PrintScreen at MCM". Unregister only PrintScreen's OWN key and
    ; mod event, by name, so sibling scripts on the quest (the MCM) keep theirs.
    UnregisterForKey(Key_TakePhoto)
    UnregisterForModEvent("PrintScreenComplete")
    ; reset stale capture state before re-registering
    IsLatentScreenshotActive = false
    IsStartingCapture = false
    _CompletionHandled = false
    _CaptureStartRealTime = 0.0
    ; LastKeyPressTime is baked into the save, but GetCurrentRealTime() resets
    ; to 0 every game launch. A save from a long session makes the OnKeyUp
    ; debounce difference negative — swallowing every key press — until the
    ; new session outlasts the stale timestamp. Reset it on every load.
    LastKeyPressTime = 0.0
    ; A crash/quit while the MCM was open leaves bConfigOpen stuck true in
    ; the save, and OnKeyUp ignores every key press while it is set. Clear
    ; it on every load so a stale flag can never permanently eat the hotkey.
    bConfigOpen = false
    RegisterForKey(Key_TakePhoto)
    ;Register for the completion event in case it was missed during capture
    RegisterForModEvent("PrintScreenComplete", "OnPrintScreenComplete")
    ; Was a Debug.MessageBox — far too intrusive for something that now runs
    ; on every single game load (the OnPlayerLoadGame wiring is fixed).
    Debug.Notification("PrintScreen " + Version + " re-initialized")
EndFunction

Event OnPlayerLoadGame()
    ; Force-reset screenshot state on any load (death respawn, manual reload)
    IsLatentScreenshotActive = false
    IsStartingCapture = false
    _CompletionHandled = false
    _CaptureStartRealTime = 0.0
    Result = "Ready"
    ; Re-initialize hotkey and event registration
    InitializePrintscreen()
EndEvent


Event OnInit()

    bool allOK = true
    if (SKSE.GetVersion() == 0)
        Debug.MessageBox("ERROR: SKSE not detected.")
        allOK = false
    endif
    if (allOK)
    ; So here we need to check that the json file exists is valid Json and is
    ;a complete file. If not write out default values else read jason and validate it.
        if (UseJsonFile)
            If(!CheckJson() || !jsonComplete())
                writeJson()
            else
                readjson()
                validateAll()
            Endif
        Endif                                                                          
        RegisterForKey(Key_TakePhoto)
        RegisterForModEvent("PrintScreenComplete", "OnPrintScreenComplete")
        Debug.Notification("PrintScreen " + Version + " initialized (event-driven)")
    endif
EndEvent
;****************************** Helper functions **************************************

; --- Write all config to JSON -------------------------------------------------

Function WriteJson()
    ; Image
    Printscreen_JSON_script.SetStringValue(jsonFilename, "Path", Path)
    Printscreen_JSON_script.SetStringValue(jsonFilename, "ImageType", ImageType)
    Printscreen_JSON_script.SetFloatValue(jsonFilename, "JPG_Compression", JPG_Compression)
    Printscreen_JSON_script.SetStringValue(jsonFilename, "Mode", Mode)
    Printscreen_JSON_script.SetFloatValue(jsonFilename, "Duration", Duration)
    Printscreen_JSON_script.SetFloatValue(jsonFilename, "Fps", Fps)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "LoopCount", LoopCount)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "Compression", Compression)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "DeltaMode", DeltaMode)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "Optimize", Optimize)
    Printscreen_JSON_script.SetFloatValue(jsonFilename, "Quality", Quality)
    Printscreen_JSON_script.SetStringValue(jsonFilename, "Tif_Mode", Tif_Mode)
    Printscreen_JSON_script.SetStringValue(jsonFilename, "DDS_Mode", DDS_Mode)

    ; Video
    Printscreen_JSON_script.SetFloatValue(jsonFilename, "VideoDuration", VideoDuration)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "TargetResolution", TargetResolution)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "VideoFrameRate", VideoFrameRate)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "QualityPreset", QualityPreset)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "VideoBitrate", VideoBitrate)
    Printscreen_JSON_script.SetFloatValue(jsonFilename, "KeyframeInterval", KeyframeInterval)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "EncoderPreference", EncoderPreference)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "RateControl", RateControl)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "VideoContainer", VideoContainer)

    ; UI
    Printscreen_JSON_script.SetIntValue(jsonFilename, "Menu", Menu as int)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "AutoUI", AutoUI as int)
    Printscreen_JSON_script.SetIntValue(jsonFilename, "Key_TakePhoto", Key_TakePhoto)

    Printscreen_JSON_script.Save(jsonFilename)
    Debug.Notification("PrintScreen: Config saved to JSON") 
EndFunction

; --- Read all config from JSON with validation --------------------------------

Function ReadJson()
  ; Image
    Path = Printscreen_JSON_script.GetStringValue(jsonFilename,"Path")
    ImageType= Printscreen_JSON_script.GetStringValue(jsonFilename, "ImageType")
    JPG_Compression = Printscreen_JSON_script.GetFloatValue(jsonFilename, "JPG_Compression")
    Mode = Printscreen_JSON_script.GetStringValue(jsonFilename, "Mode")
    Duration = Printscreen_JSON_script.GetFloatValue(jsonFilename, "Duration")
    FPS = Printscreen_JSON_script.GetFloatValue(jsonFilename, "Fps")
    LoopCount = Printscreen_JSON_script.GetIntValue(jsonFilename, "LoopCount")
    Compression = Printscreen_JSON_script.GetIntValue(jsonFilename, "Compression")
    DeltaMode = Printscreen_JSON_script.GetIntValue(jsonFilename, "DeltaMode")
    Optimize = Printscreen_JSON_script.GetIntValue(jsonFilename, "Optimize") 
    Quality = Printscreen_JSON_script.GetFloatValue(jsonFilename, "Quality")
    TIF_Mode =Printscreen_JSON_script.GetStringValue(jsonFilename, "Tif_Mode")
    DDS_Mode = Printscreen_JSON_script.GetStringValue(jsonFilename, "DDS_Mode")

    ; Video
    VideoDuration = Printscreen_JSON_script.GetFloatValue(jsonFilename, "VideoDuration" )
    TargetResolution = Printscreen_JSON_script.GetIntValue(jsonFilename, "TargetResolution")
    VideoFrameRate = Printscreen_JSON_script.GetIntValue(jsonFilename, "VideoFrameRate")
    QualityPreset = Printscreen_JSON_script.GetIntValue(jsonFilename, "QualityPreset")
    VideoBitrate = Printscreen_JSON_script.GetIntValue(jsonFilename, "VideoBitrate")
    KeyframeInterval = Printscreen_JSON_script.GetFloatValue(jsonFilename, "KeyframeInterval")
    EncoderPreference = Printscreen_JSON_script.GetIntValue(jsonFilename, "EncoderPreference")
     RateControl = Printscreen_JSON_script.GetIntValue(jsonFilename, "RateControl")
    VideoContainer = Printscreen_JSON_script.GetIntValue(jsonFilename, "VideoContainer")

    ; UI
    Menu = Printscreen_JSON_script.GetIntValue(jsonFilename, "Menu") as BOOL
    AutoUI = Printscreen_JSON_script.GetIntValue(jsonFilename, "AutoUI") as bool
    Key_TakePhoto = Printscreen_JSON_script.GetIntValue(jsonFilename, "Key_TakePhoto")

    Debug.Notification("PrintScreen: Config loaded from JSON ") 
EndFunction

 bool Function CheckJson()

if(Printscreen_JSON_script.JsonExists(jsonfilename))
    if(Printscreen_JSON_script.isGood(JsonFileName))
        Debug.Notification("PrintScreen: JSON file exists and is valid")
        return true
    else
        Debug.Notification("PrintScreen: JSON file exists but is corrupted or invalid")
        return false
    endif
else
    Debug.Notification("PrintScreen: JSON file does not exist") 
    return false
endif 
EndFunction


; Returns TRUE only when every expected key is present in the JSON file.
; BUG FIX: this was inverted (returned true when a key was MISSING). While
; the HasIntValue("Mode") type mismatch existed the inversion was masked
; (the function always returned true); once Mode was corrected to
; HasStringValue the inversion went live and OnInit took the writeJson()
; branch for every COMPLETE file — overwriting the saved config (e.g.
; ImageType="AGIF") with script defaults ("PNG") instead of reading it.
; Also added the previously-unchecked "Path" key.
bool function jsonComplete()
if(!Printscreen_JSON_script.HasStringValue(jsonfilename,"Path" ) ||  !Printscreen_JSON_script.HasStringValue(jsonfilename,"ImageType" ) ||  !Printscreen_JSON_script.HasFloatValue(jsonfilename,"JPG_Compression" ) ||  !Printscreen_JSON_script.HasStringValue(jsonfilename,"Mode" ) ||  !Printscreen_JSON_script.HasFloatValue(jsonfilename,"Duration" ) ||  !Printscreen_JSON_script.HasFloatValue(jsonfilename,"Fps" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"LoopCount" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"Compression" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"DeltaMode" ) ||  !Printscreen_JSON_script.HasINtValue(jsonfilename,"Optimize" ) ||  !Printscreen_JSON_script.HasFloatValue(jsonfilename, "Quality" ) ||  !Printscreen_JSON_script.HasStringValue(jsonfilename,"Tif_Mode" ) ||  !Printscreen_JSON_script.HasStringValue(jsonfilename,"DDS_Mode" ) ||  !Printscreen_JSON_script.HasFloatValue(jsonfilename,"VideoDuration" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"TargetResolution" ) ||  !Printscreen_JSON_script.HasINTValue(jsonfilename,"VideoFrameRate" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"QualityPreset" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"VideoBitrate" ) ||  !Printscreen_JSON_script.HasFloatValue(jsonfilename,"KeyframeInterval" ) ||  !Printscreen_JSON_script.HasINTValue(jsonfilename,"EncoderPreference" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename, "RateControl") ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"VideoContainer" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"Menu" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"AutoUI" ) ||  !Printscreen_JSON_script.HasIntValue(jsonfilename,"Key_TakePhoto" ) )
return false
else
return true
endif
EndFunction

;validate functions
function validate_path()
    if(!Printscreen_Formula_script.CheckPath(Path))
        Debug.Notification("Invalid screenshot path. Path set to C:/Pictures")
        Path = "C:/Pictures"
    endif 
endfunction 
Function Validate_ImageType()
    If(Imagetype == "png" || Imagetype == "PNG")
        ImageType = "PNG"
    elseif(Imagetype == "BMP" || Imagetype == "bmp")
        ImageType = "BMP"
    elseif(Imagetype == "JPEG" || Imagetype == "JPG" || Imagetype == "jpg" || Imagetype == "jpeg")
        ImageType = "JPG"
    elseif(ImageType == "GIF" || ImageType == "gif")
        ImageType = "GIF"
    elseif(Imagetype == "TIF" || Imagetype == "TIFF" || Imagetype == "tif" || Imagetype == "tiff")
        ImageType = "TIF"
    Elseif(ImageType == "DDS" || ImageType == "dds")
        ImageType = "DDS"
    elseif(ImageType == "AGIF" || ImageType == "agif")
        ImageType= "AGIF"
    elseif(ImageType=="APNG"||ImageType=="apng")
        ImageType = "APNG"
    Elseif(ImageType == "H264")
        return
    else
    Debug.Notification("Invalid ImageType. ImageType set to PNG")
        ImageType = "PNG"
    endif 
EndFunction

Function Validate_JPG_Compression()
    if(JPG_Compression < 1.0)
        JPG_Compression = 1.0
    elseif(JPG_Compression > 100.0)
        JPG_Compression = 100.0
    endif
EndFunction

Function Validate_Mode()
    if(Mode != "UNCOMPRESSED" && Mode != "BC1" && Mode != "BC2" && Mode != "BC3" &&  Mode != "BC4" && Mode != "BC5" && Mode != "BC6H" && Mode != "BC7_SLOW" &&  Mode != "BC7_NORMAL" && Mode != "BC7_FAST" && Mode != "RLE" && Mode != "LZW" && Mode != "ZIP")
        Mode = "UNCOMPRESSED"
    Endif
EndFunction

Function Validate_Duration()
if(duration <=0 || Duration>15)
    if(Duration < 1.0)
        Duration = 1.0              
    elseif(Duration > 15.0)
        Duration = 15.0
    endif
Endif
EndFunction

Function Validate_Fps()
If(FPS == 0 || Fps < 15)
Fps=10
Endif
EndFunction

Function Validate_LoopCount()
if(LoopCount<0 || Loopcount > 20)
LoopCount = 0
EndIf
EndFunction

Function Validate_Compression()
    if(Compression < 0)
        Compression = 0
    elseif(Compression > 9)
        Compression = 9
    endif
EndFunction

Function Validate_DeltaMode()
    if(DeltaMode < 0)
        DeltaMode = 0
    elseif(DeltaMode > 2)
        DeltaMode = 2
    endif
EndFunction

Function Validate_Optimize()
    if(Optimize < 0)
        Optimize = 0
    elseif(Optimize > 1)
        Optimize = 1
    endif
EndFunction

Function Validate_Quality()
    if(Quality < 0.0)
        Quality = 0.0
    elseif(Quality > 1.0)
        Quality = 1.0
    endif
EndFunction

Function Validate_Tif_Mode()
    if(Tif_Mode != "RLE" && Tif_Mode != "LZW" && Tif_Mode != "ZIP")
        Tif_Mode = "UNCOMPRESSED"
    endif
EndFunction

Function Validate_DDS_Mode()
  String[] validModes = new String[10]
    validModes[0] = "UNCOMPRESSED"
    validModes[1] = "BC7_FAST"
    validModes[2] = "BC1"
    validModes[3] = "BC2"
    validModes[4] = "BC3"
    validModes[5] = "BC4"
    validModes[6] = "BC5"
    validModes[7] = "BC6H"
    validModes[8] = "BC7_SLOW"
    validModes[9] = "BC7_NORMAL"
    int i = 0
    while i < 10
        if DDS_Mode == validModes[i]
            return
        endif
        i += 1
    endwhile
    ;if you get here, the mode is invalid, so default to BC1
    DDS_Mode = "UNCOMPRESSED"
EndFunction

Function Validate_VideoDuration()
If(VideoDuration <= 0.0 || VideoDuration > 120.0)
VideoDuration =15.0
endif
EndFunction

Function Validate_TargetResolution()
If(TargetResolution==0 || TargetResolution == 1 || TargetResolution == 2 || TargetResolution == 3|| TargetResolution == 4)
return
else
TargetResolution=0
endif
EndFunction

Function Validate_VideoFrameRate()
If(VideoFrameRate == 30 || VideoFrameRate == 60)
return
else
VideoFrameRate = 30
return
EndIf
EndFunction

Function Validate_QualityPreset()
if(QualityPreset==0 || QualityPreset == 1 || QualityPreset == 2  || QualityPreset==3 || QualityPreset == 4)
return
else
QualityPreset = 2
endif
EndFunction

Function Validate_VideoBitrate()
    if (QualityPreset == 4)  ; Custom preset — allow user-defined bitrate
        if (VideoBitrate < 1000)
            VideoBitrate = 1000
        elseif (VideoBitrate > 50000)
            VideoBitrate = 50000
        endif
    else
        VideoBitrate = 8000  ; Default for non-Custom presets
    endif
EndFunction

Function Validate_KeyframeInterval()
if(KeyframeInterval< 0.2 || KeyframeInterval <= 10.0)
return
else
KeyframeInterval= 2.0
endif 
EndFunction

Function Validate_EncoderPreference()
If(EncoderPreference == 0 || EncoderPreference == 1 || EncoderPreference == 2)
return
else
EncoderPreference = 0
endif
EndFunction

Function Validate_RateControl()
if(RateControl == 0 || RateControl == 1 || RateControl ==2)
return
else
RateControl=0
Endif
EndFunction

Function Validate_VideoContainer()
VideoContainer = 0
Endfunction

Function Validate_Menu()
If(Menu == true || Menu == false)
return
else
Menu= true
Endif
EndFunction

Function Validate_AutoUI()
if(AutoUI == true || AutoUI == false)
return
else
AutoUI=Menu
Endif
EndFunction

Function Validate_Key_TakePhoto()
if(Key_TakePhoto < 0 || Key_TakePhoto >256)
Key_takePhoto = 14
endif
EndFunction

Function RecalculateFPS()
    if(Duration < 4.0)
        FPS = 15.0
        return
    elseif(Duration < 6.0)
        FPS = 12.0
        return
    elseif(Duration < 9.0)
        FPS = 10.0
        return
    elseif(Duration < 13.0)
        FPS = 8.0
        return
    else
        FPS = 6.0
        return
    endif
EndFunction

function ValidateAll()
validate_path()
Validate_ImageType()
Validate_JPG_Compression()
Validate_Mode()
Validate_Duration()
Validate_Fps()
Validate_LoopCount()
Validate_Compression()
Validate_DeltaMode()
Validate_Optimize()
Validate_Quality()
Validate_Tif_Mode()
Validate_DDS_Mode()
Validate_VideoDuration()
Validate_TargetResolution()
Validate_VideoFrameRate()
Validate_QualityPreset()
Validate_VideoBitrate()
Validate_KeyframeInterval()
Validate_EncoderPreference()
Validate_RateControl()
Validate_VideoContainer()
Validate_Menu()
Validate_AutoUI()
Validate_Key_TakePhoto()
endFunction

; ==============================================================================
; CAPTURE LOGIC
; ==============================================================================

Function CaptureImage(String basePath, String imgType, float jpgComp,  String captureMode, float captureDuration)

    Debug.Trace("PrintScreen: CaptureImage — type=" + imgType + " mode=" + captureMode)

    if (!Printscreen_Formula_script.CheckPath(basePath))
        Debug.Notification("Invalid screenshot path!")
        IsStartingCapture = false
        return
    endif

    ; When AutoUI is true, the C++ native handles all UI hide/show via SendConsoleCommand("tm").
    ; Papyrus-side HideHud/ShowHud calls are removed to prevent double-toggling.

    IsLatentScreenshotActive = true
    _CaptureStartRealTime = Utility.GetCurrentRealTime()
    _CompletionHandled = false

    ; -----------------------------------------------------------------------
    ; TakePhoto signature (V4): all image + video params always passed.
    ; C++ side reads only the params relevant to imgType.
    ; Parameter 8 = DeltaMode (0=off, 1=region, 2=true delta)
    ; Parameter 9 = Optimize (0=off, 1=on — transparency for delta frames)
    ; Parameter 10 = Compression (PNG zlib level 0-9)
    ; AutoUI is the 19th parameter and must match the C++ binding exactly.
    ; -----------------------------------------------------------------------
    String startResult = Printscreen_Formula_script.TakePhoto( basePath,  imageType,  jpg_Compression,  Mode,  Duration,  Fps, LoopCount,  DeltaMode, Optimize, Compression,  VideoDuration,  TargetResolution, VideoFrameRate,  QualityPreset,  VideoBitrate,  KeyframeInterval,  EncoderPreference,  RateControl, VideoContainer, AutoUI)

    if (startResult == "Started")
        ; Event-driven: no polling needed. PrintScreenComplete event will fire on completion.
        return
    elseif (startResult == "Already running")
        Debug.Notification("Capture already in progress")
        IsStartingCapture = false
        return
    else
        Debug.Notification("Capture failed: " + startResult)
        OnScreenshotCompleted(startResult)
        return
    endif
EndFunction

; ==============================================================================
; COMPLETION HANDLING
; ==============================================================================

Function OnScreenshotCompleted(String completionResult)
    Debug.Trace("PrintScreen: Completed — " + completionResult)

    ; Always ensure state is cleaned up even if called multiple times
    IsLatentScreenshotActive = false
    IsStartingCapture = false
    _CaptureStartRealTime = 0.0

    if (StringUtil.Find(completionResult, "Success") >= 0)
        Shots += 1
        Debug.Notification("Screenshot saved! Total: " + Shots)
    elseif (StringUtil.Find(completionResult, "Cancel") >= 0)
        Debug.Notification("Capture cancelled")
    else
        Debug.Notification("Capture: " + completionResult)
    endif

    IsLatentScreenshotActive = false
    IsStartingCapture = false
    _CaptureStartRealTime = 0.0

    ; In event-driven mode, C++ callback handles all state cleanup.
    ; No need to poll Get_Result — the event already delivered the final state.
    ; UI restore is handled by C++ when AutoUI=true (via callback)
EndFunction

; ==============================================================================
; EVENT HANDLERS — Pure event-driven, no polling
; ==============================================================================

Event OnPrintScreenComplete(string eventName, string strArg, float numArg, Form sender)
    ; Legacy string format (backward compatible)
    _HandleCompletionResult(strArg)
EndEvent



Event OnKeyUp(int theKey, float holdtime)
    if (theKey != Key_TakePhoto)
        return
    endif
    if (bConfigOpen || UI.IsTextInputEnabled() || Utility.IsInMenuMode())
        return
    endif

    ; Event-driven: use local state flag instead of polling Get_Result.
    ; If a capture is active, offer to cancel it.
    if (IsLatentScreenshotActive || IsStartingCapture)
        Debug.Notification("Cancelling...")
        Printscreen_Formula_script.Cancel()
        IsLatentScreenshotActive = false
        IsStartingCapture = false
        _CompletionHandled = false
        ; UI restore handled by C++ callback when AutoUI=true
        return
    endif

    ; Debounce. Self-healing guard: if the real-time clock went backwards
    ; (save-baked LastKeyPressTime from a longer previous session), the
    ; stale value would debounce every press — clear it instead.
    float currentTime = Utility.GetCurrentRealTime()
    if (currentTime < LastKeyPressTime)
        LastKeyPressTime = 0.0
    elseif (currentTime - LastKeyPressTime < 0.75)
        return
    endif

    IsStartingCapture = true
    LastKeyPressTime = currentTime
    _CompletionHandled = false

    ; All params passed through TakePhoto; C++ uses what it needs per ImageType
    if (ImageType == "H264")
        Debug.Notification("Recording video...")
    else
        Debug.Notification("Taking screenshot...")
    endif
    CaptureImage(Path, ImageType, JPG_Compression, Mode, Duration)
EndEvent

; ==============================================================================
; HELPER FUNCTIONS
; ==============================================================================

bool Function _IsLongRunningCapture()
    if (ImageType == "GIF" || ImageType == "AGIF" || ImageType == "APNG")
        return true
    endif
    if (ImageType == "H264")
        return true
    endif
    if (ImageType == "DDS")
        if (StringUtil.Find(Mode, "BC6") >= 0 || StringUtil.Find(Mode, "BC7") >= 0)
            return true
        endif
    endif
    return false
EndFunction



bool Function _HandleCompletionResult(String r)
    if (_CompletionHandled)
        return true
    endif
    Result = r

    ; Prefer explicit CALLBACK_ prefix from C++ side
    if (StringUtil.Find(Result, "CALLBACK_") == 0)
        _CompletionHandled = true
        if (Result == "CALLBACK_SUCCESS")
            OnScreenshotCompleted("Success")
        elseif (Result == "CALLBACK_CANCELLED")
            OnScreenshotCompleted("Cancelled")
        elseif (StringUtil.Find(Result, "CALLBACK_ERROR:") == 0)
            OnScreenshotCompleted(StringUtil.Substring(Result, 15))
        else
            OnScreenshotCompleted(Result)
        endif
        return true
    endif

    ; Fallback string matching for legacy/event path
    if (Result == "Ready")
        _CompletionHandled = true
        OnScreenshotCompleted("Success")
        return true
    elseif (StringUtil.Find(Result, "Success") >= 0)
        _CompletionHandled = true
        OnScreenshotCompleted("Success")
        return true
    elseif (StringUtil.Find(Result, "Error") >= 0 || StringUtil.Find(Result, "Failed") >= 0)
        _CompletionHandled = true
        OnScreenshotCompleted(Result)
        return true
    elseif (StringUtil.Find(Result, "Cancel") >= 0)
        _CompletionHandled = true
        OnScreenshotCompleted("Cancelled")
        return true
    endif

    return false
EndFunction

Function UpdateHotkey(int newKey)
    UnregisterForKey(Key_TakePhoto)
    Key_TakePhoto = newKey
    RegisterForKey(Key_TakePhoto)
EndFunction

Function SetConfigOpen(bool isOpen)
    bConfigOpen = isOpen
EndFunction

; Video/Animated duration clamping — called by MCM when ImageType changes

Function ClampDurationForVideoMode()
    if (VideoDuration > 120.0)
        VideoDuration = 120.0
        Debug.Notification("Video duration clamped to 120s")
    endif
EndFunction

Function ClampDurationForAnimatedMode()
    if (Duration > 15.0)
        Duration = 15.0
        Debug.Notification("Animated duration clamped to 15s")
    endif
EndFunction
