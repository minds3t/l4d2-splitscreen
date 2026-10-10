#include "join.h"
#include "bridge.h"
#include "readable.h"
#include "build_10097.h"
#include "lobby.h"

#include <cstring>

namespace join {
namespace {
using bridge::Readable;
// engine.dll, CHostState::State_NewGame (+1a0220). It reads and clears the
// host state's split-screen request byte (this+329) and passes it as the
// fourth argument of Host_NewGame (+18ea70). With that argument set and the
// game DLL's split-screen capacity (+42ffc0) above one, Host_NewGame stuffs
// "connect_splitscreen localhost:<port> <capacity>" instead of
// "connect localhost:<port>". The PC `map` command never sets the byte.
const std::uint32_t kStateNewGame = rva::engine::kStateNewGame;
constexpr std::uint32_t kSplitRequestOffset = 0x329;
const std::uint32_t kSplitCapacity = rva::engine::kSplitCapacity;
using StateFn = void(__thiscall*)(void*);
StateFn originalState = nullptr;
std::uintptr_t engine = 0;
bool installed = false;

void __fastcall StateNewGameHook(void* self, void*) {
    if (installed && bridge::IsLocalSplitEnabled() && lobby::HasOwnedOfflineSession() &&
        Readable(self, kSplitRequestOffset + 1)) {
        const int capacity = *reinterpret_cast<const int*>(engine + kSplitCapacity);
        if (capacity >= 2) {
            static_cast<unsigned char*>(self)[kSplitRequestOffset] = 1;
            bridge::Log("[join] Owned offline map start: engine connects %d local players itself", capacity);
        } else {
            bridge::Log("[join] Engine split-screen capacity is %d; player two cannot join", capacity);
        }
    }
    originalState(self);
}

// Teardown guard. CNetChan::Shutdown(reason) (+1bb950) writes a disconnect
// message and transmits once more. SendDatagram then merges every attached
// split-screen sub-channel through +1b9240 (three calls, one per stream). On
// quit or disconnect the engine has already destroyed player two's
// sub-channel without detaching it from the host channel, so that merge reads
// freed memory: an intermittent crash at engine+1b92a1 (2026-10-10 06:01).
// The final disconnect packet needs no split-player payload; skip the merge
// only while a Shutdown is on the stack.
const std::uint32_t kChannelShutdown = rva::engine::kChannelShutdown;
const std::uint32_t kSplitMerge = rva::engine::kSplitMerge;
using ShutdownFn = void(__thiscall*)(void*, const char*);
using MergeFn = void(__thiscall*)(void*, int, void*);
ShutdownFn originalShutdown = nullptr;
MergeFn originalMerge = nullptr;
unsigned shutdownDepth = 0;
bool guardLogged = false;

void __fastcall ChannelShutdownHook(void* self, void*, const char* reason) {
    ++shutdownDepth;
    __try {
        originalShutdown(self, reason);
    } __finally {
        --shutdownDepth;
    }
}

void __fastcall SplitMergeHook(void* self, void*, int stream, void* buffer) {
    if (shutdownDepth) {
        if (!guardLogged) {
            bridge::Log("[join] Skipped split-player merge in a closing net channel (stale sub-channel guard)");
            guardLogged = true;
        }
        return;
    }
    originalMerge(self, stream, buffer);
}

// Drop-in guard. When player two joins a running game (ss_connect), their
// view is drawn as soon as the engine adds the split-screen user, a few
// frames before their player entity arrives. The screen-effect query at
// client+257830 (a C_TerrorPlayer virtual, four float results) dereferences
// the active slot's local player without a check: crash at client+2578d2
// (2026-10-10 17:25). No local player means no effect to report.
using ScreenEffectFn = void(__thiscall*)(void*, float*, float*, float*, float*);
using LocalPlayerFn = void*(__cdecl*)(int);
ScreenEffectFn originalScreenEffect = nullptr;
LocalPlayerFn localPlayer = nullptr;
bool effectGuardLogged = false;

void __fastcall ScreenEffectHook(void* self, void*, float* a, float* b, float* c, float* d) {
    if (!localPlayer(-1)) {
        *a = *b = *c = *d = 0.0f;
        if (!effectGuardLogged) {
            bridge::Log("[join] Skipped a screen-effect query for a joining player without an entity yet");
            effectGuardLogged = true;
        }
        return;
    }
    originalScreenEffect(self, a, b, c, d);
}

bool Bytes(std::uint32_t rva, const unsigned char* expected, std::size_t size) {
    const auto address = reinterpret_cast<const void*>(engine + rva);
    return Readable(address, size) && std::memcmp(address, expected, size) == 0;
}
}

bool Install() {
    if (installed) return true;
    HMODULE module = bridge::Engine();
    if (!module || !bridge::IsLocalSplitEnabled()) return false;
    engine = reinterpret_cast<std::uintptr_t>(module);
    static constexpr unsigned char prefix[] = {0x55,0x8b,0xec,0x51,0x53,0x56,0x57};
    // mov al,[esi+329]; add esp,4; mov byte ptr [esi+329],0
    static constexpr unsigned char request[] = {
        0x8a,0x86,0x29,0x03,0x00,0x00,0x83,0xc4,0x04,0xc6,0x86,0x29,0x03,0x00,0x00,0x00};
    // mov ecx,[ebp-4]; movzx edx,byte ptr [esi+326]; push 0; push 0; push ecx; push edx
    static constexpr unsigned char forward[] = {
        0x8b,0x4d,0xfc,0x0f,0xb6,0x96,0x26,0x03,0x00,0x00,0x6a,0x00,0x6a,0x00,0x51,0x52};
    // Host_NewGame: cmp byte ptr [ebp+14],0; je; mov eax,[capacity]
    static constexpr unsigned char gate[] = {0x80,0x7d,0x14,0x00,0x74,0x24,0xa1};
    std::uint32_t capacityAddress = 0;
    if (!Bytes(rva::engine::kStateNewGameRequestRead, request, sizeof(request)) || !Bytes(rva::engine::kStateNewGameForward, forward, sizeof(forward)) ||
        !Bytes(rva::engine::kHostNewGameSplitGate, gate, sizeof(gate)) || !Readable(reinterpret_cast<void*>(engine + rva::engine::kHostNewGameCapacityOperand), 4)) {
        bridge::Log("[join] Native two-player connect unavailable: unexpected engine code");
        engine = 0;
        return false;
    }
    std::memcpy(&capacityAddress, reinterpret_cast<void*>(engine + rva::engine::kHostNewGameCapacityOperand), sizeof(capacityAddress));
    if (capacityAddress != engine + kSplitCapacity || !Readable(reinterpret_cast<void*>(engine + kSplitCapacity), 4)) {
        bridge::Log("[join] Native two-player connect unavailable: unexpected capacity global");
        engine = 0;
        return false;
    }
    if (!bridge::InstallHook("native two-player map connect", module, kStateNewGame,
            prefix, sizeof(prefix), reinterpret_cast<void*>(&StateNewGameHook),
            reinterpret_cast<void**>(&originalState))) {
        engine = 0;
        return false;
    }
    // Independent of the connect request: any exit from a two-player game.
    static constexpr unsigned char shutdownPrefix[] = {0x55,0x8b,0xec,0x56,0x8b,0xf1,0x83,0xbe,0x8c,0x00,0x00,0x00,0x00};
    static constexpr unsigned char mergePrefix[] = {0x55,0x8b,0xec,0x83,0xec,0x5c,0x53,0x8b,0xd9};
    // The three merge calls in SendDatagram are the only ones in the module.
    static const std::uint32_t mergeCalls[] = {rva::engine::kSplitMergeCall0, rva::engine::kSplitMergeCall1, rva::engine::kSplitMergeCall2};
    bool guard = true;
    for (const auto call : mergeCalls) {
        std::int32_t displacement = 0;
        const auto site = reinterpret_cast<const unsigned char*>(engine + call);
        if (!Readable(site, 5) || site[0] != 0xe8) { guard = false; break; }
        std::memcpy(&displacement, site + 1, sizeof(displacement));
        if (static_cast<std::int64_t>(call) + 5 + displacement != kSplitMerge) { guard = false; break; }
    }
    shutdownDepth = 0;
    guardLogged = false;
    if (!guard ||
        !bridge::InstallHook("net channel shutdown scope", module, kChannelShutdown,
            shutdownPrefix, sizeof(shutdownPrefix), reinterpret_cast<void*>(&ChannelShutdownHook),
            reinterpret_cast<void**>(&originalShutdown)) ||
        !bridge::InstallHook("stale split sub-channel guard", module, kSplitMerge,
            mergePrefix, sizeof(mergePrefix), reinterpret_cast<void*>(&SplitMergeHook),
            reinterpret_cast<void**>(&originalMerge))) {
        bridge::Log("[join] Teardown guard unavailable; quitting from a running game keeps its stock risk");
    }
    // push ebp; mov ebp,esp; push ebx; push esi; mov esi,ecx; mov ecx,[client engine]
    static constexpr unsigned char effectPrefix[] = {0x55,0x8b,0xec,0x53,0x56,0x8b,0xf1,0x8b,0x0d};
    // C_BasePlayer::GetLocalPlayer(slot): push ebp; mov ebp,esp; mov eax,[ebp+8]; cmp eax,-1
    static constexpr unsigned char localPrefix[] = {0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xf8,0xff};
    effectGuardLogged = false;
    HMODULE client = bridge::Client();
    const auto local = client ? reinterpret_cast<unsigned char*>(client) + rva::client::kLocalPlayer : nullptr;
    if (!local || !Readable(local, sizeof(localPrefix)) || std::memcmp(local, localPrefix, sizeof(localPrefix)) != 0 ||
        (localPlayer = reinterpret_cast<LocalPlayerFn>(local),
         !bridge::InstallHook("joining player screen-effect guard", client, rva::client::kScreenEffectQuery,
            effectPrefix, sizeof(effectPrefix), reinterpret_cast<void*>(&ScreenEffectHook),
            reinterpret_cast<void**>(&originalScreenEffect)))) {
        bridge::Log("[join] Drop-in guard unavailable; joining during a game keeps its stock risk");
    }
    installed = true;
    bridge::Log("[join] Engine split-screen connect request installed; capacity=%d",
        *reinterpret_cast<const int*>(engine + kSplitCapacity));
    return true;
}

void Shutdown() {
    installed = false;
    originalState = nullptr;
    originalShutdown = nullptr;
    originalMerge = nullptr;
    originalScreenEffect = nullptr;
    shutdownDepth = 0;
    engine = 0;
}

bool Active() { return installed; }
}
