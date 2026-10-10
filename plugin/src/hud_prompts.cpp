#include "hud_prompts.h"
#include "readable.h"
#include "build_10097.h"
#include "bridge.h"
#include "lobby.h"

#include <array>
#include <cstring>
#include <intrin.h>

namespace hud_prompts {
namespace {
using LookupFn = const char*(__thiscall*)(void*, const char*, int, int, int);
using KeySlotFn = int(__cdecl*)(int);
using VoteStartFn = void(__thiscall*)(void*, void*);
using FindChildFn = void*(__thiscall*)(void*, const char*, bool);
LookupFn originalLookup = nullptr;
VoteStartFn originalVoteStart = nullptr;
FindChildFn findChild = nullptr;
KeySlotFn keySlot = nullptr;
void* matchBinding = nullptr;
void* engineClient = nullptr;
void* inputSystem = nullptr;
std::uintptr_t client = 0;
bool installed = false;
struct Report { int slot{-1}, device{-1}; char command[128]{}, binding[64]{}; };
std::array<Report, 16> reports{};
unsigned nextReport = 0;

using bridge::Readable;

template<class T> T Field(void* object, std::size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const char*>(object) + offset);
}

bool BoundedName(const char* text, std::size_t capacity) {
    return Readable(text, capacity) && text[0] && std::memchr(text, 0, capacity);
}

int KeyboardOwner() {
    void* variable = reinterpret_cast<void*>(client + rva::client::kInForceUserCvar);
    if (!Readable(variable, 0x34)) return -1;
    void* parent = Field<void*>(variable, 0x1c);
    if (!Readable(parent, 0x34)) return -1;
    const char* name = Field<const char*>(parent, 0xc);
    if (!Readable(name, 13) || std::memcmp(name, "in_forceuser", 13)) return -1;
    return Field<int>(parent, 0x30);
}

bool PadConnected(int slot) {
    // Same native XInput records used by the inventory binding adapter.
    const auto offset = 0x3bf4u + static_cast<unsigned>(slot) * 0x90u;
    return Readable(inputSystem, offset + 9) && Field<int>(inputSystem, offset) == slot &&
        Field<unsigned char>(inputSystem, offset + 8) != 0;
}

__declspec(noinline) const char* MatchCommand(int code, const char* command) {
    // Native engine matcher takes the button code in EDI and the command on
    // the stack. It retains Source's semicolon/+command matching and names.
    const char* result = nullptr;
    __asm {
        push edi
        mov edi, code
        push command
        call dword ptr [matchBinding]
        add esp, 4
        mov result, eax
        pop edi
    }
    return result;
}

// index selects the n-th key of that device bound to the command, as the
// native lookup's own index argument does.
const char* DeviceBinding(int slot, int device, const char* command, int index = 0) {
    if (*command == '+') ++command;
    const int first = device == 1 ? 0x72 : 0;
    const int last = device == 1 ? 0x131 : 0x71;
    for (int code = first; code <= last; ++code) {
        if (device == 1 && keySlot(code) != slot) continue;
        const char* result = MatchCommand(code, command);
        if (BoundedName(result, 64) && index-- == 0) return result;
    }
    // Preserve the two aliases in native Key_LookupBindingEx without its
    // fallback to an unfiltered device search or another pad's bindings.
    const char* alias = !_stricmp(command, "duck") ? "toggle_duck" :
        (!_stricmp(command, "zoom") ? "toggle_zoom" : nullptr);
    if (alias) {
        for (int code = first; code <= last; ++code) {
            if (device == 1 && keySlot(code) != slot) continue;
            const char* result = MatchCommand(code, alias);
            if (BoundedName(result, 64) && index-- == 0) return result;
        }
    }
    return nullptr;
}

bool LocatorCallsVerified() {
    // mov reg,[reg+26c] ... call reg at each of the three instructor lookups.
    static const struct { std::uint32_t load; unsigned char loadByte; std::uint32_t call; unsigned char callByte; } sites[] = {
        {rva::client::kLocatorLookup0Load, 0x92, rva::client::kLocatorLookup0Call, 0xd2}, {rva::client::kLocatorLookup1Load, 0x80, rva::client::kLocatorLookup1Call, 0xd0}, {rva::client::kLocatorLookup2Load, 0x80, rva::client::kLocatorLookup2Call, 0xd0}};
    for (const auto& site : sites) {
        const auto load = reinterpret_cast<const unsigned char*>(client + site.load);
        const auto call = reinterpret_cast<const unsigned char*>(client + site.call);
        static constexpr unsigned char slot155[] = {0x6c, 0x02, 0x00, 0x00};
        if (!Readable(load, 6) || load[0] != 0x8b || load[1] != site.loadByte ||
            std::memcmp(load + 2, slot155, sizeof(slot155)) ||
            !Readable(call, 2) || call[0] != 0xff || call[1] != site.callByte) return false;
    }
    return true;
}

void ReportBinding(int slot, int device, const char* command, const char* binding) {
    const char* name = BoundedName(binding, 64) ? binding : "<unbound>";
    for (const auto& report : reports) {
        if (report.slot == slot && report.device == device &&
            !std::strcmp(report.command, command) && !std::strcmp(report.binding, name)) return;
    }
    auto& report = reports[nextReport++ % reports.size()];
    report.slot = slot;
    report.device = device;
    std::strcpy(report.command, command);
    std::strcpy(report.binding, name);
    bridge::Log("[hud_prompts] Native prompt slot=%d device=%s command=%s binding=%s; input unchanged",
        slot, device == 1 ? "controller" : device == 0 ? "keyboard" : "none", command, name);
}

// Return addresses of the three lookups in native CLocatorTarget::SetBinding
// (client+2b1600), which collects up to eight keys for an instructor hint. It
// passes the active split-screen slot, but picks one device for both players
// from the joystick cvar, and the PC engine lookup ignores the slot anyway.
const std::uint32_t locatorReturns[] = {rva::client::kLocatorLookup0Return, rva::client::kLocatorLookup1Return, rva::client::kLocatorLookup2Return};

bool LocatorCaller(std::uintptr_t caller) {
    for (const auto rva : locatorReturns) if (caller == client + rva) return true;
    return false;
}

const char* SelectBinding(void* self, const char* command, int slot, int index, int device,
    std::uintptr_t caller) {
    if (installed && self == engineClient && LocatorCaller(caller) &&
        slot >= 0 && slot <= 1 && index >= 0 && index < 8 && (device == 0 || device == 1) &&
        bridge::IsLocalSplitEnabled() && lobby::HasOwnedOfflineSession() && BoundedName(command, 128)) {
        const int ownerDevice = PadConnected(slot) ? 1 : (KeyboardOwner() == slot ? 0 : -1);
        const char* result = ownerDevice >= 0 ? DeviceBinding(slot, ownerDevice, command, index) : nullptr;
        if (index == 0) ReportBinding(slot, ownerDevice, command, result);
        return result;
    }
    // Two native callers pass the right local slot with device -1: CBindPanel::
    // Update (its stored slot) and the %+command% replacement in hint and tip
    // text (the active split-screen slot). No other engine lookup, menu, online
    // session or binding listener changes.
    if (!installed || (caller != client + rva::client::kBindPanelLookupReturn &&
            caller != client + rva::client::kHintTextLookupReturn) || self != engineClient ||
        slot < 0 || slot > 1 || index != 0 || device != -1 ||
        !bridge::IsLocalSplitEnabled() || !lobby::HasOwnedOfflineSession() ||
        !BoundedName(command, 128)) return originalLookup(self, command, slot, index, device);
    const int ownerDevice = PadConnected(slot) ? 1 : (KeyboardOwner() == slot ? 0 : -1);
    const char* result = ownerDevice >= 0 ? DeviceBinding(slot, ownerDevice, command) : nullptr;
    ReportBinding(slot, ownerDevice, command, result);
    return result;
}

const char* __fastcall LookupHook(void* self, void*, const char* command, int slot, int index, int device) {
    return SelectBinding(self, command, slot, index, device,
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
}

void SelectVoteLabels(void* self) {
    // CHudVote embeds CHudElement at+17c; its native local-slot accessor reads
    // +14, hence root+190. The message handler has already handled the vote,
    // labels, animations and counts before this presentation-only selection.
    if (!installed || !Readable(self, 0x1dc) ||
        Field<std::uintptr_t>(self, 0) != client + rva::client::kVoteHudVtable ||
        Field<std::uintptr_t>(self, 0x17c) != client + rva::client::kVoteHudElementVtable ||
        !bridge::IsLocalSplitEnabled() || !lobby::HasOwnedOfflineSession()) return;
    const int slot = Field<int>(self, 0x190);
    if (slot < 0 || slot > 1) return;
    void* active = Field<void*>(self, 0x1b8);
    if (!Readable(active, sizeof(void*))) return;
    constexpr const char* names[] = {
        "YesPCLabel", "NoPCLabel", "YesLeftLabel", "YesIcon", "YesRightLabel",
        "NoLeftLabel", "NoIcon", "NoRightLabel"
    };
    void* labels[8]{};
    for (unsigned index = 0; index < 8; ++index) {
        labels[index] = findChild(active, names[index], false);
        if (!Readable(labels[index], sizeof(void*)) ||
            !Readable(Field<void*>(labels[index], 0), 0x88) ||
            reinterpret_cast<std::uintptr_t>(bridge::Virtual<void*>(labels[index], 33)) != client + rva::client::kLabelSetVisible) {
            bridge::Log("[hud_prompts] Vote labels not adapted: missing/unexpected %s", names[index]);
            return;
        }
    }
    const int device = PadConnected(slot) ? 1 : (KeyboardOwner() == slot ? 0 : -1);
    using SetVisibleFn = void(__thiscall*)(void*, bool);
    for (unsigned index = 0; index < 8; ++index) {
        const bool show = index < 2 ? device == 0 : device == 1;
        bridge::Virtual<SetVisibleFn>(labels[index], 33)(labels[index], show);
    }
    bridge::Log("[hud_prompts] Vote labels slot=%d device=%s; native vote state/actions unchanged",
        slot, device == 1 ? "controller" : device == 0 ? "keyboard" : "none");
}

void __fastcall VoteStartHook(void* self, void*, void* message) {
    originalVoteStart(self, message);
    SelectVoteLabels(self);
}
}

bool Install() {
    HMODULE engineModule = bridge::Engine();
    HMODULE inputModule = bridge::InputSystem();
    HMODULE clientModule = bridge::Client();
    if (!engineModule || !inputModule || !clientModule || originalLookup ||
        !bridge::IsLocalSplitEnabled()) return false;
    const auto engine = reinterpret_cast<std::uintptr_t>(engineModule);
    const auto input = reinterpret_cast<std::uintptr_t>(inputModule);
    client = reinterpret_cast<std::uintptr_t>(clientModule);
    using Factory = void*(__cdecl*)(const char*, int*);
    auto engineFactory = reinterpret_cast<Factory>(GetProcAddress(engineModule, "CreateInterface"));
    auto inputFactory = reinterpret_cast<Factory>(GetProcAddress(inputModule, "CreateInterface"));
    engineClient = engineFactory ? engineFactory("VEngineClient013", nullptr) : nullptr;
    inputSystem = inputFactory ? inputFactory("InputSystemVersion001", nullptr) : nullptr;
    constexpr unsigned char matcherBytes[] = {0x55,0x8b,0xec,0x81,0xec,0x04,0x01,0x00,0x00};
    constexpr unsigned char keySlotBytes[] = {0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xf8,0x72};
    constexpr unsigned char findBytes[] = {0x55,0x8b,0xec,0x83,0xec,0x0c};
    // The call sequence actually loads EDI from the CBindPanel slot, then
    // pushes device=-1/index=0/slot/command and invokes interface slot155.
    // The hint-text replacement pushes the same arguments with the slot in ESI.
    constexpr unsigned char hintTextCall[] = {
        0x6a,0xff,0x6a,0x00,0x56,0x50,0x8b,0x82,0x6c,0x02,0x00,0x00,0xff,0xd0};
    constexpr unsigned char lookupCall[] = {
        0x6a,0xff,0x6a,0x00,0x57,0x50,0x8b,0x82,0x6c,0x02,0x00,0x00,0xff,0xd0};
    if (engineClient != reinterpret_cast<void*>(engine + rva::engine::kEngineClient) ||
        !Readable(engineClient, 4) || Field<std::uintptr_t>(engineClient, 0) != engine + rva::engine::kEngineClientVtable ||
        reinterpret_cast<std::uintptr_t>(bridge::Virtual<void*>(engineClient, 155)) != engine + rva::engine::kKeyLookupBindingEx ||
        !Readable(inputSystem, 0x3e34) || Field<std::uintptr_t>(inputSystem, 0) != input + rva::inputsystem::kInputSystemVtable ||
        reinterpret_cast<std::uintptr_t>(bridge::Virtual<void*>(inputSystem, 27)) != input + rva::inputsystem::kInputSystemSlot27 ||
        std::memcmp(reinterpret_cast<void*>(engine + rva::engine::kMatchBinding), matcherBytes, sizeof(matcherBytes)) ||
        std::memcmp(reinterpret_cast<void*>(engine + rva::engine::kKeySlot), keySlotBytes, sizeof(keySlotBytes)) ||
        std::memcmp(reinterpret_cast<void*>(client + rva::client::kPanelFindChild), findBytes, sizeof(findBytes)) ||
        std::memcmp(reinterpret_cast<void*>(client + rva::client::kBindPanelLookupCall), lookupCall, sizeof(lookupCall)) ||
        std::memcmp(reinterpret_cast<void*>(client + rva::client::kHintTextLookupCall), hintTextCall, sizeof(hintTextCall)) ||
        !LocatorCallsVerified()) {
        bridge::Log("[hud_prompts] Refusing unexpected native CBindPanel lookup/interfaces");
        return false;
    }
    keySlot = reinterpret_cast<KeySlotFn>(engine + rva::engine::kKeySlot);
    matchBinding = reinterpret_cast<void*>(engine + rva::engine::kMatchBinding);
    findChild = reinterpret_cast<FindChildFn>(client + rva::client::kPanelFindChild);
    constexpr unsigned char lookupBytes[] = {0x55,0x8b,0xec,0x8b,0x45,0x14};
    constexpr unsigned char voteBytes[] = {0x55,0x8b,0xec,0xb8,0x38,0x10,0x00,0x00};
    if (!bridge::InstallHook("CBindPanel per-player binding device", engineModule, rva::engine::kKeyLookupBindingEx,
        lookupBytes, sizeof(lookupBytes), reinterpret_cast<void*>(&LookupHook),
        reinterpret_cast<void**>(&originalLookup)) ||
        !bridge::InstallHook("Vote HUD per-player instruction visibility", clientModule, rva::client::kVoteStart,
        voteBytes, sizeof(voteBytes), reinterpret_cast<void*>(&VoteStartHook),
        reinterpret_cast<void**>(&originalVoteStart))) return false;
    installed = true;
    bridge::Log("[hud_prompts] Native CBindPanel device lookup installed; exact owned-offline callsite only");
    return true;
}

void Shutdown() {
    installed = false;
    originalLookup = nullptr;
    originalVoteStart = nullptr;
    findChild = nullptr;
    keySlot = nullptr;
    matchBinding = engineClient = inputSystem = nullptr;
    client = 0;
    reports = {};
    nextReport = 0;
}
}
