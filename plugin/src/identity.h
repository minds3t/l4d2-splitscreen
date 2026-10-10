#pragma once

namespace identity {
// Read-only diagnostics, using real edicts delivered by server callbacks.
bool Initialize(void* engineServer);
// Explicit enabled offline bridge only; narrowly supplies the guest's local
// identity to four Director avatar-cache writer/reader calls.
bool InstallAvatarBridge();
// Call only after shared hook removal. Read-only diagnostics remain available.
void ResetAvatarBridge();
void ObserveClient(void* edict, const char* event);
// The split-screen player attached to this machine's host in an owned game.
bool IsLocalGuest(void* edict);
void ResetClients();
void DumpState();
void Shutdown();
}
