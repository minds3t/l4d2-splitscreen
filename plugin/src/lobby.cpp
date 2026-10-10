#include "lobby.h"
#include "guest.h"
#include "input.h"
#include "devices.h"
#include "join.h"
#include "bridge.h"
#include "readable.h"
#include "build_10097.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <string>
#include <utility>

namespace lobby {
namespace {
using Kv = void;
using Session = void;

// These are exact-build RVAs, verified against the fingerprints in bridge.
// docs/research/native_lobby_pc/capture_evidence.py lists bytes and call signatures.
const std::uint32_t kOfflineUpdate = rva::matchmaking::kOfflineUpdate;
const std::uint32_t kOfflineDestroy = rva::matchmaking::kOfflineDestroy;
const std::uint32_t kGetLocalPlayer = rva::matchmaking::kGetLocalPlayer;
const std::uint32_t kDecodeButtonUser = rva::gameui::kDecodeButtonUser;
const std::uint32_t kAvatarLookupReturn = rva::gameui::kAvatarLookupReturn;
const std::uint32_t kGetBasePanel = rva::gameui::kGetBasePanel;
const std::uint32_t kSetActiveUser = rva::gameui::kSetActiveUser;
const std::uint32_t kGetActiveUser = rva::gameui::kGetActiveUser;
const std::uint32_t kLobbyPlayerName = rva::gameui::kLobbyPlayerName;
const std::uint32_t kHybridMousePressed = rva::gameui::kHybridMousePressed;
const std::uint32_t kButtonMouseReleased = rva::gameui::kButtonMouseReleased;
const std::uint32_t kDropdownCommand = rva::gameui::kDropdownCommand;
const std::uint32_t kFindFlyoutButton = rva::gameui::kFindFlyoutButton;
const std::uint32_t kOpeningFocusLookupReturn = rva::gameui::kOpeningFocusLookupReturn;
const std::uint32_t kOfflineEvent = rva::matchmaking::kOfflineEvent;
const std::uint32_t kFrameworkFrame = rva::matchmaking::kFrameworkFrame;
const std::uint32_t kUiKeyValuesString = rva::gameui::kUiKeyValuesString;
const std::uint32_t kSettingsLayoutNetworkReturn = rva::gameui::kSettingsLayoutNetworkReturn;
const std::uint32_t kSettingsControlsNetworkReturn = rva::gameui::kSettingsControlsNetworkReturn;
const std::uint32_t kLobbyUpdateSettings = rva::gameui::kLobbyUpdateSettings;
const std::uint32_t kLobbySummaryLabel = rva::gameui::kLobbySummaryLabel;

using UpdateFn = void(__thiscall*)(Session*);
using LocalPlayerFn = void*(__thiscall*)(void*, int);
using DecodeFn = int(__cdecl*)(int);
using PlayerNameFn = const char*(__thiscall*)(void*, std::uint64_t, const char*);
using MouseFn = void(__thiscall*)(void*, int);
using DropdownCommandFn = void(__thiscall*)(void*, const char*);
using FindButtonFn = void*(__thiscall*)(void*, const char*);
using EventFn = void(__thiscall*)(void*, Kv*);
using FrameFn = void(__thiscall*)(void*);
using UiStringFn = const char*(__thiscall*)(Kv*, const char*, const char*);
using LobbySettingsFn = void(__thiscall*)(void*, Kv*);
UpdateFn originalUpdate = nullptr;
UpdateFn originalDestroy = nullptr;
LocalPlayerFn originalLocalPlayer = nullptr;
DecodeFn originalDecode = nullptr;
PlayerNameFn originalPlayerName = nullptr;
MouseFn originalMousePressed = nullptr;
MouseFn originalMouseReleased = nullptr;
DropdownCommandFn originalDropdownCommand = nullptr;
FindButtonFn originalFindButton = nullptr;
EventFn originalOfflineEvent = nullptr;
FrameFn originalFrameworkFrame = nullptr;
UiStringFn originalUiString = nullptr;
LobbySettingsFn originalLobbySettings = nullptr;
using InGameCommandFn = void(__thiscall*)(void*, const char*);
InGameCommandFn originalInGameCommand = nullptr;
bool offlineSummaryLogged = false;
unsigned mouseDispatchDepth = 0;
const char* openingCharacterCommand = nullptr;
int lastInputButton = -1;
int lastInputSlot = -1;
Session* ownedSession = nullptr;
Kv* ownedSettings = nullptr;
bool installed = false;
void* presentationEngineClient = nullptr;
void* eventsSubscription = nullptr;
Kv* returnSettings = nullptr;
Session* returnSource = nullptr;
Session* restoreTarget = nullptr;
std::string returnAvatars[2];
int returnActiveUser = 0;
bool returnDisconnected = false;
unsigned returnReadyFrames = 0;
ULONGLONG returnDeadline = 0;

template<class Fn>
Fn At(HMODULE module, std::uint32_t rva) {
    return reinterpret_cast<Fn>(reinterpret_cast<std::uintptr_t>(module) + rva);
}

template<class Fn>
Fn Vfunc(void* object, unsigned index) {
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[index]);
}

Session* CurrentSession() {
    void* framework = bridge::Framework();
    return framework ? Vfunc<Session*(__thiscall*)(void*)>(framework, 9)(framework) : nullptr;
}

Kv* Settings(Session* session) {
    return session ? Vfunc<Kv*(__thiscall*)(Session*)>(session, 1)(session) : nullptr;
}

const char* String(Kv* kv, const char* key, const char* fallback = "") {
    return kv ? At<const char*(__thiscall*)(Kv*, const char*, const char*)>(
        bridge::Matchmaking(), rva::matchmaking::kKvGetString)(kv, key, fallback) : fallback;
}

int Integer(Kv* kv, const char* key, int fallback = 0) {
    return kv ? At<int(__thiscall*)(Kv*, const char*, int)>(
        bridge::Matchmaking(), rva::matchmaking::kKvGetInt)(kv, key, fallback) : fallback;
}

std::uint64_t Uint64(Kv* kv, const char* key) {
    return kv ? At<std::uint64_t(__thiscall*)(Kv*, const char*, std::uint64_t)>(
        bridge::Matchmaking(), rva::matchmaking::kKvGetUint64)(kv, key, 0) : 0;
}

void SetString(Kv* kv, const char* key, const char* value) {
    At<void(__thiscall*)(Kv*, const char*, const char*)>(
        bridge::Matchmaking(), rva::matchmaking::kKvSetString)(kv, key, value);
}

void SetInt(Kv* kv, const char* key, int value) {
    At<void(__thiscall*)(Kv*, const char*, int)>(
        bridge::Matchmaking(), rva::matchmaking::kKvSetInt)(kv, key, value);
}

void SetUint64(Kv* kv, const char* key, std::uint64_t value) {
    At<void(__thiscall*)(Kv*, const char*, std::uint64_t)>(
        bridge::Matchmaking(), rva::matchmaking::kKvSetUint64)(kv, key, value);
}

Kv* Parse(const char* name, const char* body) {
    return At<Kv*(__cdecl*)(const char*, const char*, const char**)>(
        bridge::Matchmaking(), rva::matchmaking::kKvFromString)(name, body, nullptr);
}

void Delete(Kv* kv) {
    if (kv) At<void(__thiscall*)(Kv*)>(bridge::Matchmaking(), rva::matchmaking::kKvDelete)(kv);
}

const char* KeyName(Kv* kv) {
    return kv ? At<const char*(__thiscall*)(Kv*)>(bridge::Matchmaking(), rva::matchmaking::kKvGetName)(kv) : "";
}

Kv* FindKey(Kv* kv, const char* key) {
    return kv ? At<Kv*(__thiscall*)(Kv*, const char*, bool)>(
        bridge::Matchmaking(), rva::matchmaking::kKvFindKey)(kv, key, false) : nullptr;
}

// RemoveSubKey is inlined in this binary. These two links are verified in the
// native MakeCopy/FindKey functions and the public L4D2 KeyValues layout. This
// helper only edits our private native-allocated copy, never live settings.
void RemoveCopiedKey(Kv* parent, const char* name) {
    Kv* key = FindKey(parent, name);
    if (!key) return;
    auto link = reinterpret_cast<Kv**>(reinterpret_cast<std::uint8_t*>(parent) + 0x20);
    while (*link && *link != key)
        link = reinterpret_cast<Kv**>(reinterpret_cast<std::uint8_t*>(*link) + 0x1C);
    if (*link != key) return;
    auto next = reinterpret_cast<Kv**>(reinterpret_cast<std::uint8_t*>(key) + 0x1C);
    *link = *next;
    *next = nullptr;
    Delete(key);
}

void ClearReturn() {
    Delete(returnSettings);
    returnSettings = nullptr;
    returnSource = nullptr;
    restoreTarget = nullptr;
    returnAvatars[0].clear();
    returnAvatars[1].clear();
    returnDisconnected = false;
    returnReadyFrames = 0;
    returnDeadline = 0;
}

std::uint64_t ProfileXuid(void* profile) {
    return profile ? Vfunc<std::uint64_t(__thiscall*)(void*)>(profile, 0)(profile) : 0;
}

bool Offline(Kv* settings) {
    return settings && _stricmp(String(settings, "System/network"), "offline") == 0;
}

bool Owned(bool requireLobby = false) {
    if (!bridge::IsLocalSplitEnabled() || !guest::GuestProfile() || !ownedSession)
        return false;
    Session* current = CurrentSession();
    if (current != ownedSession || Settings(current) != ownedSettings || !Offline(ownedSettings))
        return false;
    if (Uint64(ownedSettings, "Members/machine0/player1/xuid") != guest::GuestXuid())
        return false;
    // CMatchSessionOfflineCustom state is verified at +0x10 in its Update and
    // Command methods: 0 initializing, 1 prelaunch, 2 started. This check is only
    // used after the actual offline-class Update detour registered ownership.
    return !requireLobby || *reinterpret_cast<const int*>(
        reinterpret_cast<const std::uint8_t*>(current) + 0x10) == 1;
}

void* BasePanel() {
    return At<void*(__cdecl*)()>(bridge::GameUI(), kGetBasePanel)();
}

int ActiveUser() {
    void* panel = BasePanel();
    if (!panel) return 0;
    int slot = At<int(__thiscall*)(void*)>(bridge::GameUI(), kGetActiveUser)(panel);
    return slot == 1 ? 1 : 0;
}

void SetActiveUser(int slot) {
    if (void* panel = BasePanel())
        At<void(__thiscall*)(void*, int)>(bridge::GameUI(), kSetActiveUser)(panel, slot);
}

// Modes with a human Infected team: Versus, Scavenge, Realism Versus and
// Versus Survival (the last two are the main menu's mutation12 / mutation15).
bool TeamMode(const char* mode) {
    for (const char* team : {"versus", "scavenge", "mutation12", "mutation15"})
        if (_stricmp(mode, team) == 0) return true;
    return false;
}

bool Expand(Session* session) {
    if (!bridge::IsLocalSplitEnabled()) return false;
    Kv* settings = Settings(session);
    if (!Offline(settings)) return false;
    // This hook runs only for the native offline class. Check its exact initial
    // single-local structure before touching it; never rewrite online members.
    if (Integer(settings, "Members/numMachines") != 1
        || Integer(settings, "Members/numPlayers") != 1
        || Integer(settings, "Members/machine0/numPlayers") != 1) {
        bridge::Log("[lobby] Offline expansion skipped: unexpected initial member counts");
        return false;
    }
    // Register only after a real offline session exists, never merely because
    // the feature was enabled while the user was on the online main menu.
    const bool registering = !guest::GuestProfile();
    if (registering && !guest::Register(bridge::PlayerManager(), bridge::HostProfile())) {
        bridge::Log("[lobby] Offline expansion skipped: guest registration failed");
        return false;
    }
    const auto hostXuid = ProfileXuid(bridge::HostProfile());
    const auto guestXuid = guest::GuestXuid();
    if (!hostXuid || !guestXuid || hostXuid == guestXuid
        || Uint64(settings, "Members/machine0/player0/xuid") != hostXuid) {
        bridge::Log("[lobby] Offline expansion skipped: host/guest identity mismatch");
        if (registering) guest::Unregister();
        return false;
    }

    SetUint64(settings, "Members/machine0/player1/xuid", guestXuid);
    SetString(settings, "Members/machine0/player1/name", guest::GuestName());
    SetString(settings, "Members/machine0/player1/game/avatar", "");
    SetInt(settings, "Members/machine0/numPlayers", 2);
    SetInt(settings, "Members/numPlayers", 2);
    // GameLobby selects its native survivor/Infected dropdown when nominal
    // capacity exceeds4. Offline initialization otherwise leaves capacity1,
    // even for competitive modes. Actual local membership remains exactly2.
    const char* mode = String(settings, "Game/mode");
    SetInt(settings, "Members/numSlots", (std::max)(TeamMode(mode) ? 8 : 2,
        Integer(settings, "Members/numSlots")));
    ownedSession = session;
    ownedSettings = settings;
    if (restoreTarget == session) {
        SetString(settings, "Members/machine0/player0/game/avatar", returnAvatars[0].c_str());
        SetString(settings, "Members/machine0/player1/game/avatar", returnAvatars[1].c_str());
        SetActiveUser(returnActiveUser);
        bridge::Log("[lobby] Restored native lobby selections P1=%s P2=%s", returnAvatars[0].c_str(), returnAvatars[1].c_str());
        ClearReturn();
    } else {
        SetActiveUser(0);
    }
    bridge::Log("[lobby] Expanded native offline session before ready: players=2 slots=%d guest=%llu",
        Integer(settings, "Members/numSlots"), static_cast<unsigned long long>(guestXuid));
    return true;
}

void __fastcall UpdateHook(Session* session, void*) {
    if (*reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(session) + 0x10) == 0)
        Expand(session);
    originalUpdate(session);
}

void __fastcall DestroyHook(Session* session, void*) {
    const bool ours = session == ownedSession;
    const bool restoring = session == restoreTarget;
    // Keep the profile alive during native session-end profile notifications.
    originalDestroy(session);
    if (restoring) ClearReturn();
    if (ours) {
        ownedSession = nullptr;
        ownedSettings = nullptr;
        guest::Unregister();
        bridge::Log("[lobby] Native offline session ended; local guest unregistered");
    }
}

// Public IMatchEventsSink ABI is exactly one OnEvent virtual, no destructor.
// Subscribe separately from the native session so its later signout event is
// still observable after Valve has destroyed the old offline session.
class ReturnEvents {
public:
    virtual void OnEvent(Kv* event) {
        if (!returnSettings || !bridge::IsLocalSplitEnabled()) return;
        if (_stricmp(KeyName(event), "OnEngineClientSignonStateChange") == 0
            && Integer(event, "slot") == 0
            && Integer(event, "old") >= 2 && Integer(event, "new") < 2) {
            returnDisconnected = true;
            bridge::Log("[lobby] Intentional return: host disconnected; waiting for native teardown");
        }
    }
};
ReturnEvents returnEvents;

void __fastcall OfflineEventHook(void* sink, void*, Kv* event) {
    // The exact native offline class's IMatchEventsSink subobject is at +4.
    if (Owned() && reinterpret_cast<std::uint8_t*>(sink)
            == reinterpret_cast<std::uint8_t*>(ownedSession) + 4
        && *reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(ownedSession) + 0x10) == 2
        && _stricmp(KeyName(event), "OnEngineEndGame") == 0 && !returnSettings) {
        Kv* copy = At<Kv*(__thiscall*)(Kv*)>(bridge::Matchmaking(), rva::matchmaking::kKvMakeCopy)(ownedSettings);
        if (copy) {
            ClearReturn();
            returnAvatars[0] = String(ownedSettings, "Members/machine0/player0/game/avatar");
            returnAvatars[1] = String(ownedSettings, "Members/machine0/player1/game/avatar");
            returnActiveUser = ActiveUser();
            // Preserve native Game/Options and mission metadata, but let the
            // new session recreate server and member lifecycle records.
            RemoveCopiedKey(copy, "Server");
            RemoveCopiedKey(copy, "Members");
            RemoveCopiedKey(FindKey(copy, "System"), "lock");
            returnSettings = copy;
            returnSource = ownedSession;
            returnDeadline = GetTickCount64() + 15000;
            bridge::Log("[lobby] Intentional native EndGame: saved mode=%s campaign=%s chapter=%d reason=%s",
                String(copy, "Game/mode"), String(copy, "Game/campaign"), Integer(copy, "Game/chapter"),
                String(event, "reason"));
        }
    }
    // Ordinary disconnect and native cleanup are never suppressed.
    originalOfflineEvent(sink, event);
}

// Set by the pause menu and run from the following frames, outside the menu's
// own input dispatch. A plain disconnect removes player two's survivor and
// the game later spawns a fresh bot (full health, default pistol). Leaving
// therefore first hands the survivor to a bot the way "take a break" does,
// which keeps health, items and position, and disconnects a moment later.
enum class Drop { None, Join, Idle, Disconnect };
Drop pendingDrop = Drop::None;
ULONGLONG dropAt = 0;
constexpr ULONGLONG kIdleBeforeDisconnectMs = 700;

void RunPendingDrop() {
    if (pendingDrop == Drop::None) return;
    if (!Owned()) { pendingDrop = Drop::None; return; }
    if (pendingDrop == Drop::Disconnect && GetTickCount64() < dropAt) return;
    const char* command = pendingDrop == Drop::Join ? "ss_connect\n" :
        pendingDrop == Drop::Idle ? "cmd2 go_away_from_keyboard\n" : "ss_disconnect 1\n";
    const bool sent = bridge::PrimaryCommand(command);
    bridge::Log("[lobby] Pause menu: %.*s (sent=%d)", static_cast<int>(std::strlen(command)) - 1, command, sent);
    if (pendingDrop == Drop::Idle && sent) {
        pendingDrop = Drop::Disconnect;
        dropAt = GetTickCount64() + kIdleBeforeDisconnectMs;
    } else {
        pendingDrop = Drop::None;
    }
}

void __fastcall FrameworkFrameHook(void* framework, void*) {
    originalFrameworkFrame(framework);
    RunPendingDrop();
    if (bridge::IsLocalSplitEnabled()) {
        if (devices::WatchForController()) input::ControllerFound();
        // Controller N always drives player N. Keyboard and mouse drive player
        // two while both are in the game and a first controller is present,
        // otherwise player one. Menus map the keyboard themselves (DecodeHook).
        const bool guestInGame = Owned() && bridge::GuestInGame();
        input::SetKeyboardSlot(guestInGame ? devices::KeyboardSlot() : 0);
        input::RunPlayerSetup(guestInGame);
        input::TrackSplitView(guestInGame);
        // Gameplay's native controller setup may switch the joystick off.
        // Reassert for our own session and for the main menu. Leave another
        // matchmaking session untouched. Input owns the activation snapshot
        // and the guard-before-joystick ordering.
        if (Owned() || !CurrentSession()) input::MaintainMenu();
    }
    if (!returnSettings) return;
    if (!bridge::IsLocalSplitEnabled() || GetTickCount64() >= returnDeadline) {
        bridge::Log("[lobby] Return cancelled: bridge disabled or native teardown timed out");
        ClearReturn();
        return;
    }
    Session* current = CurrentSession();
    if (current && current != returnSource) {
        bridge::Log("[lobby] Return cancelled: a different session was opened");
        ClearReturn();
        return;
    }
    if (current || !returnDisconnected) { returnReadyFrames = 0; return; }
    // Leave one complete matchmaking frame after teardown before creating UI.
    if (++returnReadyFrames < 2) return;
    Kv* saved = returnSettings;
    returnSettings = nullptr;
    Kv* request = At<Kv*(__thiscall*)(Kv*)>(bridge::Matchmaking(), rva::matchmaking::kKvMakeCopy)(saved);
    Kv* delta = Parse("Settings", "Update { }");
    Kv* update = FindKey(delta, "Update");
    if (!request || !update) {
        Delete(request);
        Delete(delta);
        Delete(saved);
        ClearReturn();
        bridge::Log("[lobby] Native offline return settings copy failed");
        return;
    }
    // UpdateSessionSettings consumes an Update/Delete/Borrow envelope. Its
    // native type0 merge ignores a flat settings tree. Type1 copies the saved
    // tree into our new Update node without changing either live session.
    At<void(__thiscall*)(Kv*, Kv*, int)>(bridge::Matchmaking(), rva::matchmaking::kKvMergeFrom)(update, saved, 1);
    Delete(saved);
    Vfunc<void(__thiscall*)(void*, Kv*)>(framework, 12)(framework, request);
    Delete(request);
    restoreTarget = CurrentSession();
    if (!restoreTarget || !Offline(Settings(restoreTarget))) {
        Delete(delta);
        bridge::Log("[lobby] Native offline return creation failed");
        ClearReturn();
        return;
    }
    // InitializeGameSettings selects default mission settings even when the
    // constructor input names a campaign. Reapply the preserved, member-free
    // settings through IMatchSession::UpdateSessionSettings after initialization
    // and before the first Update emits ready. This is also the native edit UI
    // path, so its title-extension validation/merge remains in control.
    Vfunc<void(__thiscall*)(Session*, Kv*)>(restoreTarget, 2)(restoreTarget, delta);
    Delete(delta);
    Kv* restored = Settings(restoreTarget);
    bridge::Log("[lobby] Restored native lobby settings mode=%s campaign=%s chapter=%d difficulty=%s",
        String(restored, "Game/mode"), String(restored, "Game/campaign"),
        Integer(restored, "Game/chapter"), String(restored, "Game/difficulty"));
    bridge::Log("[lobby] Recreated native offline session after intentional return; awaiting ready");
}

void* __fastcall LocalPlayerHook(void* manager, void*, int slot) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto ui = reinterpret_cast<std::uintptr_t>(bridge::GameUI());
    // Only the native GameLobby character_ command: do not redirect host
    // authorization, automatic Infected fallback, stats, or unrelated menus.
    if (slot == 0 && caller == ui + kAvatarLookupReturn && Owned(true)) {
        slot = ActiveUser();
        bridge::Log("[lobby] Native character command from local slot %d", slot);
    }
    return originalLocalPlayer(manager, slot);
}

int __cdecl DecodeHook(int button) {
    int slot = originalDecode(button);
    // Lobby and running game alike: the pause menu (Take a Break, votes) and
    // the Controller dialog act for the last active user, natively.
    if (Owned()) {
        const int nativeSlot = slot;
        // Native joystick ranges begin at 0x72. Keyboard/mouse act for the
        // player they drive in the game; preserve controller0/1 identity.
        if (mouseDispatchDepth || (button > 0 && button < 0x72)) slot = devices::KeyboardSlot();
        if (slot == 0 || slot == 1) SetActiveUser(slot);
        if (button != lastInputButton || slot != lastInputSlot) {
            bridge::Log("[lobby] Input button=0x%X native-slot=%d local-slot=%d", button, nativeSlot, slot);
            lastInputButton = button;
            lastInputSlot = slot;
        }
    }
    return slot;
}

void DispatchMouse(MouseFn original, void* button, int code) {
    const bool ours = Owned();
    const int owner = ours ? devices::KeyboardSlot() : 0;
    if (ours) {
        ++mouseDispatchDepth;
        SetActiveUser(owner);
        if (code != lastInputButton || lastInputSlot != owner) {
            bridge::Log("[lobby] Mouse button=0x%X local-slot=%d", code, owner);
            lastInputButton = code;
            lastInputSlot = owner;
        }
    }
    original(button, code);
    if (ours) {
        --mouseDispatchDepth;
        // VGUI posts button commands for later delivery. Keep the originating
        // user after this mouse event as the native keyboard path already does.
        if (Owned()) SetActiveUser(owner);
    }
}

void __fastcall MousePressedHook(void* button, void*, int code) {
    DispatchMouse(originalMousePressed, button, code);
}

void __fastcall MouseReleasedHook(void* button, void*, int code) {
    DispatchMouse(originalMouseReleased, button, code);
}

void __fastcall DropdownCommandHook(void* dropdown, void*, const char* command) {
    char selected[64] = {};
    const char* previous = openingCharacterCommand;
    if (command && Owned(true)
        && (_stricmp(command, "FlmCharacterFlyout") == 0
            || _stricmp(command, "FlmVersusCharacterFlyout") == 0)) {
        const char* avatar = String(ownedSettings, ActiveUser() == 1
            ? "Members/machine0/player1/game/avatar"
            : "Members/machine0/player0/game/avatar");
        std::snprintf(selected, sizeof(selected), "character_%s", avatar);
        openingCharacterCommand = selected;
    }
    originalDropdownCommand(dropdown, command);
    openingCharacterCommand = previous;
}

void* __fastcall FindButtonHook(void* flyout, void*, const char* command) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (openingCharacterCommand && Owned(true)
        && caller == reinterpret_cast<std::uintptr_t>(bridge::GameUI()) + kOpeningFocusLookupReturn) {
        // A native DropDownMenu remembers one shared last choice, not one per
        // local player. Resolve its initial focus against the active member's
        // real avatar. Do not call SetCurrentSelection: that also sends a new
        // character command. Native navigation still renders the preview and
        // enforces occupied/unsupported character choices.
        if (void* selected = originalFindButton(flyout, openingCharacterCommand))
            return selected;
        return originalFindButton(flyout, "character_");
    }
    return originalFindButton(flyout, command);
}



const char* __fastcall UiStringHook(Kv* kv, void*, const char* key, const char* fallback) {
    const char* value = originalUiString(kv, key, fallback);
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto ui = reinterpret_cast<std::uintptr_t>(bridge::GameUI());
    if (kv == ownedSettings && key && _stricmp(key, "System/network") == 0
        && value && _stricmp(value, "offline") == 0 && Owned(true)
        && (caller == ui + kSettingsLayoutNetworkReturn
            || caller == ui + kSettingsControlsNetworkReturn)) {
        // These two GameSettings reads classify its presentation/lifetime:
        // ApplyScheme selects GameSettings_<mode>Edit from the lobby nav-back;
        // UpdateControls sets edit mode and the "owns session" byte. Stock PC
        // treats every offline session as solo and destroys it on Back. Reuse
        // the retained local multiplayer editing path for this owned lobby.
        // No KeyValues are changed; all matchmaking/launch reads remain offline.
        if (caller == ui + kSettingsLayoutNetworkReturn)
            bridge::Log("[lobby] Using native shared-lobby GameSettings edit presentation");
        return "lan";
    }
    return value;
}

void __fastcall LobbySettingsHook(void* panel, void*, Kv* update) {
    originalLobbySettings(panel, update);
    if (!Owned(true) || !panel
        || *reinterpret_cast<Kv**>(static_cast<unsigned char*>(panel) + 0x2F0) != ownedSettings)
        return;
    // Valve maps the mode/key through LobbyDetailsSummary.res to an existing
    // label. Change only that label after the native update: the same original
    // access string also drives GameAccess_* commands and must remain untouched.
    void* label = At<void*(__thiscall*)(void*, const char*)>(
        bridge::GameUI(), kLobbySummaryLabel)(panel, "access");
    if (!label) return;
    Vfunc<void(__thiscall*)(void*, const char*)>(label, 0x350 / sizeof(void*))(
        label, "#L4D360UI_Mode_offline_SS");
    if (!offlineSummaryLogged) {
        bridge::Log("[lobby] Native access summary shows localized Offline Co-op; session access/network unchanged");
        offlineSummaryLogged = true;
    }
}

// GameUI's IVEngineClient; slot 107 runs a console command unrestricted, as
// the pause menu itself does for gameui_hide (mov edx,[eax+1ac]; call edx).
bool EngineCommand(const char* text) {
    void** global = At<void**>(bridge::GameUI(), rva::gameui::kEngineInterfaceGlobal);
    void* engineClient = bridge::Readable(global, sizeof(void*)) ? *global : nullptr;
    if (!bridge::Readable(engineClient, sizeof(void*)) ||
        !bridge::Readable(*static_cast<void***>(engineClient), 108 * sizeof(void*))) return false;
    Vfunc<void(__thiscall*)(void*, const char*)>(engineClient, 107)(engineClient, text);
    return true;
}

// The retained Xbox handler answers EnableSplitscreen / DisableSplitscreen by
// leaving the game ("You must exit to the main menu to change this setting")
// and hides those buttons by its own one-user count. The PC engine can add
// and drop a split-screen player in place; the VPK's pause-menu rows send
// these two commands of our own for an owned game.
// Grey out the row that does not apply right now. vgui::Panel::SetEnabled is
// vtable slot 54 (Panel::ApplySettings: GetInt("enabled") -> call [vtbl+d8]).
void SyncDropRows(void* menu) {
    const bool playing = bridge::GuestInGame();
    const auto find = At<void*(__thiscall*)(void*, const char*, bool)>(bridge::GameUI(), rva::gameui::kPanelFindChild);
    const auto setEnabled = [&](const char* name, bool enabled) {
        void* row = find(menu, name, true);
        if (bridge::Readable(row, sizeof(void*)) &&
            bridge::Readable(*static_cast<void***>(row), 55 * sizeof(void*)))
            Vfunc<void(__thiscall*)(void*, bool)>(row, 54)(row, enabled);
    };
    setEnabled("BtnPlayerTwoJoin", !playing);
    setEnabled("BtnPlayerTwoLeave", playing);
}

void __fastcall InGameCommandHook(void* menu, void*, const char* command) {
    if (command && Owned() && *reinterpret_cast<const int*>(
            reinterpret_cast<const std::uint8_t*>(ownedSession) + 0x10) == 2) {
        if (_stricmp(command, "FlmOptionsFlyout") == 0) {
            originalInGameCommand(menu, command);
            SyncDropRows(menu);
            return;
        }
        const bool join = _stricmp(command, "PlayerTwoJoin") == 0;
        if (join || _stricmp(command, "PlayerTwoLeave") == 0) {
            if (pendingDrop == Drop::None) pendingDrop = join ? Drop::Join : Drop::Idle;
            EngineCommand("gameui_hide");
            return;
        }
    }
    originalInGameCommand(menu, command);
}

const char* __fastcall PlayerNameHook(void* utils, void*, std::uint64_t xuid, const char* fallback) {
    // The PC UI otherwise asks Steam for this offline-only identity and may
    // replace the member name with its nonempty "unknown user" placeholder.
    if (xuid && xuid == guest::GuestXuid() && Owned()) return guest::GuestName();
    return originalPlayerName(utils, xuid, fallback);
}

bool Command(Kv* command) {
    if (!command || !Owned()) { Delete(command); return false; }
    Session* current = CurrentSession();
    Vfunc<void(__thiscall*)(Session*, Kv*)>(current, 3)(current, command);
    Delete(command);
    return true;
}

const char* CanonicalAvatar(const char* value) {
    if (!value) return nullptr;
    // Native lobby commands retain these four roster slots for both casts.
    // L4D1 labels change with the mission: Bill/Zoey/Louis/Francis map to
    // gambler/producer/coach/mechanic, matching the native character flyout.
    static const char* names[][2] = {
        {"Nick", "gambler"}, {"Rochelle", "producer"}, {"Coach", "coach"},
        {"Ellis", "mechanic"}, {"Bill", "gambler"}, {"Zoey", "producer"},
        {"Francis", "mechanic"}, {"Louis", "coach"},
        {"namvet", "gambler"}, {"teengirl", "producer"},
        {"biker", "mechanic"}, {"manager", "coach"}, {"Infected", "Infected"},
        {"Random", ""}, {"", ""}
    };
    for (const auto& pair : names)
        if (_stricmp(value, pair[0]) == 0 || _stricmp(value, pair[1]) == 0) return pair[1];
    return nullptr;
}
} // namespace

bool Install() {
    if (installed) return true;
    // Public IVEngineClient013 slots26/27 are IsInGame/IsConnected. Verify the
    // factory's actual object and entries against this fingerprinted engine;
    // never infer main-menu state merely from missing matchmaking state.
    HMODULE engine = bridge::Engine();
    using FactoryFn = void*(__cdecl*)(const char*, int*);
    auto factory = engine ? reinterpret_cast<FactoryFn>(GetProcAddress(engine, "CreateInterface")) : nullptr;
    void* client = factory ? factory("VEngineClient013", nullptr) : nullptr;
    const auto engineBase = reinterpret_cast<std::uintptr_t>(engine);
    if (client == reinterpret_cast<void*>(engineBase + rva::engine::kEngineClient)
        && *reinterpret_cast<std::uintptr_t*>(client) == engineBase + rva::engine::kEngineClientVtable
        && reinterpret_cast<std::uintptr_t>(Vfunc<void*>(client, 26)) == engineBase + rva::engine::kIsInGame
        && reinterpret_cast<std::uintptr_t>(Vfunc<void*>(client, 27)) == engineBase + rva::engine::kIsConnected) {
        presentationEngineClient = client;
    } else {
        presentationEngineClient = nullptr;
        bridge::Log("[lobby] Console presentation scope unavailable: engine interface mismatch");
    }
    static const std::uint8_t updateBytes[] = {0x83,0x79,0x10,0x00,0x75,0x4A,0x6A,0x2C};
    static const std::uint8_t destroyBytes[] = {0x55,0x8B,0xEC,0x83,0xEC,0x08,0x56,0x8B,0xF1};
    static const std::uint8_t playerBytes[] = {0x55,0x8B,0xEC,0x8B,0x45,0x08,0x83,0xF8,0x03};
    static const std::uint8_t decodeBytes[] = {0x55,0x8B,0xEC,0x8B,0x45,0x08,0x83,0xF8,0x72};
    static const std::uint8_t nameBytes[] = {0x55,0x8B,0xEC,0x83,0xEC,0x10,0x53};
    static const std::uint8_t mousePressedBytes[] = {0x55,0x8B,0xEC,0x56,0x57,0x8B,0x7D,0x08};
    static const std::uint8_t mouseReleasedBytes[] = {0x55,0x8B,0xEC,0x56,0x8B,0xF1,0x8B,0x06};
    static const std::uint8_t dropdownBytes[] = {0x55,0x8B,0xEC,0x56,0x57,0x8B,0x7D,0x08};
    static const std::uint8_t findButtonBytes[] = {0x55,0x8B,0xEC,0x53,0x56,0x57,0x8B,0xD9};
    static const std::uint8_t eventBytes[] = {0x55,0x8B,0xEC,0x56,0x8B,0x75,0x08,0x57};
    static const std::uint8_t uiStringBytes[] = {0x55,0x8B,0xEC,0x81,0xEC,0x44,0x02,0x00,0x00};
    static const std::uint8_t lobbySettingsBytes[] = {0x55,0x8B,0xEC,0x81,0xEC,0x80,0x02,0x00,0x00};
    static const std::uint8_t inGameCommandBytes[] = {0x55,0x8B,0xEC,0x81,0xEC,0xC0,0x00,0x00,0x00};
    std::uint8_t frameBytes[] = {0xA1,0,0,0,0,0x83,0x78,0x30,0x00};
    const auto frameGlobal = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bridge::Matchmaking()) + rva::matchmaking::kFrameworkFrameGlobal);
    std::memcpy(frameBytes + 1, &frameGlobal, sizeof(frameGlobal));
    if (!bridge::InstallHook("offline session ready", bridge::Matchmaking(), kOfflineUpdate,
            updateBytes, sizeof(updateBytes), reinterpret_cast<void*>(&UpdateHook),
            reinterpret_cast<void**>(&originalUpdate))
        || !bridge::InstallHook("offline session cleanup", bridge::Matchmaking(), kOfflineDestroy,
            destroyBytes, sizeof(destroyBytes), reinterpret_cast<void*>(&DestroyHook),
            reinterpret_cast<void**>(&originalDestroy))
        || !bridge::InstallHook("lobby local profile", bridge::Matchmaking(), kGetLocalPlayer,
            playerBytes, sizeof(playerBytes), reinterpret_cast<void*>(&LocalPlayerHook),
            reinterpret_cast<void**>(&originalLocalPlayer))
        || !bridge::InstallHook("lobby input identity", bridge::GameUI(), kDecodeButtonUser,
            decodeBytes, sizeof(decodeBytes), reinterpret_cast<void*>(&DecodeHook),
            reinterpret_cast<void**>(&originalDecode))
        || !bridge::InstallHook("offline guest lobby name", bridge::GameUI(), kLobbyPlayerName,
            nameBytes, sizeof(nameBytes), reinterpret_cast<void*>(&PlayerNameHook),
            reinterpret_cast<void**>(&originalPlayerName))
        || !bridge::InstallHook("lobby mouse press identity", bridge::GameUI(), kHybridMousePressed,
            mousePressedBytes, sizeof(mousePressedBytes), reinterpret_cast<void*>(&MousePressedHook),
            reinterpret_cast<void**>(&originalMousePressed))
        || !bridge::InstallHook("lobby mouse release identity", bridge::GameUI(), kButtonMouseReleased,
            mouseReleasedBytes, sizeof(mouseReleasedBytes), reinterpret_cast<void*>(&MouseReleasedHook),
            reinterpret_cast<void**>(&originalMouseReleased))
        || !bridge::InstallHook("lobby character dropdown", bridge::GameUI(), kDropdownCommand,
            dropdownBytes, sizeof(dropdownBytes), reinterpret_cast<void*>(&DropdownCommandHook),
            reinterpret_cast<void**>(&originalDropdownCommand))
        || !bridge::InstallHook("lobby character opening focus", bridge::GameUI(), kFindFlyoutButton,
            findButtonBytes, sizeof(findButtonBytes), reinterpret_cast<void*>(&FindButtonHook),
            reinterpret_cast<void**>(&originalFindButton))
        || !bridge::InstallHook("intentional offline return", bridge::Matchmaking(), kOfflineEvent,
            eventBytes, sizeof(eventBytes), reinterpret_cast<void*>(&OfflineEventHook),
            reinterpret_cast<void**>(&originalOfflineEvent))
        || !bridge::InstallHook("deferred offline lobby return", bridge::Matchmaking(), kFrameworkFrame,
            frameBytes, sizeof(frameBytes), reinterpret_cast<void*>(&FrameworkFrameHook),
            reinterpret_cast<void**>(&originalFrameworkFrame))
        || !bridge::InstallHook("owned lobby settings presentation", bridge::GameUI(), kUiKeyValuesString,
            uiStringBytes, sizeof(uiStringBytes), reinterpret_cast<void*>(&UiStringHook),
            reinterpret_cast<void**>(&originalUiString))
        || !bridge::InstallHook("owned lobby access summary", bridge::GameUI(), kLobbyUpdateSettings,
            lobbySettingsBytes, sizeof(lobbySettingsBytes), reinterpret_cast<void*>(&LobbySettingsHook),
            reinterpret_cast<void**>(&originalLobbySettings))
        || !bridge::InstallHook("pause menu drop-in/out", bridge::GameUI(), rva::gameui::kInGameMenuCommand,
            inGameCommandBytes, sizeof(inGameCommandBytes), reinterpret_cast<void*>(&InGameCommandHook),
            reinterpret_cast<void**>(&originalInGameCommand))) return false;
    void* framework = bridge::Framework();
    eventsSubscription = Vfunc<void*(__thiscall*)(void*)>(framework, 7)(framework);
    if (!eventsSubscription) return false;
    Vfunc<void(__thiscall*)(void*, ReturnEvents*)>(eventsSubscription, 0)(eventsSubscription, &returnEvents);
    installed = true;
    return true;
}

void Shutdown() {
    // The shared bridge disables/removes hooks before unloading modules. Close
    // an owned offline session before guest teardown so no dangling profile is
    // used by a surviving native lobby. Never close an unrelated online session.
    if (eventsSubscription) {
        Vfunc<void(__thiscall*)(void*, ReturnEvents*)>(eventsSubscription, 1)(eventsSubscription, &returnEvents);
        eventsSubscription = nullptr;
    }
    ClearReturn();
    CloseSession();
    ownedSession = nullptr;
    ownedSettings = nullptr;
    mouseDispatchDepth = 0;
    openingCharacterCommand = nullptr;
    lastInputButton = -1;
    lastInputSlot = -1;
    installed = false;
    presentationEngineClient = nullptr;
    offlineSummaryLogged = false;
}

bool HasOwnedOfflineSession() { return Owned(); }

bool UseConsoleMenuPresentation() {
    if (!bridge::IsLocalSplitEnabled() || !presentationEngineClient) return false;
    // Only the exact native footer paint queries consume this helper. An owned
    // active game therefore gets its Xbox prompts when its pause UI is painted,
    // without changing any gameplay input or unrelated matchmaking session.
    if (Owned()) return true;
    if (Vfunc<bool(__thiscall*)(void*)>(presentationEngineClient, 27)(presentationEngineClient))
        return false;
    return CurrentSession() == nullptr;
}

bool CreateOfflineSession(const char* mode) {
    if (!bridge::IsLocalSplitEnabled() || !mode || CurrentSession())
        return false;
    bool valid = false;
    for (const char* supported : {"coop", "realism", "survival", "versus", "scavenge", "mutation12", "mutation15"})
        if (_stricmp(mode, supported) == 0) { mode = supported; valid = true; break; }
    if (!valid) return false;
    Kv* settings = Parse("Settings", "System { network offline } Game { mode = }");
    if (!settings) return false;
    SetString(settings, "Game/mode", mode);
    // Same retained mode/difficulty helpers as MainMenu::SoloPlay_<mode>.
    if (At<bool(__cdecl*)(const char*)>(bridge::GameUI(), rva::gameui::kIsModeWithDifficulty)(mode)) {
        const char* difficulty = At<const char*(__cdecl*)(const char*)>(bridge::GameUI(), rva::gameui::kDefaultDifficulty)(mode);
        SetString(settings, "Game/difficulty", difficulty ? difficulty : "normal");
    }
    void* framework = bridge::Framework();
    Vfunc<void(__thiscall*)(void*, Kv*)>(framework, 12)(framework, settings);
    Delete(settings);
    bridge::Log("[lobby] Requested native SoloPlay offline session mode=%s", mode);
    return CurrentSession() != nullptr;
}

bool SelectAvatar(int slot, const char* avatar) {
    const char* canonical = CanonicalAvatar(avatar);
    if (!Owned(true) || (slot != 0 && slot != 1) || !canonical) return false;
    void* profile = slot == 0 ? bridge::HostProfile() : guest::GuestProfile();
    Kv* command = Parse("Game::Avatar", "run host");
    if (!command) return false;
    SetUint64(command, "xuid", ProfileXuid(profile));
    SetString(command, "avatar", canonical);
    SetActiveUser(slot);
    const bool sent = Command(command);
    bridge::Log("[lobby] Native avatar request slot=%d avatar=%s sent=%d (acceptance is native)", slot, canonical, sent);
    return sent;
}

bool StartSession() {
    if (!Owned(true)) return false;
    return Command(Parse("Start", ""));
}

bool CloseSession() {
    ClearReturn();
    if (!ownedSession || CurrentSession() != ownedSession || Settings(ownedSession) != ownedSettings
        || !Offline(ownedSettings)) return false;
    void* framework = bridge::Framework();
    ownedSession = nullptr;
    ownedSettings = nullptr;
    Vfunc<void(__thiscall*)(void*)>(framework, 15)(framework);
    guest::Unregister();
    bridge::Log("[lobby] Closed owned native offline session");
    return true;
}

void DumpState() {
    Session* current = CurrentSession();
    Kv* settings = Settings(current);
    bridge::Log("[lobby] session=%p owned=%d network=%s mode=%s machines=%d players=%d slots=%d active=%d",
        current, Owned(), String(settings, "System/network"), String(settings, "Game/mode"),
        Integer(settings, "Members/numMachines"), Integer(settings, "Members/numPlayers"),
        Integer(settings, "Members/numSlots"), ActiveUser());
    bridge::Log("[lobby] P1 xuid=%llu name=%s avatar=%s; P2 xuid=%llu name=%s avatar=%s",
        static_cast<unsigned long long>(Uint64(settings, "Members/machine0/player0/xuid")),
        String(settings, "Members/machine0/player0/name"), String(settings, "Members/machine0/player0/game/avatar"),
        static_cast<unsigned long long>(Uint64(settings, "Members/machine0/player1/xuid")),
        String(settings, "Members/machine0/player1/name"), String(settings, "Members/machine0/player1/game/avatar"));
    bridge::Log("[lobby] campaign=%s chapter=%d difficulty=%s server=%s map=%s state=%d",
        String(settings, "Game/campaign"), Integer(settings, "Game/chapter"),
        String(settings, "Game/difficulty"), String(settings, "Options/server"),
        String(settings, "Game/map"), Owned() ? *reinterpret_cast<const int*>(
            reinterpret_cast<const std::uint8_t*>(current) + 0x10) : -1);
}
} // namespace lobby
