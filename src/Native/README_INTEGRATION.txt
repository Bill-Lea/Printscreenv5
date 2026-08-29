PRINTSCREEN V4 - NATIVE JSON INTEGRATION

1. Add these files to the C++ project:
       PrintscreenJson.h
       PrintscreenJson.cpp

2. In Bindings.cpp add:
       #include "PrintscreenJson.h"

3. In PapyrusBindings::Register(), after the existing Printscreen_Formula_script
   registrations, call:

       if (!PrintscreenJson::Register(vm)) {
           logger::error("Failed to register Printscreen JSON functions");
           ++fail;
       }

   NOTE:
   PrintscreenJson::Register() registers a second Papyrus script name:
       Printscreen_JSON_script

4. Add/compile this Papyrus source:
       Printscreen_JSON_script.psc

5. Replace the current Papyrus sources with:
       Printscreen_MainQuest_script_NATIVE_JSON.psc
       Printscreen_MCM_script_NATIVE_JSON.psc

   Rename them back to their original filenames before compiling:
       Printscreen_MainQuest_script.psc
       Printscreen_MCM_script.psc

6. PapyrusUtil is no longer needed by these two scripts.

7. Existing JSON compatibility:
   The native implementation deliberately reads/writes:
       Data/SKSE/Plugins/StorageUtilData/PrintScreen.json

   This is the same base folder used by PapyrusUtil JsonUtil, so an existing
   PrintScreen.json can be reused.

FUNCTIONS PROVIDED

File:
  JsonExists(filename)
  IsGood(filename)
  GetErrors(filename)
  Load(filename)
  Save(filename)

Integer:
  SetIntValue(filename, key, value)
  GetIntValue(filename, key, missing=0)
  HasIntValue(filename, key)

Float:
  SetFloatValue(filename, key, value)
  GetFloatValue(filename, key, missing=0.0)
  HasFloatValue(filename, key)

String:
  SetStringValue(filename, key, value)
  GetStringValue(filename, key, missing="")
  HasStringValue(filename, key)

BEHAVIOR

- JsonExists verifies that the file physically exists.
- IsGood reparses the current file from disk and verifies:
    * valid JSON syntax
    * top-level JSON value is an object
- Has* verifies that a key exists and has the expected type.
- Get* returns the supplied default if the file/key/type is invalid.
- Set* updates an in-memory object.
- Save writes the complete JSON object.
- Save writes a temporary file first and replaces the destination only after
  the temporary write has completed.
- A missing or corrupt JSON file can be recreated by the existing WriteJson().
