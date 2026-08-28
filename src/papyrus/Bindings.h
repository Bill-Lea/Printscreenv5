#pragma once

namespace PapyrusBindings {
    bool Register(RE::BSScript::IVirtualMachine* vm);
    void Initialize();
    void OnDataLoaded();
    void OnPostLoadGame();
}
