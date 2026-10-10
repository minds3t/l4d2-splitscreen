#include "safearea.h"
#include "readable.h"
#include "build_10097.h"
#include "bridge.h"
#include "lobby.h"
#include "settings.h"

#include <cstring>

namespace safearea {
namespace {
void* originalMenuGate = nullptr;
void* originalHudGate = nullptr;
void* menuSafePath = nullptr;
void* hudSafePath = nullptr;
using ReloadFn = void(__cdecl*)();
ReloadFn reloadMenu = nullptr;
ReloadFn reloadHud = nullptr;
std::uintptr_t menuBase = 0;
std::uintptr_t clientBase = 0;
bool installed = false;
bool menuLogged = false;
bool hudLogged = false;
bool refreshing = false;

using bridge::Readable;

bool ObjectAt(std::uintptr_t address) {
    if (!Readable(reinterpret_cast<void*>(address), sizeof(void*))) return false;
    void* object = *reinterpret_cast<void**>(address);
    return Readable(object, sizeof(void*)) &&
        Readable(*static_cast<void***>(object), sizeof(void*));
}

bool HasLiveMatchSession() {
    // A surviving GameLobby frame is not evidence that its session still
    // exists. Its resource reload dereferences GetMatchSession() without a
    // null check (GameUI+50091). Query the same verified framework slot first.
    void* framework = bridge::Framework();
    if (!Readable(framework, sizeof(void*))) return false;
    void** table = *static_cast<void***>(framework);
    if (!Readable(table, 10 * sizeof(void*)) || !Readable(table[9], 1)) return false;
    return bridge::Virtual<void*(__thiscall*)(void*)>(framework, 9)(framework) != nullptr;
}

bool __cdecl UseMenuSafeArea() {
    const bool use = installed && settings::values.safeArea && lobby::UseConsoleMenuPresentation();
    if (use && !menuLogged) {
        bridge::Log("[safearea] Native Xbox menu title-safe branch active for flagged resources; global test cvar unchanged");
        menuLogged = true;
    }
    return use;
}

bool __cdecl UseHudSafeArea() {
    const bool use = installed && settings::values.safeArea && bridge::IsLocalSplitEnabled() &&
        lobby::HasOwnedOfflineSession();
    if (use && !hudLogged) {
        bridge::Log("[safearea] Native Xbox HUD title-safe branch active: 5%%/7.5%% of full surface, interior split edges excluded");
        hudLogged = true;
    }
    return use;
}

// Both sites are inside Panel::ApplySettings, immediately before the PC-only
// test-cvar gate. ESI is the panel, EBX its resource and EDI zero. Preserve all
// registers, flags, x87 and SSE state around our ownership query. The owned
// path enters the original Xbox calculation; the other path executes the
// original cmp/jz trampoline, respecting the user's existing test-cvar value.
__declspec(naked) void MenuGateHook() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 210h
        and esp, 0fffffff0h
        fxsave [esp]
        call UseMenuSafeArea
        test al, al
        jz native_path
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [menuSafePath]
    native_path:
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [originalMenuGate]
    }
}

__declspec(naked) void HudGateHook() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 210h
        and esp, 0fffffff0h
        fxsave [esp]
        call UseHudSafeArea
        test al, al
        jz native_path
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [hudSafePath]
    native_path:
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [originalHudGate]
    }
}

bool Prefix(std::uintptr_t address, const unsigned char* bytes, std::size_t size) {
    return Readable(reinterpret_cast<void*>(address), size) &&
        std::memcmp(reinterpret_cast<void*>(address), bytes, size) == 0;
}
}

bool Install() {
    if (installed) return true;
    // These accessors are null unless their full researched file hashes match.
    const auto ui = bridge::GameUI();
    const auto client = bridge::Client();
    if (!ui || !client || !bridge::IsLocalSplitEnabled()) return false;
    const auto uiBase = reinterpret_cast<std::uintptr_t>(ui);
    const auto hudBase = reinterpret_cast<std::uintptr_t>(client);
    static constexpr unsigned char menuReloadPrefix[] = {0x55,0x8b,0xec,0x51};
    static constexpr unsigned char hudReloadPrefix[] = {0x55,0x8b,0xec,0x83,0xec,0x24};
    if (!Prefix(uiBase + rva::gameui::kReloadMenuResources, menuReloadPrefix, sizeof(menuReloadPrefix)) ||
        !Prefix(hudBase + rva::client::kReloadHud, hudReloadPrefix, sizeof(hudReloadPrefix))) return false;
    // cmp dword ptr [ecx+30h],edi; je +123h. The complete original native
    // branch is kept in each MinHook trampoline, including its jump target.
    static constexpr unsigned char gateBytes[] = {0x39,0x79,0x30,0x0f,0x84,0x23,0x01,0x00,0x00};
    menuSafePath = reinterpret_cast<void*>(uiBase + rva::gameui::kMenuSafeAreaPath);
    hudSafePath = reinterpret_cast<void*>(hudBase + rva::client::kHudSafeAreaPath);
    if (!bridge::InstallHook("native menu title-safe gate", ui, rva::gameui::kMenuSafeAreaGate,
        gateBytes, sizeof(gateBytes), reinterpret_cast<void*>(&MenuGateHook), &originalMenuGate)) return false;
    if (!bridge::InstallHook("native local HUD title-safe gate", client, rva::client::kHudSafeAreaGate,
        gateBytes, sizeof(gateBytes), reinterpret_cast<void*>(&HudGateHook), &originalHudGate)) return false;
    menuBase = uiBase;
    clientBase = hudBase;
    reloadMenu = reinterpret_cast<ReloadFn>(uiBase + rva::gameui::kReloadMenuResources);
    reloadHud = reinterpret_cast<ReloadFn>(hudBase + rva::client::kReloadHud);
    menuLogged = false;
    hudLogged = false;
    installed = true;
    bridge::Log("[safearea] Scoped native title-safe gates installed; resource flags and native viewport calculations retained");
    return true;
}

void Refresh() {
    if (!installed || refreshing) return;
    // Enabling has no previously modified bounds to restore. The next native
    // resource initialization uses our installed gates. In particular, never
    // reconstruct a lingering lobby after its matchmaking session was closed.
    if (bridge::IsLocalSplitEnabled()) return;
    refreshing = true;
    // The stock HUD command already iterates both local client modes with its
    // own split-screen context guard. Only refresh HUDs this module has seen;
    // enabling at the main menu needs no HUD reload. New maps load naturally.
    __try {
        if (hudLogged && reloadHud && ObjectAt(clientBase + rva::client::kHudReloadObject) && ObjectAt(clientBase + rva::client::kEngineInterfaceGlobal))
            reloadHud();
        // Only in-session vote dialogs use positive menu title-safe flags.
        // Main menu, footer, lobby and PC option pages have absent/zero flags.
        // Skip no-session refresh: a cached GameLobby can still be visible but
        // its ApplySchemeSettings requires the already-destroyed session.
        if (menuLogged && reloadMenu && HasLiveMatchSession() &&
            ObjectAt(menuBase + rva::gameui::kMenuReloadObjectA) && ObjectAt(menuBase + rva::gameui::kMenuReloadObjectB))
            reloadMenu();
    } __finally {
        refreshing = false;
    }
    bridge::Log("[safearea] Disabled resource bounds restored where live contexts remain");
}

void Shutdown() {
    installed = false;
    originalMenuGate = nullptr;
    originalHudGate = nullptr;
    menuSafePath = nullptr;
    hudSafePath = nullptr;
    reloadMenu = nullptr;
    reloadHud = nullptr;
    menuBase = 0;
    clientBase = 0;
    menuLogged = false;
    hudLogged = false;
    refreshing = false;
}
}
