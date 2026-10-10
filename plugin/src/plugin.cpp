#include "source_plugin_abi.h"
#include "supported_build.h"
#include "addresses.h"
#include "build_10097.h"
#include "readable.h"
#include "settings.h"
#include <bcrypt.h>
#include <shellapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#if SS_NATIVE_GUEST_LOBBY
#include "guest.h"
#include "lobby.h"
#include "input.h"
#include "devices.h"
#include "join.h"
#endif
#if SS_NATIVE_PRESENTATION
#include "presentation.h"
#endif
#if SS_NATIVE_SAFEAREA
#include "safearea.h"
#endif
#if SS_NATIVE_HUD_INVENTORY
#include "hud_inventory.h"
#endif
#if SS_NATIVE_HUD_PROMPTS
#include "hud_prompts.h"
#endif
#if SS_NATIVE_IDENTITY
#include "identity.h"
#endif

namespace {
HMODULE selfModule{}, matchmakingModule{}, gameuiModule{}, serverModule{}, engineModule{}, inputsystemModule{}, clientModule{};
void* framework{};
void* cvar{};
void* engineServer{};
bool enabled{}, hashesAccepted{}, insecure{}, loaded{};
bool serverVerified{}, engineVerified{}, inputsystemVerified{}, clientVerified{};
int commandDllId = -1;
char logPath[MAX_PATH]{};
using MsgFn = void (__cdecl*)(const char*, ...);
MsgFn consoleMsg{};

// Virtual order and first 36 bytes match the L4D2 SDK ConCommand ABI. We do
// not link a foreign tier1 CRT or use engine-owned memory for these objects.
class SourceCommand {
public:
    SourceCommand(const char* n, const char* h, bridge::CommandCallback cb)
        : name(n), help(h), callback(cb), hasCompletion(false), newCallback(true), interfaceCallback(false) {}
    virtual ~SourceCommand() = default;
    virtual bool IsCommand() const { return true; }
    virtual bool IsFlagSet(int f) const { return (flags & f) != 0; }
    virtual void AddFlags(int f) { flags |= f; }
    virtual void RemoveFlags(int f) { flags &= ~f; }
    virtual int GetFlags() const { return flags; }
    virtual const char* GetName() const { return name; }
    virtual const char* GetHelpText() const { return help; }
    virtual bool IsRegistered() const { return registered; }
    virtual int GetDLLIdentifier() const { return commandDllId; }
    virtual void Create(const char*, const char*, int) {}
    virtual void Init() {}
    virtual int AutoCompleteSuggest(const char*, void*) { return 0; }
    virtual bool CanAutoComplete() { return false; }
    virtual void Dispatch(const CCommand& args) { if (callback) callback(args); }
private:
    SourceCommand* next{};
    bool registered{};
    const char* name{};
    const char* help{};
    int flags{};
    bridge::CommandCallback callback{};
    void* completion{};
    bool hasCompletion : 1;
    bool newCallback : 1;
    bool interfaceCallback : 1;
};
static_assert(sizeof(SourceCommand) == 36, "L4D2 ConCommand layout changed");
std::vector<std::unique_ptr<SourceCommand>> commands;

bool HasInsecureArgument() {
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return false;
    bool found = false;
    for (int i = 1; i < count; ++i) if (!_wcsicmp(args[i], L"-insecure")) found = true;
    LocalFree(args);
    return found;
}

bool CheckModuleHash(HMODULE module, const char* name, const char* expected) {
    wchar_t path[MAX_PATH]{};
    if (!module || !GetModuleFileNameW(module, path, MAX_PATH)) {
        bridge::Log("%s not loaded", name); return false;
    }
    FILE* file = _wfopen(path, L"rb");
    if (!file) return false;
    BCRYPT_ALG_HANDLE alg{};
    BCRYPT_HASH_HANDLE hash{};
    DWORD objectSize{}, written{};
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &written, 0) >= 0;
    std::vector<unsigned char> object(objectSize);
    if (ok) ok = BCryptCreateHash(alg, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
    unsigned char buffer[32768], digest[32]{};
    size_t length;
    while (ok && (length = std::fread(buffer, 1, sizeof(buffer), file)))
        ok = BCryptHashData(hash, buffer, static_cast<ULONG>(length), 0) >= 0;
    if (std::ferror(file)) ok = false;
    std::fclose(file);
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    char actual[65]{};
    for (unsigned i = 0; i < 32; ++i) std::sprintf(actual + i * 2, "%02x", digest[i]);
    ok = ok && !std::strcmp(actual, expected);
    bridge::Log("%s SHA256=%s researched=%d", name, actual, ok);
    return ok;
}

// A module is supported when every address the bridge uses in it was found
// (addresses.cpp). The file hash only tells whether it is the researched build,
// where each address must also sit exactly where it was researched.
bool CheckSupported(HMODULE module, const char* name) {
    bool researched = false;
    for (const auto& supported : kSupportedModules)
        if (!std::strcmp(supported.name, name)) researched = CheckModuleHash(module, name, supported.sha256);
    const bool found = addresses::Found(name);
    const int moved = addresses::Moved(name);
    bridge::Log("%s addresses found=%d moved=%d", name, found, moved);
    return found && !(researched && moved);
}

void* ModuleInterface(HMODULE module, const char* version) {
    if (!module) return nullptr;
    auto factory = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
    return factory ? factory(version, nullptr) : nullptr;
}

void Diagnostic(const CCommand&) {
    bridge::Log("status loaded=%d insecure=%d supported=%d enabled=%d framework=%p", loaded, insecure, hashesAccepted, enabled, framework);
    if (!hashesAccepted || !framework) return;
    __try {
        auto manager = bridge::PlayerManager();
        auto session = bridge::Virtual<void* (__thiscall*)(void*)>(framework, 9)(framework);
        bridge::Log("matchmaking=%p gameui=%p manager=%p session=%p", matchmakingModule, gameuiModule, manager, session);
        for (int slot = 0; manager && slot < 4; ++slot) {
            auto player = bridge::Virtual<void* (__thiscall*)(void*, int)>(manager, 1)(manager, slot);
            if (!player) { bridge::Log("local[%d]=null", slot); continue; }
            auto xuid = bridge::Virtual<uint64_t (__thiscall*)(void*)>(player, 0)(player);
            auto controller = bridge::Virtual<int (__thiscall*)(void*)>(player, 1)(player);
            auto name = bridge::Virtual<const char* (__thiscall*)(void*)>(player, 2)(player);
            auto state = bridge::Virtual<int (__thiscall*)(void*)>(player, 3)(player);
            bridge::Log("local[%d]=%p controller=%d xuid=%016llx state=%d name=%.128s", slot, player, controller, xuid, state, name ? name : "<null>");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        bridge::Log("Read-only diagnostic caught native exception %08lx; no further traversal", GetExceptionCode());
    }
}

// The engine still registers its split-screen drop-in/drop-out commands, but
// as development-only, which hides them from the console and from menus.
// ConCommandBase virtuals 3/4 are AddFlags/RemoveFlags (see SourceCommand).
constexpr int kDevelopmentOnly = 1 << 1;
bool dropInExposed{};
void ExposeDropIn(bool expose) {
    if (!cvar || expose == dropInExposed) return;
    for (const char* name : {"ss_connect", "ss_disconnect"}) {
        void* command = bridge::Virtual<void* (__thiscall*)(void*, const char*)>(cvar, 10)(cvar, name);
        if (!command) { bridge::Log("Engine command %s not found; drop-in/out unavailable", name); continue; }
        bridge::Virtual<void (__thiscall*)(void*, int)>(command, expose ? 4 : 3)(command, kDevelopmentOnly);
    }
    dropInExposed = expose;
    bridge::Log("Engine split-screen drop-in/out commands %s", expose ? "exposed" : "hidden again");
}

// Valve's menu files switch on [$X360SPLITSCREEN] (the main menu shows the
// OFFLINE CO-OP tile instead of SINGLE PLAYER). On Xbox the symbol follows the
// number of signed-in players; on PC it is false unless set here. vstdlib's
// IKeyValuesSystem slot 8 is SetKeyValuesExpressionSymbol(name, value), slot 9
// the getter that consults that table before its built-in answers.
void SetSplitscreenSymbol(bool value) {
    HMODULE vstdlib = GetModuleHandleW(L"vstdlib.dll");
    using Accessor = void*(__cdecl*)();
    const auto accessor = vstdlib ? reinterpret_cast<Accessor>(GetProcAddress(vstdlib, "KeyValuesSystem")) : nullptr;
    void* system = accessor ? accessor() : nullptr;
    // push ebp; mov ebp,esp; mov eax,[ebp+8]; sub esp,8; push edi; mov edi,ecx
    static constexpr std::uint8_t setter[] = {0x55,0x8b,0xec,0x8b,0x45,0x08,0x83,0xec,0x08,0x57,0x8b,0xf9};
    if (!bridge::Readable(system, sizeof(void*)) ||
        !bridge::Readable(*static_cast<void***>(system), 10 * sizeof(void*))) {
        bridge::Log("Menu split-screen condition unavailable: no KeyValues system"); return;
    }
    void* function = (*static_cast<void***>(system))[8];
    if (!bridge::Readable(function, sizeof(setter)) || std::memcmp(function, setter, sizeof(setter)) != 0) {
        bridge::Log("Menu split-screen condition unavailable: unexpected vstdlib code"); return;
    }
    bridge::Virtual<void(__thiscall*)(void*, const char*, bool)>(system, 8)(system, "X360SPLITSCREEN", value);
}

void DisableBridge() {
    const bool wasEnabled = enabled;
    enabled = false;
    ExposeDropIn(false);
    SetSplitscreenSymbol(false);
#if SS_NATIVE_SAFEAREA
    safearea::Refresh();
#endif
    bridge::RemoveHooks();
#if SS_NATIVE_HUD_PROMPTS
    hud_prompts::Shutdown();
#endif
#if SS_NATIVE_HUD_INVENTORY
    hud_inventory::Shutdown();
#endif
#if SS_NATIVE_SAFEAREA
    safearea::Shutdown();
#endif
#if SS_NATIVE_PRESENTATION
    presentation::Shutdown();
#endif
#if SS_NATIVE_IDENTITY
    identity::ResetAvatarBridge();
#endif
#if SS_NATIVE_GUEST_LOBBY
    join::Shutdown();
    lobby::Shutdown();
    guest::Shutdown();
    input::Shutdown();
    devices::Shutdown();
#endif
    bridge::Log("%s; hooks removed and cleanup attempted", wasEnabled ? "Disabled" : "Already disabled");
}
// The main menu evaluates [$X360SPLITSCREEN] when it loads its resources: the
// offline tile and the co-op switch row. Reload it after a switch, from the
// next frame (the switch row itself runs inside the menu's command dispatch).
// Only the bare main menu: a lobby's reload needs its matchmaking session,
// and before the UI exists there is nothing to reload.
void ReloadMainMenu() {
    if (!hashesAccepted || !framework || !gameuiModule) return;
    if (bridge::Virtual<void*(__thiscall*)(void*)>(framework, 9)(framework)) return;
    const auto base = reinterpret_cast<std::uintptr_t>(gameuiModule);
    for (const auto object : {rva::gameui::kMenuReloadObjectA, rva::gameui::kMenuReloadObjectB}) {
        void** pointer = reinterpret_cast<void**>(base + object);
        if (!bridge::Readable(pointer, sizeof(void*)) || !*pointer) return;
    }
    bridge::PrimaryCommand("ui_reloadscheme\n");
}

void Disable(const CCommand&) {
    const bool wasEnabled = enabled;
    DisableBridge();
    if (wasEnabled) ReloadMainMenu();
}
void Enable(const CCommand& args) {
    (void)args;
    if (enabled) { bridge::Log("Already enabled"); return; }
    if (!insecure || !hashesAccepted || !framework) {
        bridge::Log("Enable refused: requires -insecure, supported modules and MATCHFRAMEWORK_001"); return;
    }
    settings::Load(logPath);
#if SS_NATIVE_GUEST_LOBBY
    if (!guest::Initialize(matchmakingModule, [](const char* message) { bridge::Log("%s", message); })) {
        bridge::Log("Enable refused: guest validation failed"); return;
    }
    if (!input::Initialize(cvar)) {
        guest::Shutdown();
        bridge::Log("Enable refused: native input discovery failed"); return;
    }
    enabled = true;
    // Optional: without it the keyboard stays on player 2.
    devices::Initialize();
    if (!join::Install()) { DisableBridge(); bridge::Log("Enable failed: engine two-player connect unavailable"); return; }
    if (!lobby::Install()) { DisableBridge(); bridge::Log("Enable failed; cleaned up"); return; }
#if SS_NATIVE_IDENTITY
    if (!identity::InstallAvatarBridge()) { DisableBridge(); bridge::Log("Enable failed: native avatar identity bridge unavailable"); return; }
#endif
    if (!input::InstallConfigProtection()) { DisableBridge(); bridge::Log("Enable failed: native config protection unavailable"); return; }
#if SS_NATIVE_PRESENTATION
    if (!presentation::Install()) { DisableBridge(); bridge::Log("Enable failed: native menu presentation unavailable"); return; }
#endif
#if SS_NATIVE_SAFEAREA
    if (!safearea::Install()) { DisableBridge(); bridge::Log("Enable failed: native safe-area layout unavailable"); return; }
#endif
#if SS_NATIVE_HUD_INVENTORY
    // The item cross is part of the console look; the PC keeps its item list.
    if (settings::values.consoleUi && !hud_inventory::Install()) {
        DisableBridge(); bridge::Log("Enable failed: native Xbox inventory layout unavailable"); return;
    }
#endif
#if SS_NATIVE_HUD_PROMPTS
    if (!hud_prompts::Install()) { DisableBridge(); bridge::Log("Enable failed: native per-player HUD prompts unavailable"); return; }
#endif
    if (!input::Enable()) { DisableBridge(); bridge::Log("Enable failed: input activation rolled back"); return; }
    SetSplitscreenSymbol(true);
#if SS_NATIVE_SAFEAREA
    safearea::Refresh();
#endif
    ExposeDropIn(true);
    bridge::Log("Local bridge enabled");
    ReloadMainMenu();
#else
    bridge::Log("Diagnostic-only build: guest/lobby modules are not linked");
#endif
}

#if SS_NATIVE_GUEST_LOBBY
void CreateLobby(const CCommand& args) {
    if (args.ArgC() > 2) { bridge::Log("Usage: splitscreen_create [coop|realism|survival|versus|scavenge]"); return; }
    lobby::CreateOfflineSession(args.ArgC() == 2 ? args[1] : "coop");
}
void SelectAvatar(const CCommand& args) {
    if (args.ArgC() != 3 || (std::strcmp(args[1], "0") && std::strcmp(args[1], "1"))) {
        bridge::Log("Usage: splitscreen_avatar <0|1> <native avatar name>"); return;
    }
    lobby::SelectAvatar(args[1][0] - '0', args[2]);
}
void StartLobby(const CCommand&) { lobby::StartSession(); }
void CloseLobby(const CCommand&) { lobby::CloseSession(); }
void LobbyStatus(const CCommand&) {
    if (!hashesAccepted || !framework) { bridge::Log("Lobby diagnostic refused: unsupported or unavailable native module"); return; }
    lobby::DumpState();
}
void InputStatus(const CCommand&) {
    if (!hashesAccepted || !cvar) { bridge::Log("Input diagnostic refused: unsupported or unavailable native module"); return; }
    if (input::Initialize(cvar)) input::DumpState();
}
#endif

#if SS_NATIVE_IDENTITY
void IdentityStatus(const CCommand&) {
    if (!hashesAccepted || !engineServer) { bridge::Log("Identity diagnostic refused: unsupported or unavailable native module"); return; }
    identity::DumpState();
}
#endif

void UnregisterCommands() {
    if (cvar) for (auto& command : commands)
        bridge::Virtual<void (__thiscall*)(void*, SourceCommand*)>(cvar, 7)(cvar, command.get());
    commands.clear();
}

class Plugin final : public IServerPluginCallbacks {
public:
    bool Load(CreateInterfaceFn factory, CreateInterfaceFn) override {
        if (loaded) return false;
        GetModuleFileNameA(selfModule, logPath, MAX_PATH);
        if (auto slash = std::strrchr(logPath, '\\')) std::strcpy(slash + 1, "splitscreen_support.log");
        consoleMsg = reinterpret_cast<MsgFn>(GetProcAddress(GetModuleHandleW(L"tier0.dll"), "Msg"));
        bridge::Log("Load Splitscreen Support; log=%s", logPath);
        insecure = HasInsecureArgument();
        if (!insecure) { bridge::Log("Load refused: launch explicitly with -insecure"); return false; }
        matchmakingModule = GetModuleHandleW(L"matchmaking.dll");
        gameuiModule = GetModuleHandleW(L"gameui.dll");
        serverModule = GetModuleHandleW(L"server.dll");
        engineModule = GetModuleHandleW(L"engine.dll");
        inputsystemModule = GetModuleHandleW(L"inputsystem.dll");
        clientModule = GetModuleHandleW(L"client.dll");
        for (const char* note = addresses::Notes(); *note;) {
            const char* end = std::strchr(note, 10);
            bridge::Log("address %.*s", static_cast<int>(end - note), note);
            note = end + 1;
        }
        const bool mm = CheckSupported(matchmakingModule, "matchmaking.dll");
        const bool ui = CheckSupported(gameuiModule, "gameui.dll");
        engineVerified = CheckSupported(engineModule, "engine.dll");
        serverVerified = CheckSupported(serverModule, "server.dll");
        clientVerified = CheckSupported(clientModule, "client.dll");
        inputsystemVerified = CheckSupported(inputsystemModule, "inputsystem.dll");
        hashesAccepted = mm && ui && engineVerified && serverVerified && clientVerified && inputsystemVerified;
        framework = factory ? factory("MATCHFRAMEWORK_001", nullptr) : nullptr;
        if (!framework) framework = ModuleInterface(matchmakingModule, "MATCHFRAMEWORK_001");
        cvar = factory ? factory("VEngineCvar007", nullptr) : nullptr;
        if (!cvar) cvar = ModuleInterface(GetModuleHandleW(L"vstdlib.dll"), "VEngineCvar007");
        if (!cvar) { bridge::Log("Load failed: VEngineCvar007 unavailable"); return false; }
        engineServer = factory ? factory("VEngineServer022", nullptr) : nullptr;
        if (!engineServer) engineServer = ModuleInterface(GetModuleHandleW(L"engine.dll"), "VEngineServer022");
#if SS_NATIVE_IDENTITY
        if (hashesAccepted && !identity::Initialize(engineServer))
            bridge::Log("Read-only client identity diagnostic unavailable");
#endif
        commandDllId = bridge::Virtual<int (__thiscall*)(void*)>(cvar, 5)(cvar);
        bool ok = bridge::RegisterCommand("splitscreen_status", "Show native local-lobby bridge diagnostics", Diagnostic);
        ok = bridge::RegisterCommand("splitscreen_diag", "Show native local-lobby bridge diagnostics", Diagnostic) && ok;
        ok = bridge::RegisterCommand("splitscreen_enable", "Enable the experimental local-only guest bridge", Enable) && ok;
        ok = bridge::RegisterCommand("splitscreen_disable", "Disable the experimental bridge and remove hooks", Disable) && ok;
#if SS_NATIVE_GUEST_LOBBY
        ok = bridge::RegisterCommand("splitscreen_create", "Create a real native offline session: [mode]", CreateLobby) && ok;
        ok = bridge::RegisterCommand("splitscreen_avatar", "Choose native lobby character: <0|1> <avatar>", SelectAvatar) && ok;
        ok = bridge::RegisterCommand("splitscreen_start", "Start the owned native offline lobby", StartLobby) && ok;
        ok = bridge::RegisterCommand("splitscreen_close", "Close the owned native offline lobby", CloseLobby) && ok;
        ok = bridge::RegisterCommand("splitscreen_lobby_status", "Inspect native offline member records", LobbyStatus) && ok;
        ok = bridge::RegisterCommand("splitscreen_input_status", "Inspect controller-config guard and joystick state", InputStatus) && ok;
#endif
#if SS_NATIVE_IDENTITY
        ok = bridge::RegisterCommand("splitscreen_identity_status", "Inspect identities of actual server client edicts", IdentityStatus) && ok;
#endif
        if (!ok) { UnregisterCommands(); return false; }
        loaded = true;
        bridge::Log("Loaded read-only. Use splitscreen_status before explicit splitscreen_enable");
        return true;
    }
    void Unload() override {
        // Use the same cleanup without constructing a fake CCommand.
        enabled = false;
        ExposeDropIn(false);
        SetSplitscreenSymbol(false);
        bridge::RemoveHooks();
#if SS_NATIVE_HUD_PROMPTS
        hud_prompts::Shutdown();
#endif
#if SS_NATIVE_HUD_INVENTORY
        hud_inventory::Shutdown();
#endif
#if SS_NATIVE_SAFEAREA
        safearea::Shutdown();
#endif
#if SS_NATIVE_PRESENTATION
        presentation::Shutdown();
#endif
#if SS_NATIVE_IDENTITY
        identity::ResetAvatarBridge();
#endif
#if SS_NATIVE_GUEST_LOBBY
        join::Shutdown();
        lobby::Shutdown();
        guest::Shutdown();
        input::Shutdown();
        devices::Shutdown();
#endif
#if SS_NATIVE_IDENTITY
        identity::Shutdown();
#endif
        UnregisterCommands();
        loaded = false;
        bridge::Log("Unload complete");
        framework = cvar = engineServer = nullptr;
    }
    void Pause() override { DisableBridge(); bridge::Log("Paused; explicitly enable again after unpause"); }
    void UnPause() override { bridge::Log("Unpaused; read-only until explicitly enabled"); }
    const char* GetPluginDescription() override { return "Splitscreen Support (local split-screen co-op)"; }
    void LevelInit(const char* map) override {
#if SS_NATIVE_IDENTITY
        identity::ResetClients();
#endif
        bridge::Log("LevelInit %.128s", map);
    }
    void ServerActivate(edict_t*, int entities, int clients) override { bridge::Log("ServerActivate entities=%d maxClients=%d", entities, clients); }
    void GameFrame(bool) override {}
    void LevelShutdown() override {
#if SS_NATIVE_IDENTITY
        identity::ResetClients();
#endif
        bridge::Log("LevelShutdown");
    }
    void ClientActive(edict_t* player) override {
#if SS_NATIVE_IDENTITY
        if (hashesAccepted) identity::ObserveClient(player, "ClientActive");
#if SS_NATIVE_GUEST_LOBBY
        // Player two is in the game (map start or rejoin): set up both pads.
        if (enabled && identity::IsLocalGuest(player)) input::GuestActive();
#endif
#endif
    }
    void ClientDisconnect(edict_t* player) override {
#if SS_NATIVE_IDENTITY
        if (hashesAccepted) identity::ObserveClient(player, "ClientDisconnect");
#endif
    }
    void ClientPutInServer(edict_t* player, const char*) override {
#if SS_NATIVE_IDENTITY
        if (hashesAccepted) identity::ObserveClient(player, "ClientPutInServer");
#endif
    }
    void SetCommandClient(int) override {}
    void ClientSettingsChanged(edict_t*) override {}
    PLUGIN_RESULT ClientConnect(bool*, edict_t*, const char*, const char*, char*, int) override { return PLUGIN_CONTINUE; }
    PLUGIN_RESULT ClientCommand(edict_t*, const CCommand&) override { return PLUGIN_CONTINUE; }
    PLUGIN_RESULT NetworkIDValidated(const char*, const char*) override { return PLUGIN_CONTINUE; }
    void OnQueryCvarValueFinished(QueryCvarCookie_t, edict_t*, EQueryCvarValueStatus, const char*, const char*) override {}
};
Plugin plugin;
}

namespace bridge {
void Log(const char* format, ...) {
    char text[2048];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    va_end(args);
    if (consoleMsg) consoleMsg("[splitscreen_support] %s\n", text);
    if (logPath[0]) if (auto file = std::fopen(logPath, "a")) {
        SYSTEMTIME time; GetLocalTime(&time);
        std::fprintf(file, "%02u:%02u:%02u.%03u %s\n", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, text);
        std::fclose(file);
    }
}
HMODULE Matchmaking() { return matchmakingModule; }
HMODULE GameUI() { return gameuiModule; }
HMODULE Server() { return serverVerified ? serverModule : nullptr; }
HMODULE Engine() { return engineVerified ? engineModule : nullptr; }
HMODULE InputSystem() { return inputsystemVerified ? inputsystemModule : nullptr; }
HMODULE Client() { return clientVerified ? clientModule : nullptr; }
void* Framework() { return framework; }
void* PlayerManager() {
    if (!framework || !hashesAccepted) return nullptr;
    auto system = Virtual<void* (__thiscall*)(void*)>(framework, 11)(framework);
    return system ? Virtual<void* (__thiscall*)(void*)>(system, 0)(system) : nullptr;
}
void* HostProfile() {
    auto manager = PlayerManager();
    return manager ? Virtual<void* (__thiscall*)(void*, int)>(manager, 1)(manager, 0) : nullptr;
}
bool IsLocalSplitEnabled() { return enabled && insecure && hashesAccepted; }
bool PrimaryCommand(const char* text) {
    HMODULE engine = Engine();
    if (!engine || !text) return false;
    // Cbuf_AddText(slot, text, delay): push ebp; mov ebp,esp; call [GetCurrentThreadId]
    static constexpr std::uint8_t prefix[] = {0x55, 0x8b, 0xec, 0xff, 0x15};
    const auto add = reinterpret_cast<void(__cdecl*)(int, const char*, int)>(
        reinterpret_cast<std::uintptr_t>(engine) + rva::engine::kCbufAddText);
    if (!Readable(reinterpret_cast<void*>(add), sizeof(prefix)) ||
        std::memcmp(reinterpret_cast<void*>(add), prefix, sizeof(prefix)) != 0) return false;
    add(0, text, 0);
    return true;
}
bool GuestInGame() {
    // ISplitScreen slot 7, as ss_disconnect itself asks (call [vtbl+1c]).
    HMODULE engine = Engine();
    void** global = engine ? reinterpret_cast<void**>(
        reinterpret_cast<std::uintptr_t>(engine) + rva::engine::kSplitScreenInterface) : nullptr;
    void* split = Readable(global, sizeof(void*)) ? *global : nullptr;
    if (!Readable(split, sizeof(void*)) || !Readable(*static_cast<void***>(split), 8 * sizeof(void*))) return false;
    return Virtual<bool(__thiscall*)(void*, int)>(split, 7)(split, 1);
}
bool RegisterCommand(const char* name, const char* help, CommandCallback callback) {
    if (!cvar) return false;
    if (Virtual<void* (__thiscall*)(void*, const char*)>(cvar, 10)(cvar, name)) {
        Log("Command already exists: %s", name); return false;
    }
    auto command = std::make_unique<SourceCommand>(name, help, callback);
    Virtual<void (__thiscall*)(void*, SourceCommand*)>(cvar, 6)(cvar, command.get());
    commands.push_back(std::move(command));
    return true;
}
}

extern "C" __declspec(dllexport) void* __cdecl CreateInterface(const char* name, int* result) {
    if (name && (!std::strcmp(name, "ISERVERPLUGINCALLBACKS002") || !std::strcmp(name, "ISERVERPLUGINCALLBACKS001"))) {
        if (result) *result = 0;
        return &plugin;
    }
    if (result) *result = 1;
    return nullptr;
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { selfModule = module; DisableThreadLibraryCalls(module); }
    return TRUE;
}
