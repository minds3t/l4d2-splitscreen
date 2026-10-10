#include "presentation.h"
#include "readable.h"
#include "build_10097.h"
#include "bridge.h"
#include "input.h"
#include "lobby.h"
#include "settings.h"

#include <array>
#include <cstring>
#include <intrin.h>

namespace presentation {
namespace {
using ModeFn = bool(__thiscall*)(void*);
ModeFn originalMode = nullptr;
using SetButtonsFn = void(__thiscall*)(void*, int, int, bool, const void*);
SetButtonsFn originalSetButtons = nullptr;
using DrawButtonFn = void(__thiscall*)(void*, int, int, int, const char*, const wchar_t*, int);
DrawButtonFn originalDrawButton = nullptr;
using DrawBackgroundFn = int(__thiscall*)(void*, int, int, int, int, bool);
DrawBackgroundFn originalDrawBackground = nullptr;
using GetBoundsFn = void(__thiscall*)(void*, int*, int*, int*, int*);
GetBoundsFn getBounds = nullptr;
using HybridPaintFn = void(__thiscall*)(void*);
HybridPaintFn originalHybridPaint = nullptr;
using ConfirmationPaintFn = void(__thiscall*)(void*);
ConfirmationPaintFn originalConfirmationPaint = nullptr;
using FindChildFn = void*(__thiscall*)(void*, const char*, bool);
FindChildFn findChild = nullptr;
using SetVisibleFn = void(__thiscall*)(void*, bool);
using GetSchemeFn = unsigned(__thiscall*)(void*);
using ScaleInsetFn = int(__thiscall*)(void*, unsigned, int);
void* inputSystem = nullptr;
std::uintptr_t gameui = 0;
std::array<bool, 4> logged{};
bool layoutLogged = false;
bool anchorLogged = false;
bool backgroundLogged = false;
bool confirmationLogged = false;
bool confirmationPaintLogged = false;
bool confirmationGuardLogged = false;
std::array<bool, 5> insetLogged{};

// InputSystem's menu-mode predicate is also queried elsewhere for Steam
// Input behavior. Only these verified native footer queries are adapted.
// The first three enable existing footer visibility/layout/button drawing.
// The last selects the retained Xbox bitmap glyph path inside DrawButton,
// avoiding a Steam Input glyph request for a controller that it does not own.
struct FooterQuery {
    std::uint32_t returnRva;
    unsigned char callRegister;
    bool result;
};
const std::array<FooterQuery, 4> queries{{
    {rva::gameui::kFooterVisibleQueryReturn, 0xd0, true},
    {rva::gameui::kFooterLayoutQueryReturn, 0xd0, true},
    {rva::gameui::kFooterDrawQueryReturn, 0xd0, true},
    {rva::gameui::kFooterGlyphQueryReturn, 0xd2, false},
}};
const std::array<std::uint32_t, 5> buttonReturns{{
    rva::gameui::kFooterButton0Return, rva::gameui::kFooterButton1Return, rva::gameui::kFooterButton2Return, rva::gameui::kFooterButton3Return, rva::gameui::kFooterButton4Return,
}};
const std::uint32_t confirmationModeReturn = rva::gameui::kConfirmationModeReturn;

using bridge::Readable;

bool StandardConsoleFooter(void* self) {
    if (!gameui || !Readable(self, 0xb48) ||
        *static_cast<std::uintptr_t*>(self) != gameui + rva::gameui::kFooterVtable) return false;
    const int layout = *reinterpret_cast<const int*>(static_cast<const char*>(self) + 0xb44);
    // SetButtons accepts caller-defined tables in PC layouts13--15. Their
    // coordinates, including layout13's enlarged help background, stay native.
    return layout >= 1 && layout <= 12 && lobby::UseConsoleMenuPresentation();
}

bool VerifiedCall(std::uintptr_t base, std::uint32_t returnRva, std::uint32_t targetRva) {
    const auto call = reinterpret_cast<const unsigned char*>(base + returnRva - 5);
    std::int32_t displacement = 0;
    if (!Readable(call, 5) || call[0] != 0xe8) return false;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    return static_cast<std::int64_t>(returnRva) + displacement == targetRva;
}

void __fastcall ConfirmationPaintHook(void* self, void*) {
    const bool readable = Readable(self, 0x304);
    const auto table = readable ? *static_cast<std::uintptr_t*>(self) : 0;
    const bool owned = lobby::UseConsoleMenuPresentation();
    if (!findChild || !readable || table != gameui + rva::gameui::kConfirmationVtable || !owned) {
        if (!confirmationGuardLogged) {
            bridge::Log("[presentation] Confirmation child adaptation skipped: root=%p vtable=%p readable=%d consoleScope=%d",
                self, reinterpret_cast<void*>(table), readable, owned);
            confirmationGuardLogged = true;
        }
        originalConfirmationPaint(self);
        return;
    }
    // UpdateButtons uses cached child pointers. Startup resource loading can
    // leave a desktop row visible even after that updater selected controller
    // mode. Resolve only its four named PC children from the live panel tree
    // at paint time, before VGUI paints children; never reload the dialog or
    // change its message, optional checkbox, footer, focus or action callbacks.
    static constexpr const char* names[] = {"IconForwardArrow", "BtnOK", "IconBackArrow", "BtnCancel"};
    unsigned hidden = 0;
    for (unsigned i = 0; i < 4; ++i) {
        void* child = findChild(self, names[i], false);
        void* cached = *reinterpret_cast<void**>(static_cast<char*>(self) + 0x2d4 + i * 4);
        const auto childTable = Readable(child, sizeof(void*)) ? *static_cast<std::uintptr_t**>(child) : nullptr;
        const bool valid = Readable(childTable, 34 * sizeof(void*)) && childTable[33] == gameui + rva::gameui::kPanelSetVisible;
        if (valid) {
            bridge::Virtual<SetVisibleFn>(child, 33)(child, false);
            hidden |= 1u << i;
        }
        if (!confirmationPaintLogged) {
            bridge::Log("[presentation] Confirmation native child root=%p vtable=%p name=%s cached=%p found=%p SetVisibleVerified=%d",
                self, reinterpret_cast<void*>(table), names[i], cached, child, valid);
        }
    }
    if (!confirmationPaintLogged) {
        bridge::Log("[presentation] Confirmation PC child visibility mask=%x; native A/B footer and callbacks unchanged", hidden);
        confirmationPaintLogged = true;
    }
    originalConfirmationPaint(self);
}

void __fastcall HybridPaintHook(void* self, void*) {
    // Paint's sole native caller is the HybridButton virtual Paint wrapper.
    // Keep derived/foreign controls, compact flyouts and game-mode tiles native.
    if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) != gameui + rva::gameui::kHybridPaintReturn ||
        !Readable(self, 0x234) ||
        *static_cast<std::uintptr_t*>(self) != gameui + rva::gameui::kHybridButtonVtable ||
        !lobby::UseConsoleMenuPresentation()) {
        originalHybridPaint(self);
        return;
    }
    const int style = *reinterpret_cast<const int*>(static_cast<const char*>(self) + 0x230);
    const int logicalInset = style == 1 ? 1 :
        (style == 0 || style == 3 || style == 4 ? 2 : -1);
    if (logicalInset < 0) {
        originalHybridPaint(self);
        return;
    }
    void* manager = *reinterpret_cast<void**>(gameui + rva::gameui::kSchemeManagerGlobal);
    if (!Readable(manager, sizeof(void*)) ||
        !Readable(*static_cast<void***>(manager), 14 * sizeof(void*))) {
        originalHybridPaint(self);
        return;
    }
    // This is the same GetScheme + GetProportionalScaledValueEx sequence used
    // by native ApplySettings at gameui+5cb2f..5cb49. The main UI scheme loads
    // before addon overrides and retains PC inset0. Restore the ISO inset only
    // for this paint; never alter shared scheme values, font profiles or input.
    const unsigned scheme = bridge::Virtual<GetSchemeFn>(self, 20)(self);
    const int scaledInset = bridge::Virtual<ScaleInsetFn>(manager, 13)(manager, scheme, logicalInset);
    int* cachedInset = reinterpret_cast<int*>(static_cast<char*>(self) + 0x200);
    const int previousInset = *cachedInset;
    if (!insetLogged[style]) {
        bridge::Log("[presentation] Native Xbox button inset style=%d logical=%d scaled=%d previous=%d; paint scope only",
            style, logicalInset, scaledInset, previousInset);
        insetLogged[style] = true;
    }
    __try {
        *cachedInset = scaledInset;
        originalHybridPaint(self);
    } __finally {
        *cachedInset = previousInset;
    }
}

void __fastcall DrawButtonHook(void* self, void*, int alignment, int x, int y,
    const char* icon, const wchar_t* label, int button) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (button >= 0 && button < static_cast<int>(buttonReturns.size()) &&
        caller == gameui + buttonReturns[button] && StandardConsoleFooter(self)) {
        // PC PaintBackground passes tableY-14, while the ISO passes tableY+8.
        // These are still logical coordinates: DrawButton scales Y itself.
        // Shift both glyph and text together without changing fonts or labels.
        y += 22;
        if (!anchorLogged) {
            bridge::Log("[presentation] Native Xbox footer anchor restored: tableY+8 before proportional scaling");
            anchorLogged = true;
        }
    }
    originalDrawButton(self, alignment, x, y, icon, label, button);
}

int __fastcall DrawBackgroundHook(void* self, void*, int x, int y, int wide, int tall, bool shortBorders) {
    if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == gameui + rva::gameui::kFooterBackgroundReturn &&
        getBounds && x == 0 && shortBorders && y > 0 && StandardConsoleFooter(self)) {
        int panelX = 0, panelY = 0, panelWide = 0, panelTall = 0;
        getBounds(self, &panelX, &panelY, &panelWide, &panelTall);
        // This draw call already uses pixels. Match the no-help PC branch
        // exactly, then restore the ISO's fractions using the actual height;
        // y+tall can lose a pixel because both PC products truncate separately.
        // A visible help label draws the full panel and is left untouched.
        if (panelTall > 0 && wide == panelWide &&
            y == static_cast<int>(panelTall * 0.1499999761581421f) &&
            tall == static_cast<int>(panelTall * 0.8500000238418579f)) {
            y = static_cast<int>(panelTall * 0.25f);
            tall = static_cast<int>(panelTall * 0.75f);
            if (!backgroundLogged) {
                bridge::Log("[presentation] Native Xbox footer strip restored: no-help y25%% height75%%");
                backgroundLogged = true;
            }
        }
    }
    return originalDrawBackground(self, x, y, wide, tall, shortBorders);
}

bool __fastcall FooterModeHook(void* self, void*) {
    const bool native = originalMode(self);
    if (self != inputSystem || !bridge::IsLocalSplitEnabled()) return native;
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (caller == gameui + confirmationModeReturn && lobby::UseConsoleMenuPresentation()) {
        // GenericConfirmation already maps A directly to OK and B to Cancel.
        // Its retained controller presentation hides the PC selectable rows
        // and supplies the native localized A/B footer. Keep message/check-box
        // children and all action callbacks in the newer PC implementation.
        if (!confirmationLogged) {
            bridge::Log("[presentation] Native confirmation controller prompts enabled; OK/Cancel callbacks unchanged");
            confirmationLogged = true;
        }
        return true;
    }
    for (std::size_t i = 0; i < queries.size(); ++i) {
        if (caller != gameui + queries[i].returnRva) continue;
        if (!lobby::UseConsoleMenuPresentation()) return native;
        if (!logged[i]) {
            bridge::Log("[presentation] Native Xbox footer path gameui+%06x result=%d",
                queries[i].returnRva, queries[i].result);
            logged[i] = true;
        }
        return queries[i].result;
    }
    return native;
}

void __fastcall SetButtonsHook(void* self, void*, int mask, int layout, bool help, const void* custom) {
    // The ISO's offline GameLobby chooses native layout7: B at x100, A at
    // x225. The same table survives on PC, whose lobby instead passes4.
    // Its extra Y position is harmless: preserve the original A/B-only mask.
    // Other menus retain their own layout and every label/action stays native.
    if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == gameui + rva::gameui::kLobbySetButtonsReturn &&
        mask == 3 && layout == 4 && !custom && lobby::UseConsoleMenuPresentation()) {
        layout = 7;
        if (!layoutLogged) {
            bridge::Log("[presentation] Native offline lobby footer layout4 -> Xbox layout7: B Back, A Select");
            layoutLogged = true;
        }
    }
    originalSetButtons(self, mask, layout, help, custom);
}

// Console Audio/Video page (the package shows only its split direction). On
// PC the dropdown is always disabled: of the console's widescreen test only
// the "non-widescreen displays split horizontally" branch was compiled in
// (Activate: SetEnabled(false), select Horizontal). At the main menu, enable
// it and show the current ss_splitmode; the page's own command handler then
// applies a choice exactly as on the console. vgui::Panel::SetEnabled is
// vtable slot 54. In a lobby or game the native restriction stays.
using ActivateFn = void(__thiscall*)(void*);
using SetSelectionFn = void(__thiscall*)(void*, const char*);
using KeyPressedFn = void(__thiscall*)(void*, int);
ActivateFn originalAudioVideoActivate = nullptr;
KeyPressedFn originalAudioVideoKey = nullptr;
SetSelectionFn setSelection = nullptr;

// Leaving the page: after a change the console first writes the player's
// profile and closes from the storage callback. That write is a stub on PC
// (gameui+9fc0 returns false), so B / Escape would do nothing. The choice is
// already in ss_splitmode, an ordinary saved PC cvar: drop the "changed" mark.
void __fastcall AudioVideoKeyHook(void* self, void*, int code) {
    if (bridge::IsLocalSplitEnabled() && Readable(self, 0x2ea))
        static_cast<unsigned char*>(self)[0x2e9] = 0;
    originalAudioVideoKey(self, code);
}

void __fastcall AudioVideoActivateHook(void* self, void*) {
    originalAudioVideoActivate(self);
    void* framework = bridge::Framework();
    if (!bridge::IsLocalSplitEnabled() || !setSelection || !framework ||
        bridge::Virtual<void*(__thiscall*)(void*)>(framework, 9)(framework) || !Readable(self, 0x2d4)) return;
    void* direction = *reinterpret_cast<void**>(static_cast<char*>(self) + 0x2d0);
    float mode = 0;
    if (!Readable(direction, sizeof(void*)) || !Readable(*static_cast<void***>(direction), 181 * sizeof(void*)) ||
        !input::ReadNumber("ss_splitmode", mode)) return;
    bridge::Virtual<void(__thiscall*)(void*, bool)>(direction, 54)(direction, true);
    setSelection(direction, mode == 1 ? "#L4D360UI_SplitScreenDirection_Horizontal"
        : mode == 2 ? "#L4D360UI_SplitScreenDirection_Vertical" : "#L4D360UI_SplitScreenDirection_Default");
    // The page gives its first focus to the brightness slider, which this
    // package keeps inert. vgui::Panel::NavigateTo is vtable slot 180, the
    // call Activate itself makes on that slider (call [vtbl+2d0]).
    bridge::Virtual<void(__thiscall*)(void*)>(direction, 180)(direction);
}

// Optional: without it the split direction page shows a disabled setting.
void InstallSplitDirection(HMODULE ui, std::uintptr_t uiBase) {
    if (originalAudioVideoKey) return;
    static constexpr unsigned char activatePrefix[] = {0x55,0x8b,0xec,0x83,0xec,0x44,0xa1};
    static constexpr unsigned char keyPrefix[] = {0x55,0x8b,0xec,0x83,0xec,0x2c,0x53,0x8b,0x5d,0x08,0x56,0x57,0x53,0x8b,0xf1};
    static constexpr unsigned char selectionPrefix[] = {0x55,0x8b,0xec,0x81,0xec,0x08,0x01,0x00,0x00,0xa1};
    const auto selection = reinterpret_cast<const void*>(uiBase + rva::gameui::kDropDownSetSelection);
    if (Readable(selection, sizeof(selectionPrefix)) &&
        !std::memcmp(selection, selectionPrefix, sizeof(selectionPrefix)) &&
        // The exit first: never enable a setting whose page cannot be left.
        bridge::InstallHook("native split direction page exit", ui, rva::gameui::kAudioVideoKeyPressed,
            keyPrefix, sizeof(keyPrefix), reinterpret_cast<void*>(&AudioVideoKeyHook),
            reinterpret_cast<void**>(&originalAudioVideoKey)) &&
        bridge::InstallHook("native split direction setting", ui, rva::gameui::kAudioVideoActivate,
            activatePrefix, sizeof(activatePrefix), reinterpret_cast<void*>(&AudioVideoActivateHook),
            reinterpret_cast<void**>(&originalAudioVideoActivate))) {
        setSelection = reinterpret_cast<SetSelectionFn>(uiBase + rva::gameui::kDropDownSetSelection);
    } else {
        bridge::Log("[presentation] Split direction setting stays disabled: unexpected GameUI code");
    }
}
}

bool Install() {
    if (originalMode) return originalSetButtons && originalDrawButton && originalDrawBackground &&
        originalHybridPaint && originalConfirmationPaint;
    // InputSystem() is exposed only after its full file fingerprint matches.
    const auto module = bridge::InputSystem();
    const auto ui = bridge::GameUI();
    if (!module || !ui || !bridge::IsLocalSplitEnabled()) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto uiBase = reinterpret_cast<std::uintptr_t>(ui);
    if (!settings::values.consoleUi) {
        InstallSplitDirection(ui, uiBase);
        bridge::Log("[presentation] Console look switched off: PC footer and prompts; split direction page only");
        return true;
    }
    using Factory = void*(__cdecl*)(const char*, int*);
    const auto factory = reinterpret_cast<Factory>(GetProcAddress(module, "CreateInterface"));
    if (!factory) return false;
    void* object = factory("InputSystemVersion001", nullptr);
    if (!Readable(object, sizeof(void*))) return false;
    const auto table = *static_cast<std::uintptr_t**>(object);
    if (reinterpret_cast<std::uintptr_t>(table) != base + rva::inputsystem::kInputSystemVtable ||
        !Readable(table, 40 * sizeof(void*)) || table[39] != base + rva::inputsystem::kMenuModePredicate) return false;
    for (const auto& query : queries) {
        const auto call = reinterpret_cast<const unsigned char*>(uiBase + query.returnRva - 2);
        if (!Readable(call, 4) || call[0] != 0xff || call[1] != query.callRegister ||
            call[2] != 0x84 || call[3] != 0xc0) return false;
    }
    // GenericConfirmation::UpdateButtons uses the same verified predicate,
    // but stores AL in BL after its first child-pointer check rather than
    // immediately testing AL as the four footer callers do.
    static constexpr unsigned char confirmationCall[] = {
        0xff,0xd2,0x83,0xbe,0xd4,0x02,0x00,0x00,0x00,0x8a,0xd8
    };
    const auto confirmation = reinterpret_cast<const void*>(uiBase + confirmationModeReturn - 2);
    if (!Readable(confirmation, sizeof(confirmationCall)) ||
        std::memcmp(confirmation, confirmationCall, sizeof(confirmationCall))) return false;
    const auto confirmationTable = reinterpret_cast<const std::uintptr_t*>(uiBase + rva::gameui::kConfirmationVtable);
    static constexpr unsigned char findChildPrefix[] = {0x55,0x8b,0xec,0x83,0xec,0x0c};
    static constexpr unsigned char visiblePrefix[] = {0x55,0x8b,0xec,0x8b,0x45,0x08};
    const auto childFinder = reinterpret_cast<const void*>(uiBase + rva::gameui::kPanelFindChild);
    const auto visibleSetter = reinterpret_cast<const void*>(uiBase + rva::gameui::kPanelSetVisible);
    if (!Readable(confirmationTable, 128 * sizeof(void*)) ||
        confirmationTable[127] != uiBase + rva::gameui::kConfirmationPaint ||
        !Readable(childFinder, sizeof(findChildPrefix)) ||
        std::memcmp(childFinder, findChildPrefix, sizeof(findChildPrefix)) ||
        !Readable(visibleSetter, sizeof(visiblePrefix)) ||
        std::memcmp(visibleSetter, visiblePrefix, sizeof(visiblePrefix))) return false;
    for (const auto returnRva : buttonReturns) {
        if (!VerifiedCall(uiBase, returnRva, rva::gameui::kFooterDrawButton)) return false;
    }
    if (!VerifiedCall(uiBase, rva::gameui::kFooterBackgroundReturn, rva::gameui::kDrawFooterBackground)) return false;
    const auto hybridTable = reinterpret_cast<const std::uintptr_t*>(uiBase + rva::gameui::kHybridButtonVtable);
    if (!Readable(hybridTable, 129 * sizeof(void*)) ||
        hybridTable[20] != uiBase + rva::gameui::kPanelGetScheme || hybridTable[89] != uiBase + rva::gameui::kHybridApplySettings ||
        hybridTable[128] != uiBase + rva::gameui::kHybridPaintWrapper || !VerifiedCall(uiBase, rva::gameui::kHybridPaintReturn, rva::gameui::kHybridPaint)) return false;
    static constexpr unsigned char boundsPrefix[] = {0x55,0x8b,0xec,0x8b,0x45,0x0c};
    const auto bounds = reinterpret_cast<const void*>(uiBase + rva::gameui::kPanelGetBounds);
    if (!Readable(bounds, sizeof(boundsPrefix)) || std::memcmp(bounds, boundsPrefix, sizeof(boundsPrefix))) return false;
    static constexpr unsigned char layoutCaller[] = {0xe8,0x18,0x7f,0xff,0xff};
    static constexpr int xboxABPositions[] = {0,225,50,0,100,50};
    const auto call = reinterpret_cast<const void*>(uiBase + rva::gameui::kLobbySetButtonsCall);
    const auto nativeLayout = reinterpret_cast<const void*>(uiBase + rva::gameui::kFooterLayoutTable + 7 * 64);
    if (!Readable(call, sizeof(layoutCaller)) || std::memcmp(call, layoutCaller, sizeof(layoutCaller)) ||
        !Readable(nativeLayout, sizeof(xboxABPositions)) ||
        std::memcmp(nativeLayout, xboxABPositions, sizeof(xboxABPositions))) return false;
    // xor eax,eax; cmp dword ptr [ecx+0x988],2. Complete instructions are
    // supplied to the shared hook installer; the original field is never set.
    static constexpr unsigned char prefix[] = {0x33,0xc0,0x83,0xb9,0x88,0x09,0x00,0x00,0x02};
    if (!bridge::InstallHook("native local-menu Xbox footer", module, rva::inputsystem::kMenuModePredicate,
        prefix, sizeof(prefix), reinterpret_cast<void*>(&FooterModeHook),
        reinterpret_cast<void**>(&originalMode))) return false;
    inputSystem = object;
    gameui = uiBase;
    getBounds = reinterpret_cast<GetBoundsFn>(uiBase + rva::gameui::kPanelGetBounds);
    static constexpr unsigned char buttonsPrefix[] = {0x55,0x8b,0xec,0x53,0x56,0x6a,0x00};
    if (!bridge::InstallHook("native offline lobby footer order", ui, rva::gameui::kFooterSetButtons,
        buttonsPrefix, sizeof(buttonsPrefix), reinterpret_cast<void*>(&SetButtonsHook),
        reinterpret_cast<void**>(&originalSetButtons))) return false;
    static constexpr unsigned char drawPrefix[] = {0x55,0x8b,0xec,0x81,0xec,0xa0,0x00,0x00,0x00};
    if (!bridge::InstallHook("native Xbox footer prompt anchor", ui, rva::gameui::kFooterDrawButton,
        drawPrefix, sizeof(drawPrefix), reinterpret_cast<void*>(&DrawButtonHook),
        reinterpret_cast<void**>(&originalDrawButton))) return false;
    static constexpr unsigned char backgroundPrefix[] = {0x55,0x8b,0xec,0x83,0xec,0x0c};
    if (!bridge::InstallHook("native Xbox footer strip proportions", ui, rva::gameui::kDrawFooterBackground,
        backgroundPrefix, sizeof(backgroundPrefix), reinterpret_cast<void*>(&DrawBackgroundHook),
        reinterpret_cast<void**>(&originalDrawBackground))) return false;
    static constexpr unsigned char hybridPrefix[] = {0x55,0x8b,0xec,0x81,0xec,0x64,0x08,0x00,0x00};
    if (!bridge::InstallHook("native local-menu Xbox button insets", ui, rva::gameui::kHybridPaint,
        hybridPrefix, sizeof(hybridPrefix), reinterpret_cast<void*>(&HybridPaintHook),
        reinterpret_cast<void**>(&originalHybridPaint))) return false;
    static constexpr unsigned char confirmationPaintPrefix[] = {0x8b,0x81,0xc8,0x02,0x00,0x00};
    findChild = reinterpret_cast<FindChildFn>(uiBase + rva::gameui::kPanelFindChild);
    if (!bridge::InstallHook("native confirmation live child visibility", ui, rva::gameui::kConfirmationPaint,
        confirmationPaintPrefix, sizeof(confirmationPaintPrefix),
        reinterpret_cast<void*>(&ConfirmationPaintHook),
        reinterpret_cast<void**>(&originalConfirmationPaint))) return false;
    logged.fill(false);
    layoutLogged = false;
    anchorLogged = false;
    backgroundLogged = false;
    confirmationLogged = false;
    confirmationPaintLogged = false;
    confirmationGuardLogged = false;
    insetLogged.fill(false);
    InstallSplitDirection(ui, uiBase);
    bridge::Log("[presentation] Retained Xbox footer enabled for local menus; native labels/actions unchanged");
    return true;
}

void Shutdown() {
    originalAudioVideoKey = nullptr;
    originalAudioVideoActivate = nullptr;
    setSelection = nullptr;
    originalMode = nullptr;
    originalSetButtons = nullptr;
    originalDrawButton = nullptr;
    originalDrawBackground = nullptr;
    originalHybridPaint = nullptr;
    originalConfirmationPaint = nullptr;
    findChild = nullptr;
    getBounds = nullptr;
    inputSystem = nullptr;
    gameui = 0;
    logged.fill(false);
    layoutLogged = false;
    anchorLogged = false;
    backgroundLogged = false;
    confirmationLogged = false;
    confirmationPaintLogged = false;
    confirmationGuardLogged = false;
    insetLogged.fill(false);
}
}
