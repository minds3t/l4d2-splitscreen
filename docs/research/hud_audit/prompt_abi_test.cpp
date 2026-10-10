#include "../../../plugin/src/hud_prompts.cpp"
#include <cassert>
#include <cstdio>

bool enabled = true, owned = true;
int forwarded = 0, lastSlot = -1, lastDevice = -2;
int childQueries = 0;
namespace bridge {
void Log(const char*, ...) {}
HMODULE Client() { return nullptr; }
HMODULE Engine() { return nullptr; }
HMODULE InputSystem() { return nullptr; }
bool IsLocalSplitEnabled() { return enabled; }
bool InstallHook(const char*, HMODULE, uint32_t, const uint8_t*, size_t, void*, void**) { return false; }
}
namespace lobby { bool HasOwnedOfflineSession() { return owned; } }

const char* __fastcall Forward(void*, void*, const char*, int slot, int, int device) {
    ++forwarded; lastSlot = slot; lastDevice = device; return "native";
}
int __cdecl KeySlot(int code) { return code >= 0x92 ? 1 : 0; }
const char* __cdecl MockMatch(int code, const char* command) {
    if (!std::strcmp(command, "attack")) {
        if (code == 107) return "MOUSE1";
        if (code == 120) return "R_TRIGGER";
        if (code == 160) return "X_BUTTON";
    }
    if (!std::strcmp(command, "toggle_duck") && code == 121) return "STICK1";
    return nullptr;
}
__declspec(naked) const char* NativeMatcher() {
    __asm {
        push dword ptr [esp + 4]
        push edi
        call MockMatch
        add esp, 8
        ret
    }
}
void* __fastcall FindNothing(void*, void*, const char*, bool) { ++childQueries; return nullptr; }

int main() {
    using namespace hud_prompts;
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x75b000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(memory);
    client = reinterpret_cast<std::uintptr_t>(memory);
    unsigned char input[0x3e34]{};
    unsigned char parent[0x34]{};
    const char* variableName = "in_forceuser";
    *reinterpret_cast<void**>(memory + 0x75a4a8 + 0x1c) = parent;
    *reinterpret_cast<const char**>(parent + 0xc) = variableName;
    *reinterpret_cast<int*>(parent + 0x30) = 1;
    *reinterpret_cast<int*>(input + 0x3bf4) = 0;
    input[0x3bfc] = 1;
    inputSystem = input;
    engineClient = parent;
    installed = true;
    originalLookup = reinterpret_cast<LookupFn>(&Forward);
    keySlot = &KeySlot;
    matchBinding = reinterpret_cast<void*>(&NativeMatcher);
    const auto caller = client + 0x2d68ec;
    assert(!std::strcmp(SelectBinding(parent, "attack", 0, 0, -1, caller), "R_TRIGGER"));
    assert(!std::strcmp(SelectBinding(parent, "attack", 1, 0, -1, caller), "MOUSE1"));
    // A second connected pad uses its own binding, not the first pad's trigger.
    *reinterpret_cast<int*>(input + 0x3bf4 + 0x90) = 1;
    input[0x3bfc + 0x90] = 1;
    assert(!std::strcmp(SelectBinding(parent, "attack", 1, 0, -1, caller), "X_BUTTON"));
    assert(!std::strcmp(SelectBinding(parent, "duck", 0, 0, -1, caller), "STICK1"));
    input[0x3bfc + 0x90] = 0;
    assert(!SelectBinding(parent, "duck", 1, 0, -1, caller));
    assert(!SelectBinding(parent, "unbound", 0, 0, -1, caller));
    assert(!forwarded);
    assert(!std::strcmp(SelectBinding(parent, "attack", 0, 0, -1, caller + 1), "native"));
    enabled = false;
    assert(!std::strcmp(SelectBinding(parent, "attack", 0, 0, -1, caller), "native"));
    enabled = true; owned = false;
    assert(!std::strcmp(SelectBinding(parent, "attack", 0, 0, -1, caller), "native"));
    owned = true;
    assert(!std::strcmp(SelectBinding(parent, "attack", 3, 0, -1, caller), "native"));
    assert(lastSlot == 3 && lastDevice == -1 && forwarded == 4);
    unsigned char vote[0x1dc]{};
    *reinterpret_cast<std::uintptr_t*>(vote) = client + 0x5dab34;
    *reinterpret_cast<std::uintptr_t*>(vote + 0x17c) = client + 0x5daae4;
    *reinterpret_cast<int*>(vote + 0x190) = 3;
    findChild = reinterpret_cast<FindChildFn>(&FindNothing);
    SelectVoteLabels(vote);
    assert(!childQueries);
    *reinterpret_cast<int*>(vote + 0x190) = 0;
    owned = false;
    SelectVoteLabels(vote);
    assert(!childQueries);
    owned = true; enabled = false;
    SelectVoteLabels(vote);
    assert(!childQueries);
    enabled = true;
    *reinterpret_cast<std::uintptr_t*>(vote) = 0;
    SelectVoteLabels(vote);
    assert(!childQueries);
    Shutdown();
    assert(!installed && !originalLookup && !originalVoteStart && !matchBinding && !findChild);
    VirtualFree(memory, 0, MEM_RELEASE);
    std::puts("CBindPanel routing/EDI matcher ABI smoke test passed");
}
