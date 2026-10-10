#pragma once

namespace lobby {
// All operations are game-main-thread only. Hook activation is also subject to
// bridge's explicit enable, exact-build and -insecure gates.
bool Install();
void Shutdown();
// Exact owned-offline session check, including while its local game is running.
bool HasOwnedOfflineSession();
// Scope for the exact native footer paint hooks: owned offline lobby/game
// (including its pause UI), or disconnected main menu with no session.
// Unrelated sessions and connected games without ownership remain native.
bool UseConsoleMenuPresentation();

// Diagnostic entry points use the same native session and avatar commands as
// the UI. They do not construct replacement lobby rows or transfer survivors.
bool CreateOfflineSession(const char* mode);
bool SelectAvatar(int slot, const char* avatar);
bool StartSession();
bool CloseSession();
void DumpState();
}
