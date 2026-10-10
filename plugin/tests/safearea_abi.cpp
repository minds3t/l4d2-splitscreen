#include "../src/safearea.cpp"
#include "assertions_enabled.h"
#include <cassert>
#include <cstdio>

bool bridgeEnabled = true;
bool menuContext = true;
bool ownedSession = true;
void* fakeFramework = nullptr;
void* nativeSession = nullptr;
int menuReloads = 0;
int hudReloads = 0;
namespace bridge {
void Log(const char*, ...) {
    // Deliberately disturb floating-point state inside the ownership query.
    // The instruction-site hook must preserve more than the C++ ABI requires.
    __asm { fninit }
    __asm { pxor xmm0, xmm0 }
    __asm { pxor xmm7, xmm7 }
}
HMODULE GameUI() { return nullptr; }
HMODULE Client() { return nullptr; }
void* Framework() { return fakeFramework; }
bool IsLocalSplitEnabled() { return bridgeEnabled; }
bool InstallHook(const char*, HMODULE, uint32_t, const uint8_t*, size_t, void*, void**) { return false; }
}
namespace lobby {
bool UseConsoleMenuPresentation() { return bridgeEnabled && menuContext; }
bool HasOwnedOfflineSession() { return ownedSession; }
}

alignas(16) unsigned char callerFx[512]{};
alignas(16) unsigned char beforeFx[512]{};
alignas(16) unsigned char afterFx[512]{};
alignas(16) const unsigned int xmmSentinel[4] = {0x10203040,0x55667788,0x99aabbcc,0xddeeff00};
unsigned int observed[7]{};
unsigned int beforeFlags = 0, afterFlags = 0, beforeStack = 0, afterStack = 0;
int chosenPath = 0;
void* testedGate = nullptr;

__declspec(naked) void SafePath() {
    __asm { mov chosenPath, 1 }
    __asm { ret }
}
__declspec(naked) void OriginalPath() {
    // Stand in for the first instruction of the MinHook trampoline: inspect
    // state before the native comparison legitimately changes arithmetic flags.
    __asm { mov chosenPath, 2 }
    __asm { ret }
}
__declspec(naked) void RunGate() {
    __asm {
        pushfd
        pushad
        fxsave callerFx
        fninit
        fld1
        fldpi
        movaps xmm0, xmmSentinel
        movaps xmm1, xmmSentinel
        movaps xmm2, xmmSentinel
        movaps xmm3, xmmSentinel
        movaps xmm4, xmmSentinel
        movaps xmm5, xmmSentinel
        movaps xmm6, xmmSentinel
        movaps xmm7, xmmSentinel
        fxsave beforeFx
        mov eax, 11223344h
        mov ecx, 22334455h
        mov edx, 33445566h
        mov ebx, 44556677h
        mov ebp, 55667788h
        mov esi, 66778899h
        mov edi, 778899aah
        push 246h
        popfd
        pushfd
        pop beforeFlags
        mov beforeStack, esp
        call dword ptr [testedGate]
        mov afterStack, esp
        mov observed[0], eax
        mov observed[4], ecx
        mov observed[8], edx
        mov observed[12], ebx
        mov observed[16], ebp
        mov observed[20], esi
        mov observed[24], edi
        pushfd
        pop afterFlags
        fxsave afterFx
        fxrstor callerFx
        popad
        popfd
        ret
    }
}

void Verify(void* gate, int path) {
    testedGate = gate;
    chosenPath = 0;
    safearea::menuLogged = false;
    safearea::hudLogged = false;
    RunGate();
    const unsigned int expected[] = {0x11223344,0x22334455,0x33445566,0x44556677,
                                    0x55667788,0x66778899,0x778899aa};
    assert(chosenPath == path);
    assert(std::memcmp(observed, expected, sizeof(expected)) == 0);
    assert(beforeStack == afterStack && beforeFlags == afterFlags);
    assert(std::memcmp(beforeFx, afterFx, sizeof(beforeFx)) == 0);
}

void* __fastcall GetMatchSession(void*, void*) { return nativeSession; }
void __cdecl ReloadMenu() { ++menuReloads; }
void __cdecl ReloadHud() {
    ++hudReloads;
    safearea::Refresh(); // The guard rejects synchronous callback reentry.
}

void VerifyRefreshLifecycle() {
    auto menu = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x222000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    auto client = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x890000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(menu && client);
    void* table[10]{};
    table[9] = reinterpret_cast<void*>(&GetMatchSession);
    void* object[1]{table};
    fakeFramework = object;
    *reinterpret_cast<void**>(menu + 0x221504) = object;
    *reinterpret_cast<void**>(menu + 0x1ffb10) = object;
    *reinterpret_cast<void**>(client + 0x88b7ec) = object;
    *reinterpret_cast<void**>(client + 0x735e3c) = object;
    safearea::installed = true;
    safearea::menuBase = reinterpret_cast<std::uintptr_t>(menu);
    safearea::clientBase = reinterpret_cast<std::uintptr_t>(client);
    safearea::reloadMenu = &ReloadMenu;
    safearea::reloadHud = &ReloadHud;
    safearea::menuLogged = safearea::hudLogged = true;
    bridgeEnabled = true;
    safearea::Refresh();
    assert(!menuReloads && !hudReloads); // Re-enable cannot reconstruct a stale lobby.
    bridgeEnabled = false;
    safearea::Refresh();
    assert(!menuReloads && hudReloads == 1); // No session: HUD only.
    nativeSession = object;
    safearea::Refresh();
    assert(menuReloads == 1 && hudReloads == 2 && !safearea::refreshing);
    safearea::menuLogged = safearea::hudLogged = false;
    safearea::Refresh();
    assert(menuReloads == 1 && hudReloads == 2); // No changed contexts to restore.
    safearea::menuLogged = safearea::hudLogged = true;
    fakeFramework = nullptr;
    *reinterpret_cast<void**>(client + 0x735e3c) = nullptr;
    safearea::Refresh();
    assert(menuReloads == 1 && hudReloads == 2); // Unavailable interfaces are skipped.
    safearea::Shutdown();
    safearea::Refresh();
    assert(menuReloads == 1 && hudReloads == 2);
    VirtualFree(menu, 0, MEM_RELEASE);
    VirtualFree(client, 0, MEM_RELEASE);
    nativeSession = nullptr;
}

int main() {
    assert(!safearea::Install()); // An unverified/missing module cannot install.
    safearea::menuSafePath = reinterpret_cast<void*>(&SafePath);
    safearea::hudSafePath = reinterpret_cast<void*>(&SafePath);
    safearea::originalMenuGate = reinterpret_cast<void*>(&OriginalPath);
    safearea::originalHudGate = reinterpret_cast<void*>(&OriginalPath);
    // The user's SafeArea setting (splitscreen_support.ini) switches both branches off.
    for (int wanted = 0; wanted < 2; ++wanted)
    for (int installed = 0; installed < 2; ++installed)
    for (int enabled = 0; enabled < 2; ++enabled)
    for (int menu = 0; menu < 2; ++menu)
    for (int owned = 0; owned < 2; ++owned) {
        settings::values.safeArea = wanted != 0;
        safearea::installed = installed != 0;
        bridgeEnabled = enabled != 0;
        menuContext = menu != 0;
        ownedSession = owned != 0;
        Verify(reinterpret_cast<void*>(&safearea::MenuGateHook), wanted && installed && enabled && menu ? 1 : 2);
        Verify(reinterpret_cast<void*>(&safearea::HudGateHook), wanted && installed && enabled && owned ? 1 : 2);
    }
    safearea::Shutdown();
    assert(!safearea::installed && !safearea::originalMenuGate && !safearea::originalHudGate);
    safearea::Refresh(); // No callback after shutdown.
    VerifyRefreshLifecycle();
    std::puts("safearea ABI: 64 scoped dispatches preserve GPRs, flags, stack, x87 and SSE state");
    std::puts("safearea lifecycle: enable is inert; disable requires live contexts; reentry and shutdown are inert");
}
