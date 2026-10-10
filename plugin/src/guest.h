#pragma once

#include <Windows.h>
#include <cstdint>

namespace guest {
using LogCallback = void (*)(const char* message);

// Read-only validation. Registration is a separate, explicitly enabled action.
bool Initialize(HMODULE matchmaking, LogCallback logger);
// Call on the game's main thread, only for the enabled offline local session.
// Refuses to replace an existing native second profile or a foreign manager.
bool Register(void* manager, void* hostProfile);
void Unregister();
void Shutdown();

bool IsLocalSplitEnabled();
std::uint64_t GuestXuid();
const char* GuestName();
void* GuestProfile();
}
