#ifndef _INCLUDE_RTV_COMMON_H_
#define _INCLUDE_RTV_COMMON_H_

#include "mmu/chat_colors.h"

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>
#include <sh_vector.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define MAXPLAYERS 64

// Engine interface declarations
extern IServerGameDLL *g_pServerGameDLL;
extern IServerGameClients *g_pGameClients;
extern IVEngineServer *g_pEngine;
extern IGameEventManager2 *g_pGameEvents;
extern ICvar *g_pICvar;
extern INetworkServerService *g_pNetworkServerService;

class IFileSystem;
extern IFileSystem *g_pFullFileSystem;

class INetworkMessages;
class IGameEventSystem;
extern INetworkMessages *g_pNetworkMessages;
extern IGameEventSystem *g_pGameEventSystem;

// Metamod globals
extern ISmmAPI *g_SMAPI;
extern ISmmPlugin *g_PLAPI;
extern PluginId g_PLID;
extern SourceHook::ISourceHook *g_SHPtr;

// CGlobalVars accessor - only valid during an active game
CGlobalVars *GetGameGlobals();

#endif // _INCLUDE_RTV_COMMON_H_
