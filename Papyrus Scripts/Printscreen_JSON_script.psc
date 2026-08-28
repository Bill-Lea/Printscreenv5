Scriptname Printscreen_JSON_script Hidden

; Printscreen-native replacement for the subset of PapyrusUtil/JsonUtil used
; by Printscreen V4.
;
; Files are stored in:
;   Data/SKSE/Plugins/StorageUtilData/
;
; If no .json extension is supplied, the DLL appends it automatically.

Bool Function JsonExists(String FileName) Global Native
Bool Function IsGood(String FileName) Global Native
String Function GetErrors(String FileName) Global Native

; Force the cached copy to be re-read from disk.
Bool Function Load(String FileName) Global Native

; Write the current cached object to disk.
Bool Function Save(String FileName) Global Native

Int Function SetIntValue(String FileName, String KeyName, Int Value) Global Native
Float Function SetFloatValue(String FileName, String KeyName, Float Value) Global Native
String Function SetStringValue(String FileName, String KeyName, String Value) Global Native

Int Function GetIntValue(String FileName, String KeyName, Int Missing = 0) Global Native
Float Function GetFloatValue(String FileName, String KeyName, Float Missing = 0.0) Global Native
String Function GetStringValue(String FileName, String KeyName, String Missing = "") Global Native

Bool Function HasIntValue(String FileName, String KeyName) Global Native
Bool Function HasFloatValue(String FileName, String KeyName) Global Native
Bool Function HasStringValue(String FileName, String KeyName) Global Native
