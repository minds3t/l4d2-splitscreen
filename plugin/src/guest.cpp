#include "guest.h"
#include "readable.h"
#include "build_10097.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

// ABI declarations checked against the maintained L4D2 SDK header and the
// installed matchmaking.dll. Keep these type names global: native RTTI queries
// for IPlayerLocal should safely fail their cast to the native PlayerLocal.
// https://raw.githubusercontent.com/alliedmodders/hl2sdk/l4d2/public/matchmaking/iplayer.h
struct UserProfileData {
    float reputation;
    std::int32_t difficulty, sensitivity, yaxis, vibration, color1, color2;
    std::int32_t action_autoaim, action_autocenter, action_movementcontrol;
    std::int32_t region, achearned, cred, zone, titlesplayed, titleachearned, titlecred;
};
class IPlayer {
public:
    virtual std::uint64_t GetXUID() = 0;
    virtual int GetPlayerIndex() = 0;
    virtual const char* GetName() = 0;
    virtual int GetOnlineState() = 0;
};
class IPlayerLocal : public IPlayer {
public:
    virtual const UserProfileData& GetPlayerProfileData() = 0;
    virtual const void* GetPlayerTitleData(int index) = 0;
    virtual void UpdatePlayerTitleData(int index, const void* data, int offset, int size) = 0;
    virtual void GetLeaderboardData(void* query) = 0;
    virtual void UpdateLeaderboardData(void* query) = 0;
};

namespace guest {
namespace {
static_assert(sizeof(void*) == 4, "Guest ABI is verified only for the x86 game");
static_assert(sizeof(UserProfileData) == 0x44, "Native profile data size");

const std::uintptr_t kManagerGlobal = rva::matchmaking::kPlayerManagerGlobal;
const std::uintptr_t kManagerVtable = rva::matchmaking::kPlayerManagerVtable;
const std::uintptr_t kLocalVtable = rva::matchmaking::kPlayerLocalVtable;
const std::array<std::uintptr_t, 12> kLocalMethods = {
    rva::matchmaking::kPlayerLocalMethod0, rva::matchmaking::kPlayerLocalMethod1, rva::matchmaking::kPlayerLocalMethod2, rva::matchmaking::kPlayerLocalMethod3, rva::matchmaking::kPlayerLocalMethod4, rva::matchmaking::kPlayerLocalMethod5,
    rva::matchmaking::kPlayerLocalMethod6, rva::matchmaking::kPlayerLocalMethod7, rva::matchmaking::kPlayerLocalMethod8, rva::matchmaking::kPlayerLocalMethod9, rva::matchmaking::kPlayerLocalMethod10, rva::matchmaking::kPlayerLocalMethod11
};

// A nonzero LOCAL session key, deliberately outside any authenticated Steam
// account: universe/account-type/instance bits are all zero. It is never sent
// to Steam authentication or used for account/profile/leaderboard persistence.
constexpr std::uint64_t kOfflineGuestIdentity = 0x0000000053530002ULL;

std::uintptr_t g_module = 0;
LogCallback g_log = nullptr;
void* g_manager = nullptr;
bool g_registered = false;
bool g_suppressedLeaderboardLog = false;

void Log(const char* message) {
    if (g_log) g_log(message);
}

void NativeRelease();

#pragma pack(push, 4)
class OfflineGuest final : public IPlayerLocal {
public:
    std::uint64_t GetXUID() override { return identity; }
    int GetPlayerIndex() override { return controller; }
    const char* GetName() override { return name; }
    int GetOnlineState() override { return 0; } // STATE_OFFLINE
    const UserProfileData& GetPlayerProfileData() override { return profile; }
    const void* GetPlayerTitleData(int index) override {
        return static_cast<unsigned>(index) < 3 ? title[index] : nullptr;
    }
    void UpdatePlayerTitleData(int index, const void* data, int offset, int size) override {
        if (static_cast<unsigned>(index) >= 3 || !data || offset < 0 || size <= 0 ||
            offset > 1000 || size > 1000 - offset) return;
        std::memcpy(title[index] + offset, data, static_cast<std::size_t>(size));
        // Volatile guest data only. Never set native title-dirty flags: the
        // manager can call native SaveTitleData directly on registered slots.
    }
    void GetLeaderboardData(void*) override {}
    void UpdateLeaderboardData(void*) override {
        if (!g_suppressedLeaderboardLog) {
            Log("[guest] Offline guest leaderboard update ignored; no Steam account write");
            g_suppressedLeaderboardLog = true;
        }
    }

    // Native PlayerLocal extends the public nine-method ABI with these slots.
    // The game must never free plugin-owned storage through its heap or call
    // the host's Steam callback destructor. Slot9 has the x86 deleting-dtor
    // ABI (this, unsigned flags -> this); slots10/11 are Update/Release.
    virtual void* Destroy(unsigned) { NativeRelease(); return this; }
    virtual void Update() {}
    virtual void Release() { NativeRelease(); }

    // Match the native prefix and allocation extent for manager helper calls
    // that use fields directly. All pointer/callback storage remains null.
    std::uint32_t reserved = 0;                 // 0x004
    std::uint64_t identity = kOfflineGuestIdentity; // 0x008
    std::int32_t controller = 1;                 // 0x010
    std::int32_t onlineState = 0;                // 0x014
    char name[32] = "Local Guest";              // 0x018
    std::int32_t storageState = 0;               // 0x038
    float lastWriteTime = 0;                     // 0x03c
    UserProfileData profile{};                  // 0x040
    unsigned char title[3][1000]{};             // 0x084
    unsigned char titleDirty[3]{};              // 0xc3c
    unsigned char padding = 0;
    void* leaderboard = nullptr;                // 0xc40
    void* leaderboardCache = nullptr;           // 0xc44
    unsigned char callbacks[0x50]{};            // 0xc48
};
#pragma pack(pop)

static_assert(sizeof(OfflineGuest) == 0xc98, "Native local-player extent");
static_assert(offsetof(OfflineGuest, identity) == 8, "Native XUID offset");
static_assert(offsetof(OfflineGuest, controller) == 0x10, "Native controller offset");
static_assert(offsetof(OfflineGuest, name) == 0x18, "Native name offset");
static_assert(offsetof(OfflineGuest, profile) == 0x40, "Native profile offset");
static_assert(offsetof(OfflineGuest, titleDirty) == 0xc3c, "Native dirty flags offset");
OfflineGuest g_guest;

using bridge::Readable;

void** LocalSlot(void* manager, int controller) {
    return reinterpret_cast<void**>(static_cast<unsigned char*>(manager) + 8 + 4 * controller);
}

bool ValidManager(void* manager) {
    return g_module && Readable(reinterpret_cast<void*>(g_module + kManagerGlobal), 4) &&
        Readable(manager, 24) &&
        *reinterpret_cast<void***>(manager) == reinterpret_cast<void**>(g_module + kManagerVtable) &&
        *reinterpret_cast<void**>(g_module + kManagerGlobal) == manager;
}

void NativeRelease() {
    // OnGameUsersChanged clears each native slot after Release returns. Clear
    // ours here as well so reentrant UI queries cannot use a retiring profile.
    if (ValidManager(g_manager) && *LocalSlot(g_manager, 1) == &g_guest)
        *LocalSlot(g_manager, 1) = nullptr;
    g_registered = false;
    g_manager = nullptr;
    Log("[guest] Native profile release detached the offline guest");
}
}

bool Initialize(HMODULE matchmaking, LogCallback logger) {
    if (g_registered) return false;
    g_log = logger;
    g_module = reinterpret_cast<std::uintptr_t>(matchmaking);
    if (!g_module || !Readable(reinterpret_cast<void*>(g_module + kLocalVtable), 48) ||
        !Readable(reinterpret_cast<void*>(g_module + kManagerGlobal), 4)) {
        g_module = 0;
        Log("[guest] Matchmaking interface memory is unavailable");
        return false;
    }
    const auto methods = reinterpret_cast<const std::uintptr_t*>(g_module + kLocalVtable);
    for (std::size_t i = 0; i < kLocalMethods.size(); ++i) {
        if (methods[i] != g_module + kLocalMethods[i]) {
            g_module = 0;
            Log("[guest] Refusing unrecognized PlayerLocal vtable");
            return false;
        }
    }
    Log("[guest] Verified native PlayerLocal ABI; registration remains inactive");
    return true;
}

bool Register(void* manager, void* hostProfile) {
    if (!ValidManager(manager) || !Readable(hostProfile, 0x84) ||
        *LocalSlot(manager, 0) != hostProfile ||
        *reinterpret_cast<void***>(hostProfile) != reinterpret_cast<void**>(g_module + kLocalVtable)) {
        Log("[guest] Refusing foreign manager or host profile");
        return false;
    }
    if (g_registered) return g_manager == manager && *LocalSlot(manager, 1) == &g_guest;
    if (*LocalSlot(manager, 1) != nullptr) {
        Log("[guest] Existing controller1 profile was left untouched");
        return false;
    }
    const auto host = static_cast<IPlayerLocal*>(hostProfile);
    if (host->GetXUID() == kOfflineGuestIdentity) {
        Log("[guest] Offline guest identity conflicts with host");
        return false;
    }
    for (int index = 2; index < 4; ++index) {
        if (*LocalSlot(manager, index) != nullptr) {
            Log("[guest] Refusing a manager with additional local profiles");
            return false;
        }
    }

    g_guest.identity = kOfflineGuestIdentity;
    g_guest.controller = 1;
    g_guest.onlineState = 0;
    g_guest.storageState = 0;
    g_guest.lastWriteTime = 0;
    std::memset(g_guest.title, 0, sizeof(g_guest.title));
    std::memset(g_guest.titleDirty, 0, sizeof(g_guest.titleDirty));
    g_guest.profile = {};
    // Copy only scalar control preferences into independent guest memory.
    const auto& preferences = host->GetPlayerProfileData();
    g_guest.profile.difficulty = preferences.difficulty;
    g_guest.profile.sensitivity = preferences.sensitivity;
    g_guest.profile.yaxis = preferences.yaxis;
    g_guest.profile.vibration = preferences.vibration;
    g_guest.profile.action_autoaim = preferences.action_autoaim;
    g_guest.profile.action_autocenter = preferences.action_autocenter;
    g_guest.profile.action_movementcontrol = preferences.action_movementcontrol;
    std::snprintf(g_guest.name, sizeof(g_guest.name), "%.27s (2)", host->GetName());
    g_suppressedLeaderboardLog = false;
    g_manager = manager;
    g_registered = true;
    *LocalSlot(manager, 1) = &g_guest;
    Log("[guest] Registered controller1 as an independent offline local guest");
    return true;
}

void Unregister() {
    if (ValidManager(g_manager) && *LocalSlot(g_manager, 1) == &g_guest)
        *LocalSlot(g_manager, 1) = nullptr;
    const bool wasRegistered = g_registered;
    g_registered = false;
    g_manager = nullptr;
    if (wasRegistered) Log("[guest] Removed the plugin-owned offline guest profile");
}

void Shutdown() {
    Unregister();
    g_module = 0;
    g_log = nullptr;
}

bool IsLocalSplitEnabled() {
    return g_registered && ValidManager(g_manager) && *LocalSlot(g_manager, 1) == &g_guest;
}
std::uint64_t GuestXuid() { return kOfflineGuestIdentity; }
const char* GuestName() { return g_guest.name; }
void* GuestProfile() { return IsLocalSplitEnabled() ? &g_guest : nullptr; }
}
