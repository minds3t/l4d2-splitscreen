#include "bridge.h"
#include "vendor/minhook/include/MinHook.h"
#include <cstring>
#include <vector>

namespace {
bool initialized = false;
std::vector<void*> targets;
}
namespace bridge {
bool InstallHook(const char* label, HMODULE module, uint32_t rva,
                 const uint8_t* expected, size_t expectedSize,
                 void* detour, void** original) {
    if (!IsLocalSplitEnabled() || !module || !expected || expectedSize < 5 || !original) {
        Log("Hook %s refused: missing opt-in, module, or verification bytes", label);
        return false;
    }
    auto target = reinterpret_cast<uint8_t*>(module) + rva;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(module) + dos->e_lfanew);
    if (rva >= nt->OptionalHeader.SizeOfImage || expectedSize > nt->OptionalHeader.SizeOfImage - rva ||
        std::memcmp(target, expected, expectedSize)) {
        Log("Hook %s refused: expected bytes differ at +%08x", label, rva);
        return false;
    }
    if (!initialized) {
        const auto status = MH_Initialize();
        if (status != MH_OK) { Log("MinHook initialize failed: %s", MH_StatusToString(status)); return false; }
        initialized = true;
    }
    auto status = MH_CreateHook(target, detour, original);
    if (status == MH_OK) {
        status = MH_EnableHook(target);
        if (status != MH_OK) MH_RemoveHook(target);
    }
    if (status != MH_OK) { Log("Hook %s failed: %s", label, MH_StatusToString(status)); return false; }
    targets.push_back(target);
    Log("Hook %s installed at +%08x", label, rva);
    return true;
}
void RemoveHooks() {
    for (auto target : targets) MH_DisableHook(target);
    for (auto target : targets) MH_RemoveHook(target);
    targets.clear();
    if (initialized) MH_Uninitialize();
    initialized = false;
}
}
