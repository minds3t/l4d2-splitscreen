#include "devices.h"
#include "bridge.h"
#include "readable.h"
#include "build_10097.h"
#include "settings.h"
#include <cstdint>
#include <cstring>

namespace devices {
namespace {
using bridge::Readable;
void* inputSystem = nullptr;
int loggedSlot = -1;

// CInputSystem::Init (+2650) calls InitializeXDevices and sets the byte at
// InputSystem+3678 only if that found a controller; otherwise it sets up
// legacy joysticks. The frame poll (+1470) reads that byte: set, it runs the
// XInput poll (+6250), which itself handles a pad that appears or disappears
// (re-initializes the record, posts the inserted / unplugged event). Clear,
// XInput is never asked again, so a pad switched on after launch stays dead.
constexpr std::size_t kXInputPolled = 0x3678;
using GetStateFn = unsigned long(__stdcall*)(unsigned long, void*);
GetStateFn* getState = nullptr;
void* foundCvar = nullptr;
void(__thiscall* setCvarInt)(void*, int) = nullptr;
ULONGLONG nextProbe = 0;

// InputSystem keeps three XInput entry points, resolved once at Init (+61c0):
// state, vibration and capabilities, each taking the controller index first.
// To make keyboard and mouse player one, controllers 1 and 2 trade places
// below the game: the first pad arrives as controller 2, and the game's own
// controller-N-is-player-N handling (buttons, sticks, bindings, prompts,
// vibration) gives it to player two. The poll sees the change as one pad
// unplugged and another plugged in.
using SetStateFn = unsigned long(__stdcall*)(unsigned long, void*);
using CapabilitiesFn = unsigned long(__stdcall*)(unsigned long, unsigned long, void*);
struct XInputTable {
    GetStateFn getState;
    SetStateFn setState;
    CapabilitiesFn capabilities;
};
XInputTable* table = nullptr;
XInputTable real{};

unsigned long Traded(unsigned long index) { return index < 2 ? 1 - index : index; }
unsigned long __stdcall GetStateTraded(unsigned long index, void* state) {
    return real.getState(Traded(index), state);
}
unsigned long __stdcall SetStateTraded(unsigned long index, void* vibration) {
    return real.setState(Traded(index), vibration);
}
unsigned long __stdcall CapabilitiesTraded(unsigned long index, unsigned long flags, void* capabilities) {
    return real.capabilities(Traded(index), flags, capabilities);
}

bool VerifyTable(std::uintptr_t base) {
    // mov [pointer], eax for each of the three
    const rva::Rva stores[] = {rva::inputsystem::kXInputStoreState, rva::inputsystem::kXInputStoreVibration,
        rva::inputsystem::kXInputStoreCapabilities};
    for (unsigned index = 0; index < 3; ++index) {
        const auto store = reinterpret_cast<const std::uint8_t*>(base + stores[index]);
        std::uint32_t pointer = 0;
        if (!Readable(store, 5) || store[0] != 0xa3) return false;
        std::memcpy(&pointer, store + 1, sizeof(pointer));
        if (pointer != base + rva::inputsystem::kXInputGetStatePointer + index * sizeof(void*)) return false;
    }
    return true;
}

void TradeControllers(std::uintptr_t base) {
    if (table || settings::values.keyboardPlayer != 1) return;
    if (!VerifyTable(base)) {
        bridge::Log("[devices] Keyboard as player one unavailable: unexpected InputSystem code");
        return;
    }
    auto candidate = reinterpret_cast<XInputTable*>(base + rva::inputsystem::kXInputGetStatePointer);
    if (!candidate->getState || !candidate->setState || !candidate->capabilities) {
        bridge::Log("[devices] Keyboard as player one: the game loaded no XInput, nothing to trade");
        return;
    }
    real = *candidate;
    table = candidate;
    *table = {&GetStateTraded, &SetStateTraded, &CapabilitiesTraded};
    bridge::Log("[devices] Controllers 1 and 2 trade places: keyboard and mouse are player one");
}

void RestoreControllers() {
    if (!table) return;
    // Only what is still ours; the game never rewrites these after Init.
    if (table->getState == &GetStateTraded) table->getState = real.getState;
    if (table->setState == &SetStateTraded) table->setState = real.setState;
    if (table->capabilities == &CapabilitiesTraded) table->capabilities = real.capabilities;
    table = nullptr;
    real = {};
    bridge::Log("[devices] Controller order restored");
}

bool VerifyHotPlug(std::uintptr_t base) {
    // mov byte ptr [esi+3678],1 ; cmp byte ptr [esi+3678],0 ; je ; push 1 ; mov ecx,<cvar> ; call SetValue
    static constexpr std::uint8_t found[] = {0xc6,0x86,0x78,0x36,0x00,0x00,0x01,0x80,0xbe,0x78,0x36,0x00,0x00,0x00,0x74,0x0c,0x6a,0x01,0xb9};
    // cmp byte ptr [esi+3678],0
    static constexpr std::uint8_t gate[] = {0x80,0xbe,0x78,0x36,0x00,0x00,0x00};
    const auto init = reinterpret_cast<const std::uint8_t*>(base + rva::inputsystem::kXInputFoundAtInit);
    const auto poll = reinterpret_cast<const std::uint8_t*>(base + rva::inputsystem::kXInputPollGate);
    const auto call = reinterpret_cast<const std::uint8_t*>(base + rva::inputsystem::kXInputGetStateCall);
    if (!Readable(init, sizeof(found) + 9) || std::memcmp(init, found, sizeof(found)) != 0 ||
        !Readable(poll, sizeof(gate)) || std::memcmp(poll, gate, sizeof(gate)) != 0 ||
        !Readable(call, 6) || call[0] != 0xff || call[1] != 0x15) return false;
    std::uint32_t cvar = 0, pointer = 0;
    std::int32_t displacement = 0;
    std::memcpy(&cvar, init + sizeof(found), sizeof(cvar));
    std::memcpy(&displacement, init + sizeof(found) + 5, sizeof(displacement));
    std::memcpy(&pointer, call + 2, sizeof(pointer));
    const std::uintptr_t setter = rva::inputsystem::kXInputFoundAtInit + sizeof(found) + 9 + displacement;
    return init[sizeof(found) + 4] == 0xe8 && cvar == base + rva::inputsystem::kControllerFoundCvar &&
        setter == rva::inputsystem::kConVarSetInt && pointer == base + rva::inputsystem::kXInputGetStatePointer;
}
}

bool Initialize() {
    if (inputSystem) return true;
    // InputSystem() is null unless the module's full file fingerprint matched.
    HMODULE module = bridge::InputSystem();
    if (!module) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    using Factory = void*(__cdecl*)(const char*, int*);
    const auto factory = reinterpret_cast<Factory>(GetProcAddress(module, "CreateInterface"));
    void* object = factory ? factory("InputSystemVersion001", nullptr) : nullptr;
    // Same object/vtable gate as the HUD binding adapters.
    if (!Readable(object, 0x3e34) || *static_cast<std::uintptr_t*>(object) != base + rva::inputsystem::kInputSystemVtable ||
        reinterpret_cast<std::uintptr_t>(bridge::Virtual<void*>(object, 27)) != base + rva::inputsystem::kInputSystemSlot27) {
        bridge::Log("[devices] Controller detection unavailable: unexpected InputSystem object");
        return false;
    }
    inputSystem = object;
    loggedSlot = -1;
    if (VerifyHotPlug(base)) {
        getState = reinterpret_cast<GetStateFn*>(base + rva::inputsystem::kXInputGetStatePointer);
        foundCvar = reinterpret_cast<void*>(base + rva::inputsystem::kControllerFoundCvar);
        setCvarInt = reinterpret_cast<void(__thiscall*)(void*, int)>(base + rva::inputsystem::kConVarSetInt);
    } else {
        bridge::Log("[devices] Controller hot-plug unavailable: unexpected InputSystem code");
    }
    TradeControllers(base);
    return true;
}

void Shutdown() {
    RestoreControllers();
    inputSystem = nullptr;
    loggedSlot = -1;
    getState = nullptr;
    foundCvar = nullptr;
    setCvarInt = nullptr;
    nextProbe = 0;
}

bool WatchForController() {
    if (!inputSystem || !getState) return false;
    auto& polled = static_cast<unsigned char*>(inputSystem)[kXInputPolled];
    if (polled) return false;  // the game polls XInput itself, including re-plugging
    const ULONGLONG now = GetTickCount64();
    if (now < nextProbe) return false;
    nextProbe = now + 1000;  // asking for an absent pad is slow; once a second
    const GetStateFn query = *getState;  // null when no XInput DLL was loaded
    if (!query) return false;
    unsigned char state[16]{};  // XINPUT_STATE
    for (unsigned long pad = 0; pad < 4; ++pad) {
        if (query(pad, state) != 0) continue;
        // Exactly what Init does when a pad is present at launch.
        polled = 1;
        setCvarInt(foundCvar, 1);
        bridge::Log("[devices] Controller %lu switched on after launch; XInput polling started", pad + 1);
        return true;
    }
    return false;
}

bool PadConnected(int slot) {
    // Native XInput records: InputSystem+3bf4 + 90h*slot. Init writes the user
    // ID at +0 and connected=true at +8; Poll clears both when the pad is lost.
    if (!inputSystem || slot < 0 || slot > 3) return false;
    const auto record = static_cast<const unsigned char*>(inputSystem) + 0x3bf4u +
        static_cast<unsigned>(slot) * 0x90u;
    return *reinterpret_cast<const int*>(record) == slot && record[8] != 0;
}

int KeyboardSlot() {
    // Without detection keep the tested arrangement: keyboard is player 2.
    const int slot = !inputSystem || PadConnected(0) ? 1 : 0;
    if (slot != loggedSlot) {
        bridge::Log("[devices] Keyboard/mouse drive player %d (controller 1 %s)", slot + 1,
            !inputSystem ? "state unknown" : PadConnected(0) ? "connected" : "not connected");
        loggedSlot = slot;
    }
    return slot;
}
}
