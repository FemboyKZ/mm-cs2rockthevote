#ifndef _INCLUDE_RTV_COMMON_H_
#define _INCLUDE_RTV_COMMON_H_

#include "mmu/chat_colors.h"
#include "mmu/plugin_globals.h"

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>
#include <sh_vector.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// Plugin-specific engine interfaces. Shared ones live in mmu/plugin_globals.h.
extern IGameEventManager2 *g_pGameEvents;
extern INetworkServerService *g_pNetworkServerService;

class IFileSystem;
extern IFileSystem *g_pFullFileSystem;

class INetworkMessages;
class IGameEventSystem;
extern INetworkMessages *g_pNetworkMessages;
extern IGameEventSystem *g_pGameEventSystem;

// CGlobalVars accessor, only valid during an active game
#include "mmu/print.h"

inline CGlobalVars *GetGameGlobals()
{
	return mmu::GetGameGlobals();
}

#endif // _INCLUDE_RTV_COMMON_H_
