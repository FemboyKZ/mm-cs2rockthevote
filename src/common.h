#ifndef _INCLUDE_RTV_COMMON_H_
#define _INCLUDE_RTV_COMMON_H_

#include "interfaces/cs2admin/admin_access.h"
#include "utils/chat_colors.h"
#include "sdk/plugin_globals.h"

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <engine2/inetworkserverservice.h>
#include <engine2/networkgameserverbase.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// Plugin-specific engine interfaces. Shared ones live in sdk/plugin_globals.h.
extern INetworkServerService *g_pNetworkServerService;

class IFileSystem;
extern IFileSystem *g_pFullFileSystem;

class INetworkMessages;
class IGameEventSystem;
extern INetworkMessages *g_pNetworkMessages;
extern IGameEventSystem *g_pGameEventSystem;

// g_pSchemaSystem and g_pGameResourceServiceServer come from the SDK's interfaces.lib.
class CGameEntitySystem;
extern CGameEntitySystem *g_pEntitySystem;

extern mmu::AdminAccess g_RTVAdmin;

// CGlobalVars accessor, only valid during an active game
#include "game/print.h"

#endif // _INCLUDE_RTV_COMMON_H_
