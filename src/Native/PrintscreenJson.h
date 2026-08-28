#pragma once

#include "RE/Skyrim.h"

namespace PrintscreenJson
{
    // Register the native functions exposed by Printscreen_JSON_script.psc.
    bool Register(RE::BSScript::IVirtualMachine* vm);
}
