#pragma once

namespace input {
// Uses the native ICvar/IConVar interfaces; discovery does not change values.
bool Initialize(void* cvar);
// Explicitly enabled, fingerprint-gated hook for native config serialization.
bool InstallConfigProtection();
// Main thread only. Snapshot once, then protect keyboard bindings before
// enabling menu controller input and the existing split-screen map bootstrap.
// Applies the ISO response curve and PC-sign-adapted pitch/yaw for both slots,
// clearing PC's additional pitch deadzone after the shared hardware deadzone.
// Applied once per bridge lifetime. Inversion is unchanged, UI edits stay live
// for the session, and config writes/disable retain original saved preferences.
bool Enable();
// Caller must restrict this to an owned session or the menu without a
// matchmaking session. Re-arm guard, found flag and joystick without replacing
// snapshots.
bool MaintainMenu();
// A controller appeared after launch and the game now polls it: the found
// flag stays set when local co-op is switched off.
void ControllerFound();
// Keyboard and mouse drive this local player (0 or 1); controllers always
// drive the player of their own number. Call every frame; writes on change.
bool SetKeyboardSlot(int slot);
// Call every frame with whether player two has a view. When it goes away the
// weapon-model field of view returns to the single-player value; the client
// only ever sets the split layout's.
void TrackSplitView(bool guestInGame);
// Player two's client became active on this map or rejoined: schedule the
// per-player controller setup (each player's preset binds in their own
// command context, joystick look). RunPlayerSetup performs it once due.
void GuestActive();
bool RunPlayerSetup(bool guestInGame);
// Current numeric value of any registered cvar (after Initialize).
bool ReadNumber(const char* name, float& value);
void Restore();
void Shutdown();
void DumpState();
}
