#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstddef>
#include <cstdint>

// CCommand's public ABI from AlliedModders hl2sdk/l4d2 tier1/convar.h.
class CCommand {
public:
    int ArgC() const { return argc_; }
    const char* Arg(int n) const { return n >= 0 && n < argc_ && n < 64 ? argv_[n] : ""; }
    const char* operator[](int n) const { return Arg(n); }
private:
    int argc_, argv0size_;
    char args_[512], buffer_[512];
    const char* argv_[64];
};
static_assert(sizeof(void*) == 4, "This bridge targets the x86 L4D2 client only");
static_assert(sizeof(CCommand) == 1288, "Source CCommand ABI mismatch");

namespace bridge {
using CommandCallback = void (*)(const CCommand&);
void Log(const char* format, ...);
HMODULE Matchmaking();
HMODULE GameUI();
// Null unless server.dll matched the exact researched file fingerprint.
HMODULE Server();
// Null unless engine.dll matched the exact researched file fingerprint.
HMODULE Engine();
// Null unless inputsystem.dll matched the exact presentation-hook fingerprint.
HMODULE InputSystem();
// Null unless client.dll matched the exact researched HUD/layout fingerprint.
HMODULE Client();
void* Framework();
void* PlayerManager();
void* HostProfile();
// Explicit opt-in + -insecure + exact supported binaries. Individual detours
// must additionally verify that the operation belongs to an offline session.
bool IsLocalSplitEnabled();
// Queue console text in player one's command buffer. The engine keeps one
// buffer per split-screen slot; the console and IVEngineClient queue for the
// slot that owns the keyboard, where ss_connect / ss_disconnect do nothing.
bool PrimaryCommand(const char* text);
// The engine's second split-screen slot is in use (player two is connected).
bool GuestInGame();
bool RegisterCommand(const char* name, const char* help, CommandCallback callback);
bool InstallHook(const char* label, HMODULE module, uint32_t rva,
                 const uint8_t* expected, size_t expectedSize,
                 void* detour, void** original);
void RemoveHooks();
template<class Function> Function Virtual(void* object, size_t slot) {
    return reinterpret_cast<Function>((*reinterpret_cast<void***>(object))[slot]);
}
}
