#include "identity.h"
#include "readable.h"
#include "build_10097.h"
#include "guest.h"
#include "lobby.h"
#include "bridge.h"

#include <array>
#include <cstring>
#include <intrin.h>

namespace identity {
namespace {
void* engineServer = nullptr;
std::uintptr_t engine = 0;
std::array<void*, 32> clients{};
using XuidFn = std::uint64_t(__thiscall*)(void*, void*);
XuidFn originalXuid = nullptr;
std::uintptr_t serverModule = 0;
bool avatarBridgeInstalled = false;
std::array<bool, 4> substitutionLogged{};

// Return addresses immediately after IVEngineServer::GetClientXUID calls in
// server.dll. Each accesses the same Director avatar map (this+0x11c):
// +0x274faa receives avatarinfo; +0x26cded retrieves an edict's cached record;
// +0x26cf23 and +0x26da98 read preferences during native player assignment.
// Account/stat submissions (e.g. +0x2a5d36) deliberately retain native zero.
const std::array<std::uint32_t, 4> avatarCallers = {
    rva::server::kAvatarInfoReceiveReturn, rva::server::kAvatarRecordLookupReturn, rva::server::kAvatarPreferenceReturn0, rva::server::kAvatarPreferenceReturn1
};

using bridge::Readable;

int Index(void* edict) {
    // Native NUM_FOR_EDICT (+0x1e0030), also used by GetClientXUID. Validate
    // its actual pool bounds first: its invalid-pointer path calls Host_Error.
    // Pool globals and 16-byte extent are read directly from that function.
    if (!engine || !edict || !Readable(reinterpret_cast<void*>(engine + rva::engine::kEdictCount), 12)) return -1;
    const auto start = *reinterpret_cast<std::uintptr_t*>(engine + rva::engine::kEdictPool);
    const int count = *reinterpret_cast<int*>(engine + rva::engine::kEdictCount);
    const auto pointer = reinterpret_cast<std::uintptr_t>(edict);
    if (!start || count <= 0 || pointer < start || (pointer - start) % 16 ||
        (pointer - start) / 16 >= static_cast<std::uintptr_t>(count)) return -1;
    return reinterpret_cast<int(__cdecl*)(void*)>(engine + rva::engine::kNumForEdict)(edict);
}

bool IsOwnedGuest(void* edict) {
    if (!bridge::IsLocalSplitEnabled() || !lobby::HasOwnedOfflineSession() ||
        !guest::IsLocalSplitEnabled() || !originalXuid) return false;
    __try {
        const int index = Index(edict);
        if (index <= 0 || !bridge::Virtual<bool(__thiscall*)(void*, int)>(engineServer, 114)(engineServer, index))
            return false;
        void* parent = bridge::Virtual<void*(__thiscall*)(void*, int)>(engineServer, 115)(engineServer, index);
        const int parentIndex = Index(parent);
        if (parentIndex <= 0 || bridge::Virtual<bool(__thiscall*)(void*, int)>(engineServer, 114)(engineServer, parentIndex))
            return false;
        if (bridge::Virtual<int(__thiscall*)(void*, int)>(engineServer, 116)(engineServer, parentIndex) != 1 ||
            bridge::Virtual<void*(__thiscall*)(void*, int, int)>(engineServer, 117)(engineServer, parentIndex, 1) != edict)
            return false;
        void* host = bridge::HostProfile();
        if (!host) return false;
        const auto hostXuid = bridge::Virtual<std::uint64_t(__thiscall*)(void*)>(host, 0)(host);
        // Prove the attached parent is this machine's real host. Never assign
        // our guest identity to a bot or another machine's split-screen guest.
        return hostXuid && originalXuid(engineServer, parent) == hostXuid;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        bridge::Log("[identity] Guest ownership check failed (%08lx)", GetExceptionCode());
        return false;
    }
}

std::uint64_t __fastcall AvatarXuidHook(void* self, void*, void* edict) {
    const auto nativeXuid = originalXuid(self, edict);
    if (nativeXuid || self != engineServer) return nativeXuid;
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    for (std::size_t i = 0; i < avatarCallers.size(); ++i) {
        if (caller != serverModule + avatarCallers[i]) continue;
        if (!IsOwnedGuest(edict)) return nativeXuid;
        if (!substitutionLogged[i]) {
            bridge::Log("[identity] Native avatar cache guest identity: server+%06x entity=%d xuid=%016llx",
                avatarCallers[i], Index(edict), static_cast<unsigned long long>(guest::GuestXuid()));
            substitutionLogged[i] = true;
        }
        return guest::GuestXuid();
    }
    return nativeXuid;
}

void Report(void* edict, const char* event) {
    if (!engineServer) return;
    __try {
        const int index = Index(edict);
        if (index <= 0) {
            bridge::Log("[identity] %s edict=%p outside current engine edict pool", event, edict);
            return;
        }
        // Public VEngineServer022 order verified against eiface.h and the
        // installed engine vtable. All data comes from engine queries.
        const int userId = bridge::Virtual<int(__thiscall*)(void*, void*)>(engineServer, 16)(engineServer, edict);
        const char* network = bridge::Virtual<const char*(__thiscall*)(void*, void*)>(engineServer, 17)(engineServer, edict);
        const bool split = bridge::Virtual<bool(__thiscall*)(void*, int)>(engineServer, 114)(engineServer, index);
        void* parent = bridge::Virtual<void*(__thiscall*)(void*, int)>(engineServer, 115)(engineServer, index);
        const int attached = bridge::Virtual<int(__thiscall*)(void*, int)>(engineServer, 116)(engineServer, index);
        void* child = !split && attached > 0
            ? bridge::Virtual<void*(__thiscall*)(void*, int, int)>(engineServer, 117)(engineServer, index, 1)
            : nullptr;
        const auto xuid = bridge::Virtual<std::uint64_t(__thiscall*)(void*, void*)>(engineServer, 136)(engineServer, edict);
        bridge::Log("[identity] %s entity=%d userid=%d edict=%p engineXUID=%016llx network=%.128s split=%d parent=%p attached=%d guestSlot1=%p matchmakingGuest=%016llx",
            event, index, userId, edict, static_cast<unsigned long long>(xuid), network ? network : "<null>",
            split, parent, attached, child, static_cast<unsigned long long>(guest::GuestXuid()));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        bridge::Log("[identity] %s read failed for edict=%p (%08lx)", event, edict, GetExceptionCode());
    }
}
}

bool Initialize(void* server) {
    ResetClients();
    engine = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"engine.dll"));
    // Additional exact-interface gate after bridge's whole-module fingerprint.
    if (!engine || !Readable(server, 4) ||
        *reinterpret_cast<std::uintptr_t*>(server) != engine + rva::engine::kEngineServerVtable ||
        *reinterpret_cast<std::uintptr_t*>(engine + rva::engine::kEngineServerVtable + 0x220) != engine + rva::engine::kGetClientXuid) {
        engine = 0;
        engineServer = nullptr;
        bridge::Log("[identity] VEngineServer022 identity diagnostics unavailable");
        return false;
    }
    engineServer = server;
    bridge::Log("[identity] Read-only VEngineServer022 client identity diagnostics ready");
    return true;
}

bool InstallAvatarBridge() {
    if (avatarBridgeInstalled) return true;
    // bridge::Server() is exposed only after the exact server.dll SHA-256
    // fingerprint passed. No fixed server callsite is used without that gate.
    HMODULE verifiedServer = bridge::Server();
    if (!engineServer || !engine || !verifiedServer || !bridge::IsLocalSplitEnabled()) return false;
    serverModule = reinterpret_cast<std::uintptr_t>(verifiedServer);
    for (const auto offset : avatarCallers) {
        const auto call = reinterpret_cast<const unsigned char*>(serverModule + offset - 2);
        if (!Readable(call, 2) || call[0] != 0xff || call[1] != 0xd0) {
            bridge::Log("[identity] Refusing changed avatar cache callsite server+%06x", offset);
            serverModule = 0;
            return false;
        }
    }
    static const std::uint8_t bytes[] = {0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x50};
    if (!bridge::InstallHook("offline guest avatar identity", reinterpret_cast<HMODULE>(engine), rva::engine::kGetClientXuid,
        bytes, sizeof(bytes), reinterpret_cast<void*>(&AvatarXuidHook), reinterpret_cast<void**>(&originalXuid))) {
        serverModule = 0;
        return false;
    }
    avatarBridgeInstalled = true;
    substitutionLogged.fill(false);
    bridge::Log("[identity] Guest identity scoped to four native avatar-cache callsites; other callers remain native");
    return true;
}

void ResetAvatarBridge() {
    originalXuid = nullptr;
    serverModule = 0;
    avatarBridgeInstalled = false;
    substitutionLogged.fill(false);
}

bool IsLocalGuest(void* edict) { return IsOwnedGuest(edict); }

void ObserveClient(void* edict, const char* event) {
    if (!engineServer || !edict || !event) return;
    Report(edict, event);
    const bool disconnected = std::strstr(event, "Disconnect") || std::strstr(event, "disconnect");
    for (auto& item : clients) {
        if (item == edict) {
            if (disconnected) item = nullptr;
            return;
        }
    }
    if (disconnected) return;
    for (auto& item : clients) if (!item) { item = edict; return; }
    bridge::Log("[identity] Client diagnostic cache full; callback was logged without retention");
}

void ResetClients() {
    clients.fill(nullptr);
    substitutionLogged.fill(false);
}

void DumpState() {
    unsigned count = 0;
    for (void* edict : clients) if (edict) { Report(edict, "status"); ++count; }
    bridge::Log("[identity] %u callback edicts tracked; interface=%p avatarBridge=%d", count, engineServer, avatarBridgeInstalled);
}

void Shutdown() {
    ResetAvatarBridge();
    ResetClients();
    engineServer = nullptr;
    engine = 0;
}
}
