#include "hud_inventory.h"
#include "readable.h"
#include "build_10097.h"
#include "bridge.h"
#include "lobby.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <intrin.h>

namespace hud_inventory {
namespace {
using PaintFn = void(__thiscall*)(void*);
using BoxFn = void(__thiscall*)(void*, int, int, int, int);
using IconFn = void(__thiscall*)(void*, int, int, int, int, const void*);
using GlyphFn = void(__thiscall*)(void*, int, int, int, int, int);
using DimensionFn = int(__thiscall*)(void*);
using ScreenSizeFn = void(__cdecl*)(int*, int*);
using LocalPlayerFn = void*(__cdecl*)(int);
PaintFn originalPaint = nullptr;
BoxFn originalInactive = nullptr, originalActive = nullptr;
IconFn originalIcon = nullptr;
GlyphFn originalGlyph = nullptr;
DimensionFn getWide = nullptr, getTall = nullptr;
ScreenSizeFn getScreenSize = nullptr;
LocalPlayerFn getLocalPlayer = nullptr;
using KeySlotFn = int(__cdecl*)(int);
using BindingListenerFn = void(__thiscall*)(void*, int, int, const char*, const char*);
KeySlotFn keySlot = nullptr;
BindingListenerFn bindingListener = nullptr;
void* matchBinding = nullptr;
void* engineClient = nullptr;
void* inputSystem = nullptr;
std::uintptr_t client = 0;
bool installed = false;
std::array<bool, 2> logged{};
struct Binding {
    void* root{};
    ULONGLONG refreshed{};
    int device{-1}, code{-1};
    char name[64]{};
    wchar_t text[64]{};
};
std::array<Binding, 2> bindings{};

struct Frame {
    void* panel;
    void* root;
    bool attempted, ready;
    int x, y, size, gap, wide, tall;
    float iconSize;
};
// A native Paint establishes the correct split-screen player context itself.
// Added draws happen inside its item calls, before that context is restored.
__declspec(thread) Frame* frame = nullptr;

using bridge::Readable;

template<class T> T Field(void* object, std::size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const char*>(object) + offset);
}

bool VerifiedCall(std::uint32_t returnRva, std::uint32_t targetRva) {
    const auto bytes = reinterpret_cast<const unsigned char*>(client + returnRva - 5);
    std::int32_t displacement = 0;
    if (!Readable(bytes, 5) || bytes[0] != 0xe8) return false;
    std::memcpy(&displacement, bytes + 1, sizeof(displacement));
    return static_cast<std::int64_t>(returnRva) + displacement == targetRva;
}

// Exact direct call returns in the original CHudWeaponSelection::Paint.
// Everything before these item calls (primary/secondary weapons and ammo)
// forwards unchanged, even while the scoped Paint context is active.
struct ItemCalls { int slot; std::uint32_t inactive, active, icon, glyph; };
const ItemCalls calls[] = {
    {5, rva::client::kFlashlightInactiveReturn, 0,        rva::client::kFlashlightIconReturn, rva::client::kFlashlightGlyphReturn}, // flashlight (PC Steam Input)
    {2, rva::client::kThrowableInactiveReturn, rva::client::kThrowableActiveReturn, rva::client::kThrowableIconReturn, rva::client::kThrowableGlyphReturn}, // throwable
    {3, rva::client::kFirstAidInactiveReturn, rva::client::kFirstAidActiveReturn, rva::client::kFirstAidIconReturn, rva::client::kFirstAidGlyphReturn}, // first aid
    {4, rva::client::kPillsInactiveReturn, rva::client::kPillsActiveReturn, rva::client::kPillsIconReturn, rva::client::kPillsGlyphReturn}, // pills/adrenaline
};

int ItemFor(std::uintptr_t caller, int kind) {
    if (!frame || caller < client) return -1;
    for (const auto& call : calls) {
        const auto rva = kind == 0 ? call.inactive : kind == 1 ? call.active :
            kind == 2 ? call.icon : call.glyph;
        if (rva && caller == client + rva) return call.slot;
    }
    return -1;
}

void Position(int slot, int& x, int& y) {
    x = frame->x;
    y = frame->y;
    // PPC uses the scaled float IconSize and truncates after the arithmetic.
    if (slot == 2) x = static_cast<int>(x - frame->iconSize - frame->gap);
    if (slot == 3) x = static_cast<int>(x + frame->iconSize + frame->gap);
    if (slot == 4) y = static_cast<int>(y + frame->iconSize + frame->gap);
    if (slot == 5) y = static_cast<int>(y - frame->iconSize - frame->gap);
}

// Engine's internal command matcher takes its button code in EDI and the
// command on the stack (1a27d0). Reuse its exact semicolon/+command matching
// and native ButtonCodeToString rather than copying the binding parser.
__declspec(noinline) const char* MatchLastInv(int code) {
    const char* result = nullptr;
    const char* command = "lastinv";
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

bool CopyName(char (&out)[64], const char* name) {
    if (!Readable(name, sizeof(out))) return false;
    std::size_t length = 0;
    while (length < sizeof(out) && name[length]) ++length;
    if (!length || length == sizeof(out)) return false;
    std::memcpy(out, name, length + 1);
    return true;
}

int KeyboardOwner() {
    // Read only the exact registered in_forceuser ConVar's parent. Never
    // change its cheat-protected value or infer routing from a menu option.
    void* variable = reinterpret_cast<void*>(client + rva::client::kInForceUserCvar);
    if (!Readable(variable, 0x34)) return -1;
    void* parent = Field<void*>(variable, 0x1c);
    if (!Readable(parent, 0x34)) return -1;
    const char* name = Field<const char*>(parent, 0xc);
    if (!Readable(name, 13) || std::memcmp(name, "in_forceuser", 13)) return -1;
    return Field<int>(parent, 0x30);
}

bool PadConnected(int slot) {
    // Native XInput records: InputSystem+3bf4 + 90h*slot. Init5310 writes
    // user ID at+0 and connected=true at+8; Poll6250 clears both on loss.
    const auto offset = 0x3bf4u + static_cast<unsigned>(slot) * 0x90u;
    if (!Readable(inputSystem, offset + 9)) return false;
    return Field<int>(inputSystem, offset) == slot && Field<unsigned char>(inputSystem, offset + 8) != 0;
}

void LocalizePad(Binding& binding, int slot) {
    auto* nativeText = reinterpret_cast<wchar_t*>(static_cast<char*>(frame->panel) + 0x32c);
    wchar_t saved[64];
    std::memcpy(saved, nativeText, sizeof(saved));
    // Feed the actual bound button through the retained native listener. Its
    // glyph result is private to this paint adapter; preserve the panel's
    // existing display buffer even if native localization raises an exception.
    __try {
        bindingListener(static_cast<char*>(frame->root) + 0x194,
            slot, binding.code, binding.name, "lastinv");
        std::memcpy(binding.text, nativeText, sizeof(binding.text));
        binding.text[63] = 0;
    } __finally {
        std::memcpy(nativeText, saved, sizeof(saved));
    }
}

Binding* CurrentBinding() {
    const int slot = Field<int>(frame->root, 0x14);
    if (slot < 0 || slot >= static_cast<int>(bindings.size())) return nullptr;
    const int device = PadConnected(slot) ? 1 : (KeyboardOwner() == slot ? 0 : -1);
    Binding& binding = bindings[slot];
    const auto now = GetTickCount64();
    if (binding.root == frame->root && binding.device == device && now - binding.refreshed < 500)
        return &binding;
    Binding next{};
    next.root = frame->root;
    next.refreshed = now;
    next.device = device;
    if (device == 1) {
        // PC's extended lookup ignores its user argument. Enumerate native
        // joystick codes and use the engine's own code->controller function
        // before its command matcher, so pad1 cannot inherit pad0's binding.
        for (int code = 0x72; code <= 0x131; ++code) {
            if (keySlot(code) != slot) continue;
            if (CopyName(next.name, MatchLastInv(code))) {
                next.code = code;
                LocalizePad(next, slot);
                break;
            }
        }
    } else if (device == 0) {
        using LookupFn = const char*(__thiscall*)(void*, const char*, int, int, int);
        const char* name = bridge::Virtual<LookupFn>(engineClient, 155)(engineClient, "lastinv", slot, 0, 0);
        if (CopyName(next.name, name)) {
            // GameUIButtons contains Xbox symbols, not keyboard letters, and
            // stock localization has no GameUI_Icons_Q token. Use the actual
            // engine key name with the panel's existing readable HudAmmo font.
            for (std::size_t n = 0; n < 63 && next.name[n]; ++n)
                next.text[n] = static_cast<wchar_t>(std::toupper(static_cast<unsigned char>(next.name[n])));
        }
    }
    if (binding.root != next.root || binding.device != next.device ||
        binding.code != next.code || std::strcmp(binding.name, next.name)) {
        bridge::Log("[hud_inventory] Binding hint slot=%d device=%s code=%d binding=%s glyph=%04x; controls/buffer unchanged",
            slot, device == 1 ? "controller" : device == 0 ? "keyboard" : "none",
            next.code, next.name[0] ? next.name : "<unbound>", static_cast<unsigned>(next.text[0]));
    }
    binding = next;
    return &binding;
}

void DrawBinding() {
    Binding* binding = CurrentBinding();
    if (!binding) return;
    const wchar_t* text = binding->text;
    unsigned length = 0;
    while (length < 64 && text[length]) ++length;
    // TextFont (+15c, HudSelectionText11) is a tiny selection-list label.
    // Reuse the already rendered PrimaryAmmoFont (+438, HudAmmo18) for keys.
    const auto font = Field<unsigned>(frame->panel, binding->device == 1 ? 0x4f0 : 0x438);
    const float bindingY = Field<float>(frame->panel, 0x4c8);
    if (!length || length == 64 || !font || !std::isfinite(bindingY) || bindingY <= 0) return;
    void* surface = *reinterpret_cast<void**>(client + rva::client::kSurfaceGlobal);
    if (!Readable(surface, sizeof(void*)) || !Readable(*static_cast<void***>(surface), 0x1f4)) return;
    using FontFn = void(__thiscall*)(void*, unsigned);
    using ColorFn = void(__thiscall*)(void*, unsigned);
    using TextSizeFn = void(__thiscall*)(void*, unsigned, const wchar_t*, int*, int*);
    using TextPosFn = void(__thiscall*)(void*, int, int);
    using TextFn = void(__thiscall*)(void*, const wchar_t*, int);
    int wide = 0, tall = 0;
    bridge::Virtual<FontFn>(surface, 0x44 / 4)(surface, font);
    bridge::Virtual<ColorFn>(surface, 0x48 / 4)(surface, 0xffffffff);
    bridge::Virtual<TextSizeFn>(surface, 0x120 / 4)(surface, font, text, &wide, &tall);
    bridge::Virtual<TextPosFn>(surface, 0x50 / 4)(surface,
        static_cast<int>(frame->wide - Field<float>(frame->panel, 0x4e0)) - wide / 2,
        static_cast<int>(bindingY) - tall / 2);
    bridge::Virtual<TextFn>(surface, 0x1f0 / 4)(surface, text, 0);
}

bool BeginItems(void* root, int wide, int tall) {
    if (!frame || frame->root != root) return false;
    if (frame->attempted) return frame->ready && wide == frame->size && tall == frame->size;
    frame->attempted = true;
    frame->iconSize = Field<float>(frame->panel, 0x4e8);
    const float indent = Field<float>(frame->panel, 0x4e0);
    if (!std::isfinite(frame->iconSize) || frame->iconSize < 4 || frame->iconSize > 512 ||
        !std::isfinite(indent) || indent < 0 || indent > 1024) return false;
    frame->size = static_cast<int>(frame->iconSize);
    if (wide != frame->size || tall != frame->size) return false;
    void* player = getLocalPlayer(-1);
    void* dpad = Field<void*>(frame->panel, 0x514);
    const bool flashlightOn = Readable(player, 0xe4) && (Field<unsigned>(player, 0xe0) & 4);
    void* flashlight = Field<void*>(frame->panel, flashlightOn ? 0x51c : 0x518);
    if (!Readable(player, 0xe4) || !Readable(dpad, 0x9c) || !Readable(flashlight, 0x9c)) return false;
    frame->wide = getWide(frame->panel);
    frame->tall = getTall(frame->panel);
    int screenWide = 0, screenTall = 0;
    getScreenSize(&screenWide, &screenTall);
    if (frame->wide <= 0 || frame->wide > 16384 || frame->tall <= 0 || frame->tall > 16384 ||
        screenTall <= 0 || screenTall > 16384) return false;
    // Exact ISO Paint 8852b640--8852b860: the constant is 2.0, and the
    // one-unit gap is ScreenHeight/480 (not the PC vertical stack's width/320).
    frame->x = static_cast<int>(frame->wide - indent - 2.0f * frame->iconSize);
    frame->y = static_cast<int>(frame->tall - 2.0f * frame->iconSize);
    frame->gap = static_cast<int>(screenTall / 480.0);
    frame->ready = true;
    const unsigned white = 0xffffffff;
    DrawBinding();
    originalIcon(dpad, frame->x, frame->y, frame->size, frame->size, &white);
    int x = 0, y = 0;
    Position(5, x, y);
    originalInactive(root, x, y, frame->size, frame->size);
    originalIcon(flashlight, x, y, frame->size, frame->size, &white);
    const int slot = Field<int>(root, 0x14);
    if (slot >= 0 && slot < static_cast<int>(logged.size()) && !logged[slot]) {
        bridge::Log("[hud_inventory] Xbox cross slot=%d panel=%dx%d icon=%.3f anchor=%d,%d gap=%d; native item/ammo state preserved",
            slot, frame->wide, frame->tall, frame->iconSize, frame->x, frame->y, frame->gap);
        logged[slot] = true;
    }
    return true;
}

void __fastcall PaintHook(void* self, void*) {
    Frame current{};
    current.panel = self;
    current.root = static_cast<char*>(self) - 0x48;
    const bool eligible = installed && bridge::IsLocalSplitEnabled() && lobby::HasOwnedOfflineSession() &&
        Readable(self, 0x538) && Readable(current.root, 0x48) &&
        Field<std::uintptr_t>(self, 0) == client + rva::client::kWeaponSelectionVtable &&
        Field<std::uintptr_t>(current.root, 0) == client + rva::client::kWeaponSelectionRootVtable;
    Frame* previous = frame;
    __try {
        // A nested ineligible Paint must not inherit an outer player's frame.
        frame = eligible ? &current : nullptr;
        originalPaint(self);
    } __finally {
        frame = previous;
    }
}

void __fastcall InactiveHook(void* self, void*, int x, int y, int wide, int tall) {
    const int slot = ItemFor(reinterpret_cast<std::uintptr_t>(_ReturnAddress()), 0);
    if (slot >= 0 && BeginItems(self, wide, tall)) {
        if (slot == 5) return; // the retained flashlight was already drawn above
        Position(slot, x, y);
    }
    originalInactive(self, x, y, wide, tall);
}

void __fastcall ActiveHook(void* self, void*, int x, int y, int wide, int tall) {
    const int slot = ItemFor(reinterpret_cast<std::uintptr_t>(_ReturnAddress()), 1);
    if (slot >= 0 && BeginItems(self, wide, tall)) Position(slot, x, y);
    originalActive(self, x, y, wide, tall);
}

void __fastcall IconHook(void* self, void*, int x, int y, int wide, int tall, const void* color) {
    const int slot = ItemFor(reinterpret_cast<std::uintptr_t>(_ReturnAddress()), 2);
    if (slot >= 0 && frame->ready && wide == frame->size && tall == frame->size) {
        if (slot == 5) return;
        Position(slot, x, y);
    }
    originalIcon(self, x, y, wide, tall, color);
}

void __fastcall GlyphHook(void* self, void*, int slot, int x, int y, int wide, int tall) {
    const int expected = ItemFor(reinterpret_cast<std::uintptr_t>(_ReturnAddress()), 3);
    // Replace only PC item-specific Steam Input hints inside this Xbox cross.
    // No input predicate, native binding, or primary/secondary hint changes.
    if (expected >= 0 && expected == slot && frame && frame->ready && self == frame->root &&
        wide == frame->size && tall == frame->size) return;
    originalGlyph(self, slot, x, y, wide, tall);
}
}

bool Install() {
    HMODULE module = bridge::Client();
    if (!module || !bridge::IsLocalSplitEnabled() || originalPaint) return false;
    client = reinterpret_cast<std::uintptr_t>(module);
    // Binding queries use only these fingerprinted native interfaces and
    // helpers. In particular, the public PC lookup's user argument is not
    // assumed to filter pad ownership: the exact native code classifier does.
    const auto engine = reinterpret_cast<std::uintptr_t>(bridge::Engine());
    const auto input = reinterpret_cast<std::uintptr_t>(bridge::InputSystem());
    if (!engine || !input) return false;
    using Factory = void*(__cdecl*)(const char*, int*);
    auto engineFactory = reinterpret_cast<Factory>(GetProcAddress(reinterpret_cast<HMODULE>(engine), "CreateInterface"));
    auto inputFactory = reinterpret_cast<Factory>(GetProcAddress(reinterpret_cast<HMODULE>(input), "CreateInterface"));
    engineClient = engineFactory ? engineFactory("VEngineClient013", nullptr) : nullptr;
    inputSystem = inputFactory ? inputFactory("InputSystemVersion001", nullptr) : nullptr;
    constexpr unsigned char matcherBytes[] = {0x55,0x8b,0xec,0x81,0xec,0x04,0x01,0x00,0x00};
    constexpr unsigned char keySlotBytes[] = {0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xf8,0x72};
    constexpr unsigned char listenerBytes[] = {0x55,0x8b,0xec,0x81,0xec,0x04,0x01,0x00,0x00};
    if (engineClient != reinterpret_cast<void*>(engine + rva::engine::kEngineClient) ||
        !Readable(engineClient, 4) || Field<std::uintptr_t>(engineClient, 0) != engine + rva::engine::kEngineClientVtable ||
        reinterpret_cast<std::uintptr_t>(bridge::Virtual<void*>(engineClient, 155)) != engine + rva::engine::kKeyLookupBindingEx ||
        !Readable(inputSystem, 0x3e34) || Field<std::uintptr_t>(inputSystem, 0) != input + rva::inputsystem::kInputSystemVtable ||
        reinterpret_cast<std::uintptr_t>(bridge::Virtual<void*>(inputSystem, 27)) != input + rva::inputsystem::kInputSystemSlot27 ||
        std::memcmp(reinterpret_cast<void*>(engine + rva::engine::kMatchBinding), matcherBytes, sizeof(matcherBytes)) ||
        std::memcmp(reinterpret_cast<void*>(engine + rva::engine::kKeySlot), keySlotBytes, sizeof(keySlotBytes)) ||
        std::memcmp(reinterpret_cast<void*>(client + rva::client::kBindingListener), listenerBytes, sizeof(listenerBytes))) {
        bridge::Log("[hud_inventory] Refusing unexpected native binding interfaces/helpers");
        return false;
    }
    matchBinding = reinterpret_cast<void*>(engine + rva::engine::kMatchBinding);
    keySlot = reinterpret_cast<KeySlotFn>(engine + rva::engine::kKeySlot);
    bindingListener = reinterpret_cast<BindingListenerFn>(client + rva::client::kBindingListener);
    for (const auto& call : calls) {
        if (!VerifiedCall(call.inactive, rva::client::kDrawInactiveBox) ||
            (call.active && !VerifiedCall(call.active, rva::client::kDrawActiveBox)) ||
            !VerifiedCall(call.icon, rva::client::kHudTextureDraw) || !VerifiedCall(call.glyph, rva::client::kDrawItemGlyph)) {
            bridge::Log("[hud_inventory] Refusing unexpected native item callsites");
            return false;
        }
    }
    getWide = reinterpret_cast<DimensionFn>(client + rva::client::kPanelGetWide);
    getTall = reinterpret_cast<DimensionFn>(client + rva::client::kPanelGetTall);
    getScreenSize = reinterpret_cast<ScreenSizeFn>(client + rva::client::kGetScreenSize);
    getLocalPlayer = reinterpret_cast<LocalPlayerFn>(client + rva::client::kGetLocalPlayer);
    constexpr unsigned char paintBytes[] = {0x55,0x8b,0xec,0x81,0xec,0x90,0,0,0};
    constexpr unsigned char boxBytes[] = {0x55,0x8b,0xec,0x83,0xec,0x0c};
    constexpr unsigned char iconBytes[] = {0x55,0x8b,0xec,0x56,0x8b,0xf1};
    constexpr unsigned char glyphBytes[] = {0x55,0x8b,0xec,0x51,0x56,0x8b,0xf1};
    if (!bridge::InstallHook("Xbox inventory Paint context", module, rva::client::kWeaponSelectionPaint, paintBytes, sizeof(paintBytes),
            reinterpret_cast<void*>(&PaintHook), reinterpret_cast<void**>(&originalPaint)) ||
        !bridge::InstallHook("Xbox inventory inactive positions", module, rva::client::kDrawInactiveBox, boxBytes, sizeof(boxBytes),
            reinterpret_cast<void*>(&InactiveHook), reinterpret_cast<void**>(&originalInactive)) ||
        !bridge::InstallHook("Xbox inventory active positions", module, rva::client::kDrawActiveBox, boxBytes, sizeof(boxBytes),
            reinterpret_cast<void*>(&ActiveHook), reinterpret_cast<void**>(&originalActive)) ||
        !bridge::InstallHook("Xbox inventory icon positions", module, rva::client::kHudTextureDraw, iconBytes, sizeof(iconBytes),
            reinterpret_cast<void*>(&IconHook), reinterpret_cast<void**>(&originalIcon)) ||
        !bridge::InstallHook("Xbox inventory item hints", module, rva::client::kDrawItemGlyph, glyphBytes, sizeof(glyphBytes),
            reinterpret_cast<void*>(&GlyphHook), reinterpret_cast<void**>(&originalGlyph))) return false;
    installed = true;
    bridge::Log("[hud_inventory] Retained Valve D-pad/flashlight and ISO item layout installed; owned offline Paint only");
    return true;
}

void Shutdown() {
    installed = false;
    frame = nullptr;
    originalPaint = nullptr;
    originalInactive = originalActive = nullptr;
    originalIcon = nullptr;
    originalGlyph = nullptr;
    getWide = getTall = nullptr;
    getScreenSize = nullptr;
    getLocalPlayer = nullptr;
    keySlot = nullptr;
    bindingListener = nullptr;
    matchBinding = engineClient = inputSystem = nullptr;
    client = 0;
    logged = {};
    bindings = {};
}
}
