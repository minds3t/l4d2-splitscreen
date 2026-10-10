#include "input.h"
#include "readable.h"
#include "build_10097.h"
#include "bridge.h"
#include "settings.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <intrin.h>

namespace input {
namespace {
// Verified against native GameUI's CGameUIConVarRef::Init at +0x87ba0:
// ICvar slot13 returns ConVar*, secondary IConVar is at +0x18. Native menu
// writes use IConVar slot1 (int); slot2 is float. ConVar parent is at +0x1c,
// and its numeric float/int fields are +0x2c/+0x30. No console visibility or
// client.dll static address is required for joy_xcontroller_cfg_loaded.
struct Setting {
    const char* name;
    float previous;
};
Setting guard{"joy_xcontroller_cfg_loaded", 0};
Setting joystick{"joystick", 0};
// InputSystem sets this at launch only, and only if a pad is already on. The
// client's joystick setup (client+12fdb0, also behind joyadvancedupdate) reads
// it: found, joystick on, guard clear -> exec 360controller.cfg; guard set
// without (found and joystick on) -> exec undo360controller.cfg and clear the
// guard. Both start with unbindall. All three set is the only state that runs
// neither, so local co-op holds all three, pad or no pad.
Setting found{"joy_xcontroller_found", 0};
// The main menu (gameui+67db0) does the same once per launch while no joystick
// is connected: guard set -> exec undo360controller.cfg, clear the guard, then
// set this flag. Set beforehand, that step is skipped.
Setting popupShown{"controller_popup_shown", 0};
// Keyboard and mouse follow this cvar; controller N always drives player N.
// Cheat-protected for the console only.
Setting forceUser{"in_forceuser", 0};
Setting lowend{"joy_lowend", 0};
Setting accelScale{"joy_accelscale", 0};
Setting accelMax{"joy_accelmax", 0};
Setting pitch1{"joy_pitchsensitivity", 0};
Setting yaw1{"joy_yawsensitivity", 0};
Setting pitch2{"joy_pitchsensitivity2", 0};
Setting yaw2{"joy_yawsensitivity2", 0};
Setting pitchThreshold{"joy_pitchthreshold", 0};
// Valve's gamepad axis mapping from cfg/360controller.cfg. That file cannot be
// executed (it starts with unbindall), and the PC defaults are all zero.
struct Axis { Setting setting; float value; };
Axis axes[] = {
    {{"joy_advanced", 0}, 1}, {{"joy_advaxisx", 0}, 3}, {{"joy_advaxisy", 0}, 1},
    {{"joy_advaxisz", 0}, 0}, {{"joy_advaxisr", 0}, 2}, {{"joy_advaxisu", 0}, 4},
    {{"joy_advaxisv", 0}, 0}};
// Not an input setting, and never written here while playing: the client
// (CViewportClientSystem) copies the split layout's weapon-model field of view
// from splitscreen_config.txt into this archived variable and leaves it there.
// Snapshot, config protection and Restore keep the single-player value.
// Only this one needs it: the same code (client+1b3710) rewrites cl_fov and
// the four infected arm variables on every layout change, back to their
// defaults for one view, and skips this one unless two players are present.
Setting viewmodelFov{"cl_viewmodelfovsurvivor", 0};
Setting* const archivedSettings[] = {&joystick, &lowend, &accelScale, &accelMax,
    &pitch1, &yaw1, &pitch2, &yaw2, &pitchThreshold,
    &axes[0].setting, &axes[1].setting, &axes[2].setting, &axes[3].setting,
    &axes[4].setting, &axes[5].setting, &axes[6].setting, &viewmodelFov};
// Everything Enable snapshots and Restore puts back.
Setting* const allSettings[] = {&guard, &found, &popupShown, &joystick, &forceUser, &lowend, &accelScale, &accelMax,
    &pitch1, &yaw1, &pitch2, &yaw2, &pitchThreshold,
    &axes[0].setting, &axes[1].setting, &axes[2].setting, &axes[3].setting,
    &axes[4].setting, &axes[5].setting, &axes[6].setting, &viewmodelFov};
constexpr unsigned archivedCount = sizeof(archivedSettings) / sizeof(archivedSettings[0]);
void* registry = nullptr;
bool active = false;
bool restorePending = false;
using WriteVariablesFn = void(__thiscall*)(void*, void*, int, bool);
WriteVariablesFn originalWriteVariables = nullptr;
std::uintptr_t writeConfigReturn = 0;
bool serializing = false;
// Tick count at which the per-player controller setup runs; 0 = none pending.
ULONGLONG setupAt = 0;
// Player two had a view on the last frame.
bool splitView = false;

using bridge::Readable;

// Resolve afresh for every operation, including shutdown: client cvars may be
// unregistered before a late plugin unload. Never keep a stale ConVar pointer.
void* Resolve(const char* name) {
    if (!Readable(registry, 4)) return nullptr;
    auto table = *reinterpret_cast<void***>(registry);
    if (!Readable(table, 14 * sizeof(void*))) return nullptr;
    void* value = bridge::Virtual<void*(__thiscall*)(void*, const char*)>(registry, 13)(registry, name);
    if (!Readable(value, 0x34)) return nullptr;
    auto bytes = static_cast<unsigned char*>(value);
    void* parent = *reinterpret_cast<void**>(bytes + 0x1c);
    if (!Readable(parent, 0x34)) return nullptr;
    const auto actualName = *reinterpret_cast<const char* const*>(
        static_cast<const unsigned char*>(parent) + 0xc);
    const auto length = std::strlen(name) + 1;
    if (!Readable(actualName, length) || std::memcmp(actualName, name, length)) return nullptr;
    auto secondary = *reinterpret_cast<void***>(bytes + 0x18);
    return Readable(secondary, 4 * sizeof(void*)) ? value : nullptr;
}

bool Read(const Setting& setting, float& result) {
    __try {
        auto value = static_cast<unsigned char*>(Resolve(setting.name));
        if (!value) return false;
        auto parent = *reinterpret_cast<unsigned char**>(value + 0x1c);
        result = *reinterpret_cast<float*>(parent + 0x2c);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        bridge::Log("[input] Native lookup failed for %s (%08lx)", setting.name, GetExceptionCode());
        return false;
    }
}

bool Set(const Setting& setting, float value) {
    __try {
        auto variable = static_cast<unsigned char*>(Resolve(setting.name));
        if (!variable) return false;
        void* secondary = variable + 0x18;
        bridge::Virtual<void(__thiscall*)(void*, float)>(secondary, 2)(secondary, value);
        float actual = 0;
        return Read(setting, actual) && actual == value;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        bridge::Log("[input] Native setter failed for %s (%08lx)", setting.name, GetExceptionCode());
        return false;
    }
}

// ConVar string storage is at +0x24 of the parent (CGameUIConVarRef reads it).
const char* ReadString(const char* name) {
    __try {
        auto value = static_cast<unsigned char*>(Resolve(name));
        if (!value) return nullptr;
        auto parent = *reinterpret_cast<unsigned char**>(value + 0x1c);
        const char* text = *reinterpret_cast<const char**>(parent + 0x24);
        return Readable(text, 1) ? text : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// The engine's default name for the second local player is "unnamed".
void NameSecondPlayer() {
    const char* second = ReadString("name2");
    const char* first = ReadString("name");
    if (!second || !first || std::strcmp(second, "unnamed") != 0 || std::strchr(first, '"')) return;
    char command[160];
    _snprintf_s(command, sizeof(command), _TRUNCATE, "name2 \"%.100s (2)\"\n", first);
    bridge::PrimaryCommand(command);
}

void WriteVariables(void* self, void* buffer, int slot, bool all, bool nativeConfigCaller) {
    // Host_WriteConfiguration is the sole native caller of this archive
    // writer. Keep the exact caller check so unrelated future invocations
    // never inherit the bridge's saved preferences.
    if (!nativeConfigCaller || !bridge::IsLocalSplitEnabled() || !active ||
        restorePending || serializing) {
        originalWriteVariables(self, buffer, slot, all);
        return;
    }
    float live[archivedCount]{};
    bool changed[archivedCount]{}, attempted[archivedCount]{};
    bool anyChanged = false;
    for (unsigned i = 0; i < archivedCount; ++i) {
        if (!Read(*archivedSettings[i], live[i])) {
            bridge::Log("[input] Cannot read %s for config protection", archivedSettings[i]->name);
            originalWriteVariables(self, buffer, slot, all);
            return;
        }
        changed[i] = live[i] != archivedSettings[i]->previous;
        anyChanged = anyChanged || changed[i];
    }
    if (!anyChanged) {
        originalWriteVariables(self, buffer, slot, all);
        return;
    }
    // Quit serializes config before the client unregisters its ConVars, but
    // plugin Unload runs afterward. Serialize the user's original archived
    // settings here, then restore the live menu/gameplay values synchronously.
    // The non-archived controller-config guard stays enabled throughout.
    serializing = true;
    bool prepared = true;
    __try {
        if (changed[0] && !Set(guard, 1)) prepared = false;
        for (unsigned i = 0; i < archivedCount && prepared; ++i) {
            if (!changed[i]) continue;
            attempted[i] = true;
            prepared = Set(*archivedSettings[i], archivedSettings[i]->previous);
        }
        if (!prepared) {
            // Best-effort rollback of every attempted write before forwarding
            // the native writer. If a rollback setter also fails, its retry
            // occurs in finally; this is not a transactional native API.
            for (unsigned i = archivedCount; i-- > 0;) {
                if (attempted[i] && Set(*archivedSettings[i], live[i])) attempted[i] = false;
            }
            bridge::Log("[input] Could not stage archived input preferences; native config write forwarded after rollback");
        }
        originalWriteVariables(self, buffer, slot, all);
    } __finally {
        bool restored = true;
        for (unsigned i = archivedCount; i-- > 0;) {
            if (attempted[i] && !Set(*archivedSettings[i], live[i])) restored = false;
        }
        serializing = false;
        bridge::Log("[input] Archived input protection prepared=%d restoredLive=%d; saved joystick=%.9g lowend=%.9g accelscale=%.9g accelmax=%.9g pitch/yaw=%.9g/%.9g,%.9g/%.9g",
            prepared, restored, joystick.previous, lowend.previous, accelScale.previous, accelMax.previous,
            pitch1.previous, yaw1.previous, pitch2.previous, yaw2.previous);
        bridge::Log("[input] Archived pitch threshold protected: saved=%.9g; weapon-model fov saved=%.9g",
            pitchThreshold.previous, viewmodelFov.previous);
    }
}

void __fastcall WriteVariablesHook(void* self, void*, void* buffer, int slot, bool all) {
    WriteVariables(self, buffer, slot, all,
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == writeConfigReturn);
}
}

bool Initialize(void* cvar) {
    if (active) return registry == cvar;
    registry = cvar;
    float value = 0;
    for (const Setting* setting : allSettings) {
        if (!Read(*setting, value)) {
            bridge::Log("[input] Cannot find native %s; menu controller activation refused", setting->name);
            registry = nullptr;
            return false;
        }
    }
    bridge::Log("[input] Native controller guard, found flag, joystick, keyboard owner, axis and Xbox response cvars found");
    return true;
}

bool InstallConfigProtection() {
    if (originalWriteVariables) return true;
    const auto module = bridge::Engine();
    if (!module || !bridge::IsLocalSplitEnabled()) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    // Verified engine.dll: Host_WriteConfiguration calls this __thiscall
    // writer with (CUtlBuffer*, -1, false), whose epilogue is ret 0xc.
    static constexpr unsigned char caller[] = {0xe8, 0xa2, 0xe1, 0xfc, 0xff};
    if (!Readable(reinterpret_cast<void*>(base + rva::engine::kWriteConfigCall), sizeof(caller)) ||
        std::memcmp(reinterpret_cast<void*>(base + rva::engine::kWriteConfigCall), caller, sizeof(caller))) return false;
    static constexpr unsigned char prefix[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x34};
    if (!bridge::InstallHook("native archived input preferences", module, rva::engine::kWriteArchivedVariables,
        prefix, sizeof(prefix), reinterpret_cast<void*>(&WriteVariablesHook),
        reinterpret_cast<void**>(&originalWriteVariables))) return false;
    writeConfigReturn = base + rva::engine::kWriteConfigReturn;
    bridge::Log("[input] Protected native archived joystick/response/sensitivity preferences at engine+15de80");
    return true;
}

bool Enable() {
    if (active && !restorePending) return true;
    if (restorePending) {
        Restore();
        if (active) return false;
    }
    for (Setting* setting : allSettings) {
        if (!Read(*setting, setting->previous)) return false;
    }
    active = true;
    setupAt = 0;
    splitView = false;
    // The guard must already be effective when the client next sees a found
    // controller and joystick1, and the found flag before it sees the guard.
    // No 360controller.cfg, undo360controller.cfg, exec, bind, or unbind occurs.
    // Match the ISO response curve and sensitivity magnitudes, adapting pitch
    // to PC's positive normal-look axis convention. joy_inverty/2 are separate
    // native preferences and remain untouched. Apply once for this explicit
    // bridge lifetime; preserve subsequent UI edits for the session, device
    // bindings, mouse input and native aim eligibility. Both InputSystems already
    // apply the same stick deadzone; Xbox's client adds no pitch threshold.
    // Clear only PC's extra look-axis threshold, leaving movement axes intact.
    // The user may keep their own stick settings instead (splitscreen_support.ini).
    const bool xboxSticks = settings::values.xboxSticks;
    bool applied = Set(popupShown, 1) && Set(guard, 1) && Set(found, 1) && Set(joystick, 1);
    if (xboxSticks) {
        applied = applied && Set(lowend, 0.75f) && Set(accelScale, 2) && Set(accelMax, 1) &&
            Set(pitch1, 0.8f) && Set(yaw1, -2.25f) &&
            Set(pitch2, 0.8f) && Set(yaw2, -2.25f) && Set(pitchThreshold, 0);
    }
    for (Axis& axis : axes) applied = applied && Set(axis.setting, axis.value);
    if (!applied) {
        bridge::Log("[input] Native controller activation failed; restoring snapshots");
        Restore();
        return false;
    }
    bridge::Log("[input] Menu controller enabled: guard1 found1 joystick1 (previous %.9g %.9g %.9g), gamepad axes mapped",
        guard.previous, found.previous, joystick.previous);
    bridge::Log("[input] Gamepad axes before: advanced %.9g, x %.9g y %.9g z %.9g r %.9g u %.9g v %.9g",
        axes[0].setting.previous, axes[1].setting.previous, axes[2].setting.previous, axes[3].setting.previous,
        axes[4].setting.previous, axes[5].setting.previous, axes[6].setting.previous);
    NameSecondPlayer();
    if (!xboxSticks) {
        bridge::Log("[input] Stick response and sensitivity left as the user set them: lowend=%.9g accelscale=%.9g accelmax=%.9g, pitch/yaw %.9g/%.9g, %.9g/%.9g, pitch threshold %.9g",
            lowend.previous, accelScale.previous, accelMax.previous, pitch1.previous, yaw1.previous,
            pitch2.previous, yaw2.previous, pitchThreshold.previous);
        return true;
    }
    bridge::Log("[input] Native Xbox response applied once: lowend=.75 accelscale=2 accelmax=1 (previous %.9g %.9g %.9g)",
        lowend.previous, accelScale.previous, accelMax.previous);
    bridge::Log("[input] Native Xbox sensitivity applied once: pitch=.8 yaw=-2.25 both slots, inversion unchanged (previous %.9g/%.9g, %.9g/%.9g)",
        pitch1.previous, yaw1.previous, pitch2.previous, yaw2.previous);
    bridge::Log("[input] Native Xbox pitch normalization: extra PC pitch threshold=0 (previous %.9g)", pitchThreshold.previous);
    return true;
}

bool MaintainMenu() {
    if (!active || restorePending || serializing) return false;
    float guardValue = 0, foundValue = 0, joystickValue = 0;
    if (!Read(guard, guardValue) || !Read(found, foundValue) || !Read(joystick, joystickValue)) return false;
    if (guardValue == 1 && foundValue == 1 && joystickValue == 1) return true;
    // These are the same temporary menu values as Enable(), but the original
    // user preferences remain the snapshot from that first activation. Never
    // enable the joystick if restoring the protective guard failed.
    if (!Set(guard, 1) || !Set(found, 1) || !Set(joystick, 1)) return false;
    // A cleared guard means the client ran one of its controller configs.
    bridge::Log("[input] Re-armed controller input: guard1 found1 joystick1 (were %.9g %.9g %.9g)",
        guardValue, foundValue, joystickValue);
    return true;
}

void ControllerFound() {
    // InputSystem's own state is now 1; Restore must not put 0 back.
    if (active) found.previous = 1;
}

bool SetKeyboardSlot(int slot) {
    if (!active || restorePending || serializing) return false;
    float current = 0;
    if (!Read(forceUser, current)) return false;
    if (current == static_cast<float>(slot)) return true;
    if (!Set(forceUser, static_cast<float>(slot))) return false;
    bridge::Log("[input] Keyboard and mouse now drive player %d", slot + 1);
    return true;
}

bool ReadNumber(const char* name, float& value) {
    const Setting setting{name, 0};
    return Read(setting, value);
}

void TrackSplitView(bool guestInGame) {
    if (!active || restorePending || serializing || guestInGame == splitView) return;
    splitView = guestInGame;
    if (guestInGame) return;  // the client applies the split layout's value itself
    // One full-screen view again (player two left, or the game ended): the
    // client leaves the split layout's weapon-model field of view in place.
    float live = 0;
    if (!Read(viewmodelFov, live) || live == viewmodelFov.previous) return;
    const bool restored = Set(viewmodelFov, viewmodelFov.previous);
    bridge::Log("[input] One view again: weapon-model fov %.9g -> %.9g (%d)", live, viewmodelFov.previous, restored);
}

void GuestActive() {
    // Let the client finish its own controller setup for the new player first.
    if (active && !restorePending) setupAt = GetTickCount64() + 500;
}

bool RunPlayerSetup(bool guestInGame) {
    if (!setupAt || GetTickCount64() < setupAt || !active || restorePending || serializing) return false;
    setupAt = 0;
    if (!guestInGame) return false;
    // Stock presets hold controller binds only (no unbindall). Each pad has
    // its own key codes, so each player execs their preset in their own
    // context, as the Controller options dialog does. The UI stores 0..3.
    int preset[2] = {0, 0};
    const Setting presetVars[2] = {{"joy_cfg_preset", 0}, {"joy_cfg_preset2", 0}};
    for (int slot = 0; slot < 2; ++slot) {
        float value = 0;
        if (Read(presetVars[slot], value) && value >= 0 && value <= 3) preset[slot] = static_cast<int>(value);
    }
    char command[192];
    _snprintf_s(command, sizeof(command), _TRUNCATE,
        "cmd1 exec joy_preset_%d.cfg; cmd2 exec joy_preset_%d.cfg; joyadvancedupdate; cmd1 +jlook; cmd2 +jlook\n",
        preset[0] + 1, preset[1] + 1);
    const bool sent = bridge::PrimaryCommand(command);
    bridge::Log("[input] Controller presets applied: player 1 = %d, player 2 = %d (sent=%d)",
        preset[0] + 1, preset[1] + 1, sent);
    return sent;
}

void Restore() {
    if (!active) return;
    restorePending = true;
    // Disable the joystick before releasing a previously-zero config guard.
    // Restore numeric values exactly, including a user's fractional E3 timer.
    const bool restoredJoystick = Set(joystick, joystick.previous);
    const bool restoredSplit = Set(forceUser, forceUser.previous);
    bool restoredAxes = true;
    for (Axis& axis : axes) restoredAxes = Set(axis.setting, axis.setting.previous) && restoredAxes;
    setupAt = 0;
    splitView = false;
    const bool restoredLowend = Set(lowend, lowend.previous);
    const bool restoredScale = Set(accelScale, accelScale.previous);
    const bool restoredMax = Set(accelMax, accelMax.previous);
    const bool restoredPitch1 = Set(pitch1, pitch1.previous);
    const bool restoredYaw1 = Set(yaw1, yaw1.previous);
    const bool restoredPitch2 = Set(pitch2, pitch2.previous);
    const bool restoredYaw2 = Set(yaw2, yaw2.previous);
    const bool restoredThreshold = Set(pitchThreshold, pitchThreshold.previous);
    const bool restoredFound = Set(found, found.previous);
    const bool restoredFov = Set(viewmodelFov, viewmodelFov.previous);
    // A failed joystick restore must never release the config guard while the
    // gamepad could still be enabled. Retain snapshots so shutdown can retry.
    const bool restoredGuard = restoredJoystick && Set(guard, guard.previous);
    // Only once the guard is released may the main menu run its own check.
    const bool restoredPopup = restoredGuard && Set(popupShown, popupShown.previous);
    if (!restoredJoystick) {
        const bool protectedBindings = Set(guard, 1);
        bridge::Log("[input] Joystick restore pending; config guard retained at1 (%d)", protectedBindings);
    }
    if (restoredJoystick && restoredSplit && restoredGuard && restoredLowend && restoredScale && restoredMax &&
        restoredPitch1 && restoredYaw1 && restoredPitch2 && restoredYaw2 && restoredThreshold && restoredAxes &&
        restoredFound && restoredPopup && restoredFov) {
        active = false;
        restorePending = false;
    }
    bridge::Log("[input] Restored menu input: joystick=%.9g (%d), keyboard owner=%.9g (%d), found=%.9g (%d), guard=%.9g (%d), axes (%d)",
        joystick.previous, restoredJoystick, forceUser.previous, restoredSplit, found.previous, restoredFound,
        guard.previous, restoredGuard, restoredAxes);
    bridge::Log("[input] Restored response preferences: lowend=%.9g (%d), accelscale=%.9g (%d), accelmax=%.9g (%d)",
        lowend.previous, restoredLowend, accelScale.previous, restoredScale, accelMax.previous, restoredMax);
    bridge::Log("[input] Restored sensitivity preferences: pitch/yaw1=%.9g (%d)/%.9g (%d), pitch/yaw2=%.9g (%d)/%.9g (%d)",
        pitch1.previous, restoredPitch1, yaw1.previous, restoredYaw1,
        pitch2.previous, restoredPitch2, yaw2.previous, restoredYaw2);
    bridge::Log("[input] Restored pitch threshold=%.9g (%d), weapon-model fov=%.9g (%d)",
        pitchThreshold.previous, restoredThreshold, viewmodelFov.previous, restoredFov);
}

void Shutdown() {
    Restore();
    // The plugin removes shared hooks before calling Shutdown().
    originalWriteVariables = nullptr;
    writeConfigReturn = 0;
    serializing = false;
    if (!active) registry = nullptr;
    else bridge::Log("[input] Shutdown restoration incomplete; snapshots retained and config guard protected where available");
}

void DumpState() {
    float guardValue = 0, foundValue = 0, joystickValue = 0, splitValue = 0;
    const bool readable = Read(guard, guardValue) && Read(found, foundValue) && Read(joystick, joystickValue) &&
        Read(forceUser, splitValue);
    bridge::Log("[input] active=%d restorePending=%d readable=%d guard=%.9g found=%.9g joystick=%.9g keyboardOwner=%.9g",
        active, restorePending, readable, guardValue, foundValue, joystickValue, splitValue);
    float lowendValue = 0, scaleValue = 0, maxValue = 0;
    const bool responseReadable = Read(lowend, lowendValue) && Read(accelScale, scaleValue) && Read(accelMax, maxValue);
    bridge::Log("[input] responseReadable=%d lowend=%.9g accelscale=%.9g accelmax=%.9g saved=%.9g/%.9g/%.9g",
        responseReadable, lowendValue, scaleValue, maxValue, lowend.previous, accelScale.previous, accelMax.previous);
    float pitchValue1 = 0, yawValue1 = 0, pitchValue2 = 0, yawValue2 = 0;
    const bool sensitivityReadable = Read(pitch1, pitchValue1) && Read(yaw1, yawValue1) &&
        Read(pitch2, pitchValue2) && Read(yaw2, yawValue2);
    bridge::Log("[input] sensitivityReadable=%d pitch/yaw1=%.9g/%.9g pitch/yaw2=%.9g/%.9g saved=%.9g/%.9g,%.9g/%.9g",
        sensitivityReadable, pitchValue1, yawValue1, pitchValue2, yawValue2,
        pitch1.previous, yaw1.previous, pitch2.previous, yaw2.previous);
    float thresholdValue = 0;
    const bool thresholdReadable = Read(pitchThreshold, thresholdValue);
    bridge::Log("[input] pitchThresholdReadable=%d pitchthreshold=%.9g saved=%.9g",
        thresholdReadable, thresholdValue, pitchThreshold.previous);
}
}
