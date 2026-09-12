Scriptname Printscreen_MCM_Script extends SKI_Configbase

; ==============================================================================
; PROPERTIES
; ==============================================================================

Printscreen_MainQuest_script Property MainQuest Auto

; --- Option IDs: Settings page ---
int Property PathID auto
int Property ImageTypeID auto
int Property RemoveMenuID auto
int Property KeyCodeID auto
int Property UseJsonFileID auto

int Property JPG_CompressionID auto
int Property Compression_ID auto
int Property Quality_ID auto
int Property DurationID auto
int Property FPSID auto
int Property LoopID auto
int Property OptimizeID auto
int Property DeltaModeID auto

int Property Tif_ModeID auto
int Property DDS_ModeID auto


; --- Option IDs: Video Settings page ---
; (data lives on MainQuest; these are just this page's widget handles)
int oidVideoDuration    ; Capture
int oidResolution       ; Capture
int oidFrameRate        ; Capture
int oidQualityPreset    ; Encoding
int oidBitrate          ; Encoding
int oidKeyframeInterval ; Encoding
int oidEncoderPref      ; Encoding
int oidRateControl      ; Encoding
int oidContainer        ; Encoding

; --- Local state ---
String MyPath = ""
String InfoText = ""

; ==============================================================================
; DROPDOWN ARRAYS — Image formats
; ==============================================================================

String[] Function ImageArray()
    string[] arr = Utility.CreateStringArray(9)
    arr[0] = "PNG"
    arr[1] = "APNG"
    arr[2] = "BMP"
    arr[3] = "TIF"
    arr[4] = "JPG"
    arr[5] = "GIF"
    arr[6] = "AGIF"
    arr[7] = "DDS"
    arr[8] = "H264"
    return arr
EndFunction

String[] Function DDSArray()
    string[] dds = Utility.CreateStringArray(10)
    dds[0] = "UNCOMPRESSED"
    dds[1] = "BC1"
    dds[2] = "BC2"
    dds[3] = "BC3"
    dds[4] = "BC4"
    dds[5] = "BC5"
    dds[6] = "BC6h"
    dds[7] = "BC7_SLOW"
    dds[8] = "BC7_NORMAL"
    dds[9] = "BC7_FAST"
    return dds
EndFunction

String[] Function TifArray()
    String[] arr = Utility.CreateStringArray(4)
    arr[0] = "UNCOMPRESSED"
    arr[1] = "RLE"
    arr[2] = "LZW"
    arr[3] = "ZIP"
    return arr
EndFunction

; ==============================================================================
; DROPDOWN ARRAYS — Video settings
; ==============================================================================

String[] Function ResolutionOptions()
    String[] arr = Utility.CreateStringArray(5)
    arr[0] = "Native"
    arr[1] = "720p"
    arr[2] = "1080p"
    arr[3] = "1440p"
    arr[4] = "4K"
    return arr
EndFunction

String[] Function QualityPresetOptions()
    String[] arr = Utility.CreateStringArray(5)
    arr[0] = "Low"
    arr[1] = "Medium"
    arr[2] = "High"
    arr[3] = "Very High"
    arr[4] = "Custom"
    return arr
EndFunction

String[] Function EncoderOptions()
    String[] arr = Utility.CreateStringArray(3)
    arr[0] = "Auto"
    arr[1] = "Prefer Hardware"
    arr[2] = "Force Software"
    return arr
EndFunction

String[] Function RateControlOptions()
    String[] arr = Utility.CreateStringArray(3)
    arr[0] = "CBR"
    arr[1] = "VBR"
    arr[2] = "CQP"
    return arr
EndFunction

String[] Function ContainerOptions()
    String[] arr = Utility.CreateStringArray(1)
    arr[0] = "MP4"
    return arr
EndFunction

String[] Function FrameRateOptions()
    String[] arr = Utility.CreateStringArray(2)
    arr[0] = "30 FPS"
    arr[1] = "60 FPS"
    return arr
EndFunction

; ==============================================================================
; WIDGET ENABLE/DISABLE — single source of truth
; ==============================================================================

Function EnableWidgetsForType(string imgType)
    ResetImageFlags()

    if (imgType == "JPG")
        SetOptionFlags(JPG_CompressionID, OPTION_FLAG_NONE)

    elseif (imgType == "TIF")
        SetOptionFlags(Tif_ModeID, OPTION_FLAG_NONE)

    elseif (imgType == "DDS")
        SetOptionFlags(DDS_ModeID, OPTION_FLAG_NONE)

    elseif (imgType == "GIF")


    elseif (imgType == "AGIF" || imgType == "APNG")
        
        SetOptionFlags(OptimizeID, OPTION_FLAG_NONE)
        SetOptionFlags(DeltaModeID, OPTION_FLAG_NONE)
        SetOptionFlags(FPSID, OPTION_FLAG_NONE)
        SetOptionFlags(LoopID, OPTION_FLAG_NONE)
        SetOptionFlags(Quality_ID, OPTION_FLAG_NONE)
        SetOptionFlags(DurationID, OPTION_FLAG_NONE)


    elseif (imgType == "PNG")
        
        SetOptionFlags(Compression_ID, OPTION_FLAG_NONE)

    elseif (imgType == "H264")
        ; H264 uses the Video Settings page for configuration.
        ; No image-specific widgets needed here.

    elseif (imgType == "BMP")
        ; No format-specific widgets
    endif
EndFunction

Function ResetImageFlags()
    SetOptionFlags(Tif_ModeID, OPTION_FLAG_DISABLED)
    SetOptionFlags(JPG_CompressionID, OPTION_FLAG_DISABLED)
    SetOptionFlags(DDS_ModeID, OPTION_FLAG_DISABLED)
    
    
    SetOptionFlags(OptimizeID, OPTION_FLAG_DISABLED)
    SetOptionFlags(DeltaModeID, OPTION_FLAG_DISABLED)
    SetOptionFlags(FPSID, OPTION_FLAG_DISABLED)
    SetOptionFlags(LoopID, OPTION_FLAG_DISABLED)
    SetOptionFlags(Quality_ID, OPTION_FLAG_DISABLED)
    SetOptionFlags(Compression_ID, OPTION_FLAG_DISABLED)
    SetOptionFlags(DurationID, OPTION_FLAG_DISABLED)
EndFunction

; Helper: convert VideoFrameRate int to dropdown index
int Function FrameRateToIndex()
    if (MainQuest.VideoFrameRate >= 60)
        return 1
    endif
    return 0
EndFunction

; Helper: clamp a dropdown index into [0, maxIndex] — Papyrus has no Math.MinInt
int Function ClampIndex(int value, int maxIndex)
    if (value < 0)
        return 0
    endif
    if (value > maxIndex)
        return maxIndex
    endif
    return value
EndFunction

; ==============================================================================
; MCM LIFECYCLE
; ==============================================================================

Event OnConfigOpen()
    MainQuest.bConfigOpen = true
EndEvent

Event OnConfigInit()
    ModName = "PrintScreen"
    pages = New string[2]
    pages[0] = "Settings"
    pages[1] = "Video Settings"
EndEvent

Event OnConfigClose()
    MainQuest.bConfigOpen = false
    ; BUG FIX: the old code called RegisterForKey here, which registered THIS
    ; MCM script for the key — a script with no OnKeyUp handler, so the
    ; registration did nothing. Route through UpdateHotkey on MainQuest
    ; instead: it is idempotent (unregister + re-register the same key) and
    ; also self-heals a lost registration, e.g. after a mid-playthrough
    ; install where no valid registration existed yet.
    MainQuest.UpdateHotkey(MainQuest.Key_TakePhoto)
    if (MainQuest.UseJsonFile)
        MainQuest.WriteJson()
    endif
EndEvent

; ==============================================================================
; PAGE RESET
; ==============================================================================

Event OnPageReset(string pagename)

    ; --- Splash page (no page selected) ---
    if (pagename == "")
        SetCursorFillMode(TOP_TO_BOTTOM)
        SetCursorPosition(1)
        int imageSelect = Utility.RandomInt(1, 6)
        LoadCustomContent("PrintScreen/ScreenShot0" + imageSelect + ".dds")
        return
    else
        UnloadCustomContent()
    endif

    ; ==========================================================================
    ; SETTINGS PAGE
    ; ==========================================================================
    if (pagename == "Settings")
        SetCursorFillMode(TOP_TO_BOTTOM)
        SetCursorPosition(0)

        ; --- Header ---
        AddHeaderOption("PrintScreen version " + MainQuest.Version)
        AddEmptyOption()

        ; --- Path ---
        int OL = StringUtil.GetLength(MainQuest.Path)
        int pathFlag = OPTION_FLAG_NONE
        if (OL > 30)
            MyPath = "Json Long Path Option"
            InfoText = "Long path — use JSON file; MCM editing disabled"
            pathFlag = OPTION_FLAG_DISABLED
        else
            MyPath = MainQuest.Path
            InfoText = "Enter path to image storage"
        endif
        PathID = AddInputOption("Path", MyPath, pathFlag)
        AddEmptyOption()

        ; --- Image type ---
        int I = ImageArray().Find(MainQuest.ImageType)
        if (I < 0)
            I = 0
            ; Defensive fallback: if ImageType is ever somehow invalid, keep
            ; the displayed dropdown and the underlying property in sync
            ; instead of only fixing the display.
            MainQuest.ImageType = ImageArray()[I]
            Debug.Notification("Invalid Image Type. Defaulting to PNG.")
        endif
        ImageTypeID = AddMenuOption("Select Image File Type", ImageArray()[I], 0)
        AddEmptyOption()

        ; --- Toggles ---
        RemoveMenuID = AddToggleOption("Automatic Menu Removal", MainQuest.Menu, 0)
        AddEmptyOption()
        KeyCodeID = AddKeyMapOption("Select Take Photo Key", MainQuest.Key_TakePhoto, 0)
        AddEmptyOption()
        UseJsonFileID = AddToggleOption("Save/Restore Configuration", MainQuest.UseJsonFile)

        ; --- Format-specific widgets (left column) ---
        SetCursorPosition(3)

        ; BUG FIX: the old code created every widget below hard-disabled and
        ; then tried to re-enable the right ones via EnableWidgetsForType()
        ; (SetOptionFlags) at the end of this same OnPageReset pass. Flags
        ; set on an option in the same build that created it don't reliably
        ; take effect — that's why widgets stayed disabled on first display
        ; but worked once OnOptionMenuAccept called SetOptionFlags against an
        ; already-rendered page. The Video Settings page below never had this
        ; problem because it computes its flag *before* creating each widget.
        ; Mirror that here: work out each widget's initial flag first, then
        ; bake it into the Add*Option call directly.
        string curType = MainQuest.ImageType
        int jpgFlag = OPTION_FLAG_DISABLED
        int tifFlag = OPTION_FLAG_DISABLED
        int ddsFlag = OPTION_FLAG_DISABLED
        int gifFlag = OPTION_FLAG_DISABLED
        int pngFlag = OPTION_FLAG_DISABLED
        int animFlag = OPTION_FLAG_DISABLED ; shared by AGIF/APNG widgets

        if (curType == "JPG")
            jpgFlag = OPTION_FLAG_NONE
        elseif (curType == "TIF")
            tifFlag = OPTION_FLAG_NONE
        elseif (curType == "DDS")
            ddsFlag = OPTION_FLAG_NONE
        elseif (curType == "GIF")
            gifFlag = OPTION_FLAG_NONE
        elseif (curType == "AGIF" || curType == "APNG")
            animFlag = OPTION_FLAG_NONE
        elseif (curType == "PNG")
            pngFlag = OPTION_FLAG_NONE
        endif

        JPG_CompressionID = AddSliderOption("JPG Compression", MainQuest.JPG_Compression, "{0}", jpgFlag)
        AddEmptyOption()
        Quality_ID = AddSliderOption("Quality", MainQuest.Quality * 100.0, "{0}%", animFlag)
        DurationID = AddSliderOption("Capture Duration", MainQuest.Duration, "{0} s", animFlag)
        AddEmptyOption()

        I = DDSArray().Find(MainQuest.DDS_Mode)
        if (I < 0)
            I = 0
            MainQuest.DDS_Mode = DDSArray()[I]
            Debug.MessageBox("Invalid DDS mode. Defaulting to UNCOMPRESSED.")
        endif
        DDS_ModeID = AddMenuOption("DDS Mode", DDSArray()[I], ddsFlag)

        I = TifArray().Find(MainQuest.Tif_Mode)
        if (I < 0)
            I = 0
            MainQuest.Tif_Mode = TifArray()[I]
        endif
        Tif_ModeID = AddMenuOption("Tif Compression Mode", TifArray()[I], tifFlag)

        FPSID = AddSliderOption("FPS", MainQuest.Fps, "{0}", animFlag)
        LoopID = AddSliderOption("Loop Count", MainQuest.LoopCount as float, "{0}", animFlag)
        OptimizeID = AddSliderOption("Optimize", MainQuest.Optimize as float, "{0}", animFlag)
        DeltaModeID = AddSliderOption("Delta Mode", MainQuest.DeltaMode as float, "{0}", animFlag)
        Compression_ID = AddSliderOption("PNG Compression", MainQuest.Compression as float, "{0}", pngFlag)

     
     

        ; Defensive: also run the SetOptionFlags-based pass. Harmless if the
        ; flags above already rendered correctly; keeps behavior correct if
        ; ImageType ever changes without a full page rebuild.
        EnableWidgetsForType(MainQuest.ImageType)

    ; ==========================================================================
    ; VIDEO SETTINGS PAGE
    ; ==========================================================================
    elseif (pagename == "Video Settings")
        SetCursorFillMode(TOP_TO_BOTTOM)

        ; All video settings are disabled unless ImageType == H264
        int videoFlags = OPTION_FLAG_NONE
        if (MainQuest.ImageType != "H264")
            videoFlags = OPTION_FLAG_DISABLED
        endif

        AddHeaderOption("Capture", 0)
        oidVideoDuration = AddSliderOption("Duration (seconds)", MainQuest.VideoDuration, "{1} s", videoFlags)
        oidResolution = AddMenuOption("Target Resolution", ResolutionOptions()[ClampIndex(MainQuest.TargetResolution, 4)], videoFlags)
        oidFrameRate = AddMenuOption("Frame Rate", FrameRateOptions()[FrameRateToIndex()], videoFlags)

        AddHeaderOption("Encoding", 0)
        oidQualityPreset = AddMenuOption("Quality Preset", QualityPresetOptions()[MainQuest.QualityPreset], videoFlags)

        ; Bitrate: disabled unless H264 AND Custom preset AND not CQP
        int bitrateFlags = videoFlags
        if (videoFlags == OPTION_FLAG_NONE && MainQuest.QualityPreset != 4)
            bitrateFlags = OPTION_FLAG_DISABLED
        endif
        if (videoFlags == OPTION_FLAG_NONE && MainQuest.RateControl == 2)
            bitrateFlags = OPTION_FLAG_DISABLED
        endif
        oidBitrate = AddSliderOption("Bitrate (kbps)", MainQuest.VideoBitrate as float, "{0} kbps", bitrateFlags)

        oidKeyframeInterval = AddSliderOption("Keyframe Interval (s)", MainQuest.KeyframeInterval, "{1} s", videoFlags)
        oidEncoderPref = AddMenuOption("Encoder Preference", EncoderOptions()[MainQuest.EncoderPreference], videoFlags)
        oidRateControl = AddMenuOption("Rate Control", RateControlOptions()[MainQuest.RateControl], videoFlags)
        oidContainer = AddMenuOption("Container", ContainerOptions()[MainQuest.VideoContainer], videoFlags)
    endif
EndEvent

; ==============================================================================
; HIGHLIGHT — Info text
; ==============================================================================

Event OnOptionHighlight(int option)
    ; --- Settings page ---
    if (option == ImageTypeID)
        SetInfoText("Select the type of image to create")
    elseif (option == RemoveMenuID)
        SetInfoText("Toggle automatic removal of HUD/Menu during capture")
    elseif (option == KeyCodeID)
        SetInfoText("Select an unused key to take photo")
    elseif (option == PathID)
        SetInfoText("Enter path string\nMust be absolute and contain no illegal characters")
    elseif (option == JPG_CompressionID)
        SetInfoText("Quality factor for JPG: 0 (lowest) to 100 (highest)")
    elseif (option == UseJsonFileID)
        SetInfoText("Enable configuration save/restore via JSON file")
    elseif (option == DDS_ModeID)
        SetInfoText("Select DDS mode\nBC6h and BC7 variants take several minutes to process")
    elseif (option == DurationID)
        SetInfoText("Duration of animated capture in seconds")
    elseif (option == OptimizeID)
        SetInfoText("Enable transparency optimization for delta frames in AGIF/APNG\n0=off, 1=on")
    elseif (option == DeltaModeID)
        SetInfoText("Differential encoding mode for AGIF/APNG animation\n0=Off (full frames), 1=Region extraction, 2=True delta (with transparency)")
    elseif (option == Compression_ID)
        SetInfoText("PNG compression level: 0 (none) to 9 (maximum)\nApplies to PNG image type only")
    elseif (option == FPSID)
        SetInfoText("Number of frames to capture per second")
    elseif (option == LoopID)
        SetInfoText("Number of times to loop animated image\n0 = infinite looping")
    elseif (option == Quality_ID)
        SetInfoText("Quality factor for animated images (APNG/AGIF): 0 (low) to 100 (high)")

    ; --- Video Settings page: Capture ---
    elseif (option == oidVideoDuration)
        SetInfoText("How long to record. Maximum 120 seconds for H264 video.")
    elseif (option == oidResolution)
        SetInfoText("Output resolution (letterboxed). Lower = smaller files.")
    elseif (option == oidFrameRate)
        SetInfoText("Capture frame rate. 60 FPS doubles file size compared to 30 FPS.")

    ; --- Video Settings page: Encoding ---
    elseif (option == oidQualityPreset)
        SetInfoText("Presets auto-set bitrate. Choose Custom to set bitrate manually.")
    elseif (option == oidBitrate)
        SetInfoText("Manual bitrate in kbps. Only active when Quality = Custom and Rate Control is not CQP.")
    elseif (option == oidKeyframeInterval)
        SetInfoText("Seconds between keyframes. Lower = better seeking but larger files.")
    elseif (option == oidEncoderPref)
        SetInfoText("Auto picks the best available. Force Software if hardware encoder causes issues.")
    elseif (option == oidRateControl)
        SetInfoText("CBR = constant bitrate, VBR = variable (best quality/size), CQP = constant quality.")
    elseif (option == oidContainer)
        SetInfoText("MP4 is universally supported. MKV may be added in a future version.")
    endif
EndEvent

; ==============================================================================
; INPUT HANDLING (Path)
; ==============================================================================

Event OnOptionInputAccept(int a_option, string a_input)
    if (a_option == PathID)
        bool bRes = Printscreen_Formula_script.CheckPath(a_input)
        if (bRes)
            MainQuest.Path = a_input
            SetInputOptionValue(a_option, MainQuest.Path, false)
        else
            Debug.MessageBox("Path failed validation/creation. Please re-enter.")
            SetInputOptionValue(a_option, MainQuest.Path, false)
        endif
    endif
EndEvent

; ==============================================================================
; TOGGLE HANDLING
; ==============================================================================

Event OnOptionSelect(int option)
    if (option == RemoveMenuID)
        MainQuest.Menu = !MainQuest.Menu
        SetToggleOptionValue(option, MainQuest.Menu, false)
    elseif (option == UseJsonFileID)
        MainQuest.UseJsonFile = !MainQuest.UseJsonFile
        SetToggleOptionValue(option, MainQuest.UseJsonFile, false)
    endif
EndEvent

; ==============================================================================
; KEYMAP HANDLING
; ==============================================================================

Event OnOptionKeyMapChange(int option, int keyCode, string conflictControl, string conflictName)
    if (conflictName == "") && (conflictControl == "") && (Input.GetMappedControl(keyCode) == "")
        ; BUG FIX: RegisterForKey/UnregisterForKey are PER-SCRIPT-INSTANCE.
        ; The old code called UnregisterForKey here, which unregistered THIS
        ; MCM script (never registered in the first place — a no-op), then
        ; only assigned the property. MainQuest stayed registered for the OLD
        ; key while its OnKeyUp filtered for the NEW one, so after any remap
        ; neither key worked even though the MCM displayed the change as
        ; applied. UpdateHotkey performs unregister/set/register on MainQuest,
        ; the script that actually owns the OnKeyUp handler.
        MainQuest.UpdateHotkey(keyCode)
        SetKeyMapOptionValue(option, MainQuest.Key_TakePhoto, false)
    else
        Debug.MessageBox("Key conflict detected. Please choose an unassigned key.")
    endif
EndEvent

; ==============================================================================
; MENU HANDLING
; ==============================================================================

Event OnOptionMenuOpen(int option)
    ; --- Settings page menus ---
    if (option == ImageTypeID)
        int I = ImageArray().Find(MainQuest.ImageType)
        if (I < 0)
            I = 0
        endif
        SetMenuDialogOptions(ImageArray())
        SetMenuDialogDefaultIndex(0)
        SetMenuDialogStartIndex(I)

    elseif (option == TIF_ModeID)
        int I = TifArray().Find(MainQuest.Tif_Mode)
        if (I < 0)
            I = 0
        endif
        SetMenuDialogOptions(TifArray())
        SetMenuDialogDefaultIndex(0)
        SetMenuDialogStartIndex(I)

    elseif (option == DDS_ModeID)
        int I = DDSArray().Find(MainQuest.DDS_Mode)
        if (I < 0)
            I = 0
        endif
        SetMenuDialogOptions(DDSArray())
        SetMenuDialogDefaultIndex(0)
        SetMenuDialogStartIndex(I)

    ; --- Video Settings page: Capture ---
    elseif (option == oidResolution)
        SetMenuDialogOptions(ResolutionOptions())
        SetMenuDialogStartIndex(MainQuest.TargetResolution)
        SetMenuDialogDefaultIndex(0)

    elseif (option == oidFrameRate)
        SetMenuDialogOptions(FrameRateOptions())
        SetMenuDialogStartIndex(FrameRateToIndex())
        SetMenuDialogDefaultIndex(0)

    ; --- Video Settings page: Encoding ---
    elseif (option == oidQualityPreset)
        SetMenuDialogOptions(QualityPresetOptions())
        SetMenuDialogStartIndex(MainQuest.QualityPreset)
        SetMenuDialogDefaultIndex(2)

    elseif (option == oidEncoderPref)
        SetMenuDialogOptions(EncoderOptions())
        SetMenuDialogStartIndex(MainQuest.EncoderPreference)
        SetMenuDialogDefaultIndex(0)

    elseif (option == oidRateControl)
        SetMenuDialogOptions(RateControlOptions())
        SetMenuDialogStartIndex(MainQuest.RateControl)
        SetMenuDialogDefaultIndex(1)

    elseif (option == oidContainer)
        SetMenuDialogOptions(ContainerOptions())
        SetMenuDialogStartIndex(MainQuest.VideoContainer)
        SetMenuDialogDefaultIndex(0)

    else
        Debug.MessageBox("Invalid OptionID " + option)
    endif
EndEvent

Event OnOptionMenuAccept(int option, int index)
    ; --- Settings page ---
    if (option == ImageTypeID)
        MainQuest.ImageType = ImageArray()[index]
        SetMenuOptionValue(option, MainQuest.ImageType, false)
        EnableWidgetsForType(MainQuest.ImageType)

        ; Clamp durations when switching to/from animated/video types
        if (MainQuest.ImageType == "H264")
            MainQuest.ClampDurationForVideoMode()
        elseif (MainQuest.ImageType == "AGIF" || MainQuest.ImageType == "APNG")
            MainQuest.ClampDurationForAnimatedMode()
        endif

    elseif (option == TIF_ModeID)
        MainQuest.Tif_Mode = TifArray()[index]
        MainQuest.Mode = MainQuest.Tif_Mode
        SetMenuOptionValue(option, TifArray()[index])

    elseif (option == DDS_ModeID)
        MainQuest.DDS_Mode = DDSArray()[index]
        MainQuest.Mode = MainQuest.DDS_Mode
        SetMenuOptionValue(option, MainQuest.DDS_Mode)

    ; --- Video Settings page: Capture ---
    elseif (option == oidResolution)
        MainQuest.TargetResolution = index
        SetMenuOptionValue(oidResolution, ResolutionOptions()[index])

    elseif (option == oidFrameRate)
        if (index == 0)
            MainQuest.VideoFrameRate = 30
        else
            MainQuest.VideoFrameRate = 60
        endif
        SetMenuOptionValue(oidFrameRate, FrameRateOptions()[index])

    ; --- Video Settings page: Encoding ---
    elseif (option == oidQualityPreset)
        MainQuest.QualityPreset = index
        SetMenuOptionValue(oidQualityPreset, QualityPresetOptions()[index])
        ; Refresh to update bitrate enabled/disabled state
        ForcePageReset()

    elseif (option == oidEncoderPref)
        MainQuest.EncoderPreference = index
        SetMenuOptionValue(oidEncoderPref, EncoderOptions()[index])

    elseif (option == oidRateControl)
        MainQuest.RateControl = index
        SetMenuOptionValue(oidRateControl, RateControlOptions()[index])
        ; Refresh — CQP disables bitrate
        ForcePageReset()

    elseif (option == oidContainer)
        MainQuest.VideoContainer = index
        SetMenuOptionValue(oidContainer, ContainerOptions()[index])
    endif
EndEvent

; ==============================================================================
; SLIDER HANDLING
; ==============================================================================

Event OnOptionSliderOpen(int a_option)
    ; --- Settings page sliders ---
    if (a_option == JPG_CompressionID)
        SetSliderOptionValue(a_option, MainQuest.JPG_Compression, "{0}", false)
        SetSliderDialogStartValue(MainQuest.JPG_Compression)
        SetSliderDialogDefaultValue(85.0)
        SetSliderDialogRange(0.0, 100.0)
        SetSliderDialogInterval(5.0)

    elseif (a_option == Compression_ID)
        SetSliderOptionValue(a_option, MainQuest.Compression as float, "{0}", false)
        SetSliderDialogStartValue(MainQuest.Compression as float)
        SetSliderDialogDefaultValue(9.0)
        SetSliderDialogRange(0.0, 9.0)
        SetSliderDialogInterval(1.0)

    elseif (a_option == DurationID)
        SetSliderOptionValue(a_option, MainQuest.Duration, "{0}", false)
        SetSliderDialogStartValue(MainQuest.Duration)
        SetSliderDialogDefaultValue(5.0)
        SetSliderDialogRange(1.0, 30.0)
        SetSliderDialogInterval(1.0)

    elseif (a_option == FPSID)
        SetSliderOptionValue(a_option, MainQuest.Fps, "{0}", false)
        SetSliderDialogStartValue(MainQuest.Fps)
        SetSliderDialogDefaultValue(10.0)
        SetSliderDialogRange(1.0, 30.0)
        SetSliderDialogInterval(1.0)

    elseif (a_option == LoopID)
        SetSliderOptionValue(a_option, MainQuest.LoopCount as float, "{0}", false)
        SetSliderDialogStartValue(MainQuest.LoopCount as float)
        SetSliderDialogDefaultValue(0.0)
        SetSliderDialogRange(0.0, 10.0)
        SetSliderDialogInterval(1.0)

    elseif (a_option == OptimizeID)
        SetSliderOptionValue(a_option, MainQuest.Optimize as float, "{0}", false)
        SetSliderDialogStartValue(MainQuest.Optimize as float)
        SetSliderDialogDefaultValue(1.0)
        SetSliderDialogRange(0.0, 1.0)
        SetSliderDialogInterval(1.0)

    elseif (a_option == DeltaModeID)
        SetSliderOptionValue(a_option, MainQuest.DeltaMode as float, "{0}", false)
        SetSliderDialogStartValue(MainQuest.DeltaMode as float)
        SetSliderDialogDefaultValue(0.0)
        SetSliderDialogRange(0.0, 2.0)
        SetSliderDialogInterval(1.0)

    elseif (a_option == Quality_ID)
        SetSliderOptionValue(a_option, MainQuest.Quality * 100.0, "{0}", false)
        SetSliderDialogStartValue(MainQuest.Quality * 100.0)
        SetSliderDialogDefaultValue(85.0)
        SetSliderDialogRange(0.0, 100.0)
        SetSliderDialogInterval(5.0)

    ; --- Video Settings page: Capture ---
    elseif (a_option == oidVideoDuration)
        SetSliderDialogStartValue(MainQuest.VideoDuration)
        SetSliderDialogDefaultValue(10.0)
        SetSliderDialogRange(1.0, 120.0)
        SetSliderDialogInterval(0.5)

    ; --- Video Settings page: Encoding ---
    elseif (a_option == oidBitrate)
        SetSliderDialogStartValue(MainQuest.VideoBitrate as float)
        SetSliderDialogDefaultValue(8000.0)
        SetSliderDialogRange(1000.0, 50000.0)
        SetSliderDialogInterval(500.0)

    elseif (a_option == oidKeyframeInterval)
        SetSliderDialogStartValue(MainQuest.KeyframeInterval)
        SetSliderDialogDefaultValue(2.0)
        SetSliderDialogRange(0.5, 10.0)
        SetSliderDialogInterval(0.5)
    endif
EndEvent

Event OnOptionSliderAccept(int a_option, float a_value)
    ; --- Settings page ---
    if (a_option == JPG_CompressionID)
        MainQuest.JPG_Compression = a_value
        SetSliderOptionValue(a_option, MainQuest.JPG_Compression, "{0}", false)

    elseif (a_option == Compression_ID)
        MainQuest.Compression = a_value as int
        SetSliderOptionValue(a_option, MainQuest.Compression as float, "{0}", false)

    elseif (a_option == DurationID)
        MainQuest.Duration = a_value
        SetSliderOptionValue(a_option, MainQuest.Duration, "{0}", false)

    elseif (a_option == FPSID)
        MainQuest.Fps = a_value
        SetSliderOptionValue(a_option, MainQuest.Fps, "{0}", false)

    elseif (a_option == LoopID)
        MainQuest.LoopCount = a_value as int
        SetSliderOptionValue(a_option, MainQuest.LoopCount as float, "{0}", false)

    elseif (a_option == OptimizeID)
        MainQuest.Optimize = a_value as int
        SetSliderOptionValue(a_option, MainQuest.Optimize as float, "{0}", false)

    elseif (a_option == DeltaModeID)
        MainQuest.DeltaMode = a_value as int
        SetSliderOptionValue(a_option, MainQuest.DeltaMode as float, "{0}", false)

    elseif (a_option == Quality_ID)
        MainQuest.Quality = a_value / 100.0
        SetSliderOptionValue(a_option, MainQuest.Quality * 100.0, "{0}", false)

    ; --- Video Settings page: Capture ---
    elseif (a_option == oidVideoDuration)
        MainQuest.VideoDuration = a_value
        SetSliderOptionValue(oidVideoDuration, MainQuest.VideoDuration, "{1} s")

    ; --- Video Settings page: Encoding ---
    elseif (a_option == oidBitrate)
        int clamped = a_value as int
        if (clamped < 1000)
            clamped = 1000
        elseif (clamped > 50000)
            clamped = 50000
        endif
        MainQuest.VideoBitrate = clamped
        SetSliderOptionValue(oidBitrate, MainQuest.VideoBitrate as float, "{0} kbps")

    elseif (a_option == oidKeyframeInterval)
        MainQuest.KeyframeInterval = a_value
        SetSliderOptionValue(oidKeyframeInterval, MainQuest.KeyframeInterval, "{1} s")
    endif
EndEvent
