#pragma once
#include "bridge.h"

// Narrow declarations preserving the exact vtable in public/engine/iserverplugin.h
// from AlliedModders hl2sdk, l4d2 branch. Original header retained in vendor/.
// No virtual destructor exists in this interface.
using CreateInterfaceFn = void* (__cdecl*)(const char*, int*);
struct edict_t;
enum PLUGIN_RESULT { PLUGIN_CONTINUE, PLUGIN_OVERRIDE, PLUGIN_STOP };
enum EQueryCvarValueStatus { ValueIntact, CvarNotFound, NotACvar, CvarProtected };
using QueryCvarCookie_t = int;
class IServerPluginCallbacks {
public:
    virtual bool Load(CreateInterfaceFn, CreateInterfaceFn) = 0;
    virtual void Unload() = 0;
    virtual void Pause() = 0;
    virtual void UnPause() = 0;
    virtual const char* GetPluginDescription() = 0;
    virtual void LevelInit(const char*) = 0;
    virtual void ServerActivate(edict_t*, int, int) = 0;
    virtual void GameFrame(bool) = 0;
    virtual void LevelShutdown() = 0;
    virtual void ClientActive(edict_t*) = 0;
    virtual void ClientDisconnect(edict_t*) = 0;
    virtual void ClientPutInServer(edict_t*, const char*) = 0;
    virtual void SetCommandClient(int) = 0;
    virtual void ClientSettingsChanged(edict_t*) = 0;
    virtual PLUGIN_RESULT ClientConnect(bool*, edict_t*, const char*, const char*, char*, int) = 0;
    virtual PLUGIN_RESULT ClientCommand(edict_t*, const CCommand&) = 0;
    virtual PLUGIN_RESULT NetworkIDValidated(const char*, const char*) = 0;
    virtual void OnQueryCvarValueFinished(QueryCvarCookie_t, edict_t*, EQueryCvarValueStatus, const char*, const char*) = 0;
};
