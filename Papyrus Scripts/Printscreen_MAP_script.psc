Scriptname Printscreen_MAP_script extends Quest

Printscreen_MainQuest_script Property MainQuest Auto

; Pure-Papyrus replacement for the former JContainers JIntMap lookup.
; Preserves the existing GetKeyName(Int KeyCode) interface.
String Function GetKeyName(Int KeyCode) Global
    If KeyCode == 1
        Return "Escape"
    ElseIf KeyCode == 2
        Return "1"
    ElseIf KeyCode == 3
        Return "2"
    ElseIf KeyCode == 4
        Return "3"
    ElseIf KeyCode == 5
        Return "4"
    ElseIf KeyCode == 6
        Return "5"
    ElseIf KeyCode == 7
        Return "6"
    ElseIf KeyCode == 8
        Return "7"
    ElseIf KeyCode == 9
        Return "8"
    ElseIf KeyCode == 10
        Return "9"
    ElseIf KeyCode == 11
        Return "0"
    ElseIf KeyCode == 12
        Return "Minus"
    ElseIf KeyCode == 13
        Return "Equals"
    ElseIf KeyCode == 14
        Return "Backspace"
    ElseIf KeyCode == 15
        Return "Tab"
    ElseIf KeyCode == 16
        Return "Q"
    ElseIf KeyCode == 17
        Return "W"
    ElseIf KeyCode == 18
        Return "E"
    ElseIf KeyCode == 19
        Return "R"
    ElseIf KeyCode == 20
        Return "T"
    ElseIf KeyCode == 21
        Return "Y"
    ElseIf KeyCode == 22
        Return "U"
    ElseIf KeyCode == 23
        Return "I"
    ElseIf KeyCode == 24
        Return "O"
    ElseIf KeyCode == 25
        Return "P"
    ElseIf KeyCode == 26
        Return "Left Bracket"
    ElseIf KeyCode == 27
        Return "Right Bracket"
    ElseIf KeyCode == 28
        Return "Enter"
    ElseIf KeyCode == 29
        Return "Left Control"
    ElseIf KeyCode == 30
        Return "A"
    ElseIf KeyCode == 31
        Return "S"
    ElseIf KeyCode == 32
        Return "D"
    ElseIf KeyCode == 33
        Return "F"
    ElseIf KeyCode == 34
        Return "G"
    ElseIf KeyCode == 35
        Return "H"
    ElseIf KeyCode == 36
        Return "J"
    ElseIf KeyCode == 37
        Return "K"
    ElseIf KeyCode == 38
        Return "L"
    ElseIf KeyCode == 39
        Return "Semicolon"
    ElseIf KeyCode == 40
        Return "Apostrophe"
    ElseIf KeyCode == 41
        Return "~ (Console)"
    ElseIf KeyCode == 42
        Return "Left Shift"
    ElseIf KeyCode == 43
        Return "Back Slash"
    ElseIf KeyCode == 44
        Return "Z"
    ElseIf KeyCode == 45
        Return "X"
    ElseIf KeyCode == 46
        Return "C"
    ElseIf KeyCode == 47
        Return "V"
    ElseIf KeyCode == 48
        Return "B"
    ElseIf KeyCode == 49
        Return "N"
    ElseIf KeyCode == 50
        Return "M"
    ElseIf KeyCode == 51
        Return "Comma"
    ElseIf KeyCode == 52
        Return "Period"
    ElseIf KeyCode == 53
        Return "Forward Slash"
    ElseIf KeyCode == 54
        Return "Right Shift"
    ElseIf KeyCode == 55
        Return "NUM*"
    ElseIf KeyCode == 56
        Return "Left Alt"
    ElseIf KeyCode == 57
        Return "Spacebar"
    ElseIf KeyCode == 58
        Return "Caps Lock"
    ElseIf KeyCode == 59
        Return "F1"
    ElseIf KeyCode == 60
        Return "F2"
    ElseIf KeyCode == 61
        Return "F3"
    ElseIf KeyCode == 62
        Return "F4"
    ElseIf KeyCode == 63
        Return "F5"
    ElseIf KeyCode == 64
        Return "F6"
    ElseIf KeyCode == 65
        Return "F7"
    ElseIf KeyCode == 66
        Return "F8"
    ElseIf KeyCode == 67
        Return "F9"
    ElseIf KeyCode == 68
        Return "F10"
    ElseIf KeyCode == 69
        Return "Num Lock"
    ElseIf KeyCode == 70
        Return "Scroll Lock"
    ElseIf KeyCode == 71
        Return "NUM7"
    ElseIf KeyCode == 72
        Return "NUM8"
    ElseIf KeyCode == 73
        Return "NUM9"
    ElseIf KeyCode == 74
        Return "NUM-"
    ElseIf KeyCode == 75
        Return "NUM4"
    ElseIf KeyCode == 76
        Return "NUM5"
    ElseIf KeyCode == 77
        Return "NUM6"
    ElseIf KeyCode == 78
        Return "NUM+"
    ElseIf KeyCode == 79
        Return "NUM1"
    ElseIf KeyCode == 80
        Return "NUM2"
    ElseIf KeyCode == 81
        Return "NUM3"
    ElseIf KeyCode == 82
        Return "NUM0"
    ElseIf KeyCode == 83
        Return "NUM."
    ElseIf KeyCode == 87
        Return "F11"
    ElseIf KeyCode == 88
        Return "F12"
    ElseIf KeyCode == 156
        Return "NUM Enter"
    ElseIf KeyCode == 157
        Return "Right Control"
    ElseIf KeyCode == 181
        Return "NUM/"
    ElseIf KeyCode == 183
        Return "0"
    ElseIf KeyCode == 184
        Return "Right Alt"
    ElseIf KeyCode == 197
        Return "Pause"
    ElseIf KeyCode == 199
        Return "Home"
    ElseIf KeyCode == 200
        Return "Up Arrow"
    ElseIf KeyCode == 201
        Return "PgUp"
    ElseIf KeyCode == 203
        Return "Left Arrow"
    ElseIf KeyCode == 205
        Return "Right Arrow"
    ElseIf KeyCode == 207
        Return "End"
    ElseIf KeyCode == 208
        Return "Down Arrow"
    ElseIf KeyCode == 209
        Return "PgDown"
    ElseIf KeyCode == 210
        Return "Insert"
    ElseIf KeyCode == 211
        Return "Delete"
    ElseIf KeyCode == 256
        Return "Left Mouse Button"
    ElseIf KeyCode == 257
        Return "Right Mouse Button"
    ElseIf KeyCode == 258
        Return "Middle/Wheel Mouse Button"
    ElseIf KeyCode == 259
        Return "Mouse Button 3"
    ElseIf KeyCode == 260
        Return "Mouse Button 4"
    ElseIf KeyCode == 261
        Return "Mouse Button 5"
    ElseIf KeyCode == 262
        Return "Mouse Button 6"
    ElseIf KeyCode == 263
        Return "Mouse Button 7"
    ElseIf KeyCode == 264
        Return "Mouse Wheel Up"
    ElseIf KeyCode == 265
        Return "Mouse Wheel Down"
    EndIf

    Return "default"
EndFunction

; True when KeyCode is one of the codes GetKeyName() actually maps.
; The valid codes are not contiguous - keyboard 1-211, mouse 256-263,
; wheel 264-265, with gaps throughout - so a range check cannot detect a
; bogus value and the map itself is the only reliable test. Used by
; Printscreen_MainQuest_script.Validate_Key_TakePhoto() to reject codes
; typed straight into the JSON file rather than bound through the MCM.
Bool Function IsValidKeyCode(Int KeyCode) Global
    Return GetKeyName(KeyCode) != "default"
EndFunction
