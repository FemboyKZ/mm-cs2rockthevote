#include "cs2rockthevote.h"
#include "common.h"
#include <cctype>
#include <stdio.h>

#include "admin/admin_bridge.h"
#include "config/config.h"
#include "lang/translations.h"
#include "maplist/map_lister.h"
#include "menu/chatmenu.h"
#include "menu/menu_bridge.h"
#include "nominate/nominate.h"
#include "player/player_manager.h"
#include "public/forwards.h"
#include "public/ics2rtv.h"
#include "rtv/rtv_manager.h"
#include "timers/timer_system.h"
#include "utils/http_client.h"
#include "utils/print_utils.h"
#include "vote/map_vote.h"
#include "whitelist/whitelist_bridge.h"
#include "workshop/workshop_validator.h"

#include <engine/igameeventsystem.h>
#include <iserver.h>
#include <networksystem/inetworkmessages.h>
#include <filesystem.h>
#include "steam/steam_gameserver.h"

#include "iclientcvarvalue.h"

// SourceHook declarations
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIActivated, SH_NOATTRIB, 0);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char *, uint64, const char *, const char *, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const *, int, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64,
				   const char *);
SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandRef, const CCommandContext &, const CCommand &);

// Global interface pointers (defined here, declared extern in common.h)
// g_pNetworkServerService, g_pFullFileSystem and g_pNetworkMessages are defined in interfaces.lib
IServerGameDLL *g_pServerGameDLL = nullptr;
IServerGameClients *g_pGameClients = nullptr;
IVEngineServer *g_pEngine = nullptr;
IGameEventManager2 *g_pGameEvents = nullptr;
ICvar *g_pICvar = nullptr;
IGameEventSystem *g_pGameEventSystem = nullptr;

// Optional. Provides each client's cl_language for phrase translation.
// May load after us, so it is re-acquired whenever translations reload.
static IClientCvarValue *g_pClientCvarValue = nullptr;

// Steam game-server API context used for workshop validation (ISteamUGC).
CSteamGameServerAPIContext g_RTVSteamAPI;

std::string RTV_SlotLanguage(int slot)
{
	const char *raw = g_pClientCvarValue ? g_pClientCvarValue->GetClientLanguage(CPlayerSlot(slot)) : nullptr;
	return g_RTVTranslations.MapClientLanguage(raw);
}

// Re-acquire ClientCvarValue and reload phrase tables.
// Called after each config load so phrases and the default language stay in sync with core.cfg.
static void RTV_LoadTranslations()
{
	g_pClientCvarValue = static_cast<IClientCvarValue *>(g_SMAPI->MetaFactory(CLIENTCVARVALUE_INTERFACE, nullptr, nullptr));
	g_RTVTranslations.Load(g_SMAPI->GetBaseDir());
	g_RTVTranslations.SetDefaultLanguage(g_RTVConfig.general.defaultLanguage);
}

// Plugin globals
CS2RTVPlugin g_ThisPlugin;

// Public forwards registry, queried by other plugins via CS2RTV_FORWARDS_INTERFACE.
CS2RTVForwards g_CS2RTVForwards;

PLUGIN_EXPOSE(CS2RTVPlugin, g_ThisPlugin);

// Public read-only status/maplist interface, queried via CS2RTV_INTERFACE.
class CS2RTVAPI : public ICS2RTV
{
	bool IsVoteActive() override
	{
		return g_MapVoteManager.IsVoteActive();
	}

	bool IsRTVVote() override
	{
		return g_MapVoteManager.IsRTVVote();
	}

	bool IsMapChangeScheduled() override
	{
		return g_MapVoteManager.IsChangeScheduled();
	}

	int GetRTVVoteCount() override
	{
		return g_RTVManager.GetVoteCount();
	}

	int GetRTVVotesNeeded() override
	{
		return g_RTVManager.GetVotesNeeded();
	}

	bool HasPlayerRockedVote(int slot) override
	{
		return g_RTVManager.HasVoted(slot);
	}

	int GetMapCount() override
	{
		return static_cast<int>(g_MapLister.GetMaps().size());
	}

	const char *GetMapName(int index) override
	{
		const auto &maps = g_MapLister.GetMaps();
		if (index < 0 || index >= static_cast<int>(maps.size()))
		{
			return "";
		}
		return maps[index].mapName.c_str();
	}

	const char *GetMapDisplayName(int index) override
	{
		const auto &maps = g_MapLister.GetMaps();
		if (index < 0 || index >= static_cast<int>(maps.size()))
		{
			return "";
		}
		return maps[index].displayName.c_str();
	}

	const char *GetMapWorkshopId(int index) override
	{
		const auto &maps = g_MapLister.GetMaps();
		if (index < 0 || index >= static_cast<int>(maps.size()))
		{
			return "";
		}
		return maps[index].workshopId.c_str();
	}

	const char *GetCurrentMap() override
	{
		return g_MapVoteManager.GetCurrentMap();
	}
};

static CS2RTVAPI g_CS2RTVAPI;

void *CS2RTVPlugin::OnMetamodQuery(const char *iface, int *ret)
{
	if (!strcmp(iface, CS2RTV_INTERFACE))
	{
		if (ret)
		{
			*ret = META_IFACE_OK;
		}
		return static_cast<ICS2RTV *>(&g_CS2RTVAPI);
	}
	if (!strcmp(iface, CS2RTV_FORWARDS_INTERFACE))
	{
		if (ret)
		{
			*ret = META_IFACE_OK;
		}
		return static_cast<ICS2RTVForwards *>(&g_CS2RTVForwards);
	}

	if (ret)
	{
		*ret = META_IFACE_FAILED;
	}
	return nullptr;
}

static void ShowMapChooserMenu(int slot)
{
	const auto &maps = g_MapLister.GetMaps();
	if (maps.empty())
	{
		RTV_PrintToChatT(slot, "No maps in the map list.");
		return;
	}

	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;

	ChatMenuDef def;
	def.title = RTV_Translate(slot, "Choose a map (immediate change)");
	def.exitButton = true;
	def.closeOnSelect = true;

	for (const auto &e : maps)
	{
		std::string display = g_MapLister.GetDisplayLabel(e);
		// Capture a value copy of the entry so we're not holding a pointer into
		// m_maps, which can reallocate (AddDynamicMap) or be cleared (Reload).
		MapEntry entryCopy = e;
		def.AddItem(display,
					[entryCopy](int /*playerSlot*/)
					{
						char cmd[256];
						if (entryCopy.isWorkshop && !entryCopy.workshopId.empty())
						{
							RTV_EnsureWorkshopMapReady(entryCopy.workshopId);
							snprintf(cmd, sizeof(cmd), "host_workshop_map %s\n", entryCopy.workshopId.c_str());
						}
						else
						{
							snprintf(cmd, sizeof(cmd), "changelevel %s\n", entryCopy.mapName.c_str());
						}
						g_pEngine->ServerCommand(cmd);
					});
	}

	g_RTVMenus.ShowMenu(slot, def, curtime);
}

CGlobalVars *GetGameGlobals()
{
	INetworkGameServer *pServer = g_pNetworkServerService->GetIGameServer();
	if (!pServer)
	{
		return nullptr;
	}
	return pServer->GetGlobals();
}

bool CS2RTVPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	RTV_HttpResetShutdownLatch();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pICvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pServerGameDLL, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, g_pGameClients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);

	g_SMAPI->AddListener(this, this);

	SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pServerGameDLL, SH_MEMBER(this, &CS2RTVPlugin::Hook_GameFrame), true);
	SH_ADD_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pServerGameDLL, SH_MEMBER(this, &CS2RTVPlugin::Hook_GameServerSteamAPIActivated),
				true);
	SH_ADD_HOOK(IServerGameClients, OnClientConnected, g_pGameClients, SH_MEMBER(this, &CS2RTVPlugin::Hook_OnClientConnected), false);
	SH_ADD_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients, SH_MEMBER(this, &CS2RTVPlugin::Hook_ClientPutInServer), true);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &CS2RTVPlugin::Hook_ClientDisconnect), true);
	SH_ADD_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &CS2RTVPlugin::Hook_DispatchConCommand), false);

	g_pCVar = g_pICvar;
	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL);

	META_CONPRINTF("[CS2RTV] Plugin loaded.\n");
	return true;
}

bool CS2RTVPlugin::Unload(char *error, size_t maxlen)
{
	RTV_DrainMainThread();

	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pServerGameDLL, SH_MEMBER(this, &CS2RTVPlugin::Hook_GameFrame), true);
	SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pServerGameDLL, SH_MEMBER(this, &CS2RTVPlugin::Hook_GameServerSteamAPIActivated),
				   true);
	SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, g_pGameClients, SH_MEMBER(this, &CS2RTVPlugin::Hook_OnClientConnected), false);
	SH_REMOVE_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients, SH_MEMBER(this, &CS2RTVPlugin::Hook_ClientPutInServer), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &CS2RTVPlugin::Hook_ClientDisconnect), true);
	SH_REMOVE_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &CS2RTVPlugin::Hook_DispatchConCommand), false);

	g_Timers.KillAll();
	RTV_HttpShutdown();

	RTV_ClearMainQueue();

	RTV_AdminBridge_Shutdown();
	RTV_WhitelistBridge_Shutdown();
	g_RTVMenus.Shutdown();
	g_RTVSteamAPI.Clear();

	g_CS2RTVForwards.Shutdown();

	return true;
}

void CS2RTVPlugin::AllPluginsLoaded()
{
	RTV_AdminBridge_Init();
	RTV_WhitelistBridge_Init();
	g_RTVMenus.Init();
}

void CS2RTVPlugin::OnPluginLoad(PluginId /*id*/)
{
	RTV_AdminBridge_Refresh();
	RTV_WhitelistBridge_Refresh();
	g_RTVMenus.Refresh();
}

void CS2RTVPlugin::OnPluginUnload(PluginId /*id*/)
{
	RTV_AdminBridge_Refresh();
	RTV_WhitelistBridge_Refresh();
	g_RTVMenus.Refresh();
}

// IMetamodListener: map load/unload
void CS2RTVPlugin::OnLevelInit(char const *pMapName, char const * /*pMapEntities*/, char const * /*pOldLevel*/, char const * /*pLandmarkName*/,
							   bool /*loadGame*/, bool /*background*/)
{
	char cfgPath[512];
	snprintf(cfgPath, sizeof(cfgPath), "%s/cfg/cs2rtv/core.cfg", g_SMAPI->GetBaseDir());
	RTV_LoadConfig(cfgPath, g_RTVConfig);
	RTV_LoadTranslations();

	char mapPath[512];
	snprintf(mapPath, sizeof(mapPath), "%s/cfg/maplist.txt", g_SMAPI->GetBaseDir());
	g_MapLister.LoadFromFile(mapPath);

	g_MapVoteManager.NotifyMapChangeSucceeded(); // cancel failure-detection timer
												 // before KillAll
	g_Timers.KillAll();
	g_RTVManager.OnMapStart(pMapName);
	g_MapVoteManager.OnMapStart(pMapName);
	g_NominateManager.OnMapStart(pMapName);
}

void CS2RTVPlugin::OnLevelShutdown()
{
	g_Timers.KillAll();
	g_MapVoteManager.Reset();
}

void CS2RTVPlugin::Hook_GameFrame(bool /*simulating*/, bool /*bFirstTick*/, bool /*bLastTick*/)
{
	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
	{
		return;
	}

	float curtime = globals->curtime;
	RTV_DrainMainThread();
	g_Timers.Process(curtime);
	g_RTVMenus.Tick(curtime);

	// Auto end-of-map vote: start the next-map vote before mp_timelimit expires
	// if nobody has triggered !rtv (no-op unless the setting is enabled).
	g_RTVManager.CheckEndOfMapVote(
		[]()
		{
			auto noms = g_NominateManager.GetNominations();
			g_MapVoteManager.StartVote(false, noms);
		});

	RETURN_META(MRES_IGNORED);
}

void CS2RTVPlugin::Hook_GameServerSteamAPIActivated()
{
	if (g_RTVSteamAPI.SteamUGC())
	{
		RETURN_META(MRES_IGNORED);
	}
	g_RTVSteamAPI.Init();
	RETURN_META(MRES_IGNORED);
}

void CS2RTVPlugin::Hook_OnClientConnected(CPlayerSlot slot, const char *pszName, uint64 xuid, const char * /*pszNetworkID*/,
										  const char * /*pszAddress*/, bool bFakePlayer)
{
	int s = slot.Get();
	g_RTVPlayerManager.OnClientConnected(s, pszName ? pszName : "", xuid, "", bFakePlayer);
	RETURN_META(MRES_IGNORED);
}

void CS2RTVPlugin::Hook_ClientPutInServer(CPlayerSlot slot, char const * /*pszName*/, int /*type*/, uint64 /*xuid*/)
{
	// type: 0=player, 1=bot
	int s = slot.Get();
	g_RTVPlayerManager.OnClientPutInServer(s);
	RETURN_META(MRES_IGNORED);
}

void CS2RTVPlugin::Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason /*reason*/, const char * /*pszName*/, uint64 /*xuid*/,
										 const char * /*pszNetworkID*/)
{
	int s = slot.Get();
	g_RTVManager.OnPlayerDisconnect(s);
	g_MapVoteManager.OnPlayerDisconnect(s);
	g_NominateManager.OnPlayerDisconnect(s);
	g_RTVMenus.OnPlayerDisconnect(s);
	g_RTVPlayerManager.OnClientDisconnect(s);
	RETURN_META(MRES_IGNORED);
}

void CS2RTVPlugin::Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args)
{
	const char *cmdName = args.Arg(0);
	if (!cmdName)
	{
		RETURN_META(MRES_IGNORED);
	}

	bool isSay = (strcmp(cmdName, "say") == 0 || strcmp(cmdName, "say_team") == 0);
	if (!isSay)
	{
		RETURN_META(MRES_IGNORED);
	}

	int slot = ctx.GetPlayerSlot().Get();
	if (slot < 0 || slot > MAXPLAYERS)
	{
		RETURN_META(MRES_IGNORED);
	}

	const char *rawMsg = args.ArgS(); // everything after the command name
	if (!rawMsg || !rawMsg[0])
	{
		RETURN_META(MRES_IGNORED);
	}

	// Strip outer quotes that CS2 wraps around say message
	char msg[512];
	strncpy(msg, rawMsg, sizeof(msg) - 1);
	msg[sizeof(msg) - 1] = '\0';

	size_t len = strlen(msg);
	if (len >= 2 && msg[0] == '"' && msg[len - 1] == '"')
	{
		memmove(msg, msg + 1, len - 2);
		msg[len - 2] = '\0';
		len -= 2;
	}

	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;

	if (g_RTVMenus.HasMenu(slot))
	{
		if (g_RTVMenus.ProcessInput(slot, msg, curtime))
		{
			RETURN_META(MRES_SUPERCEDE);
		}
	}

	// Second pass: chat commands
	if (len < 2)
	{
		RETURN_META(MRES_IGNORED);
	}

	// Each config string is treated as a set of prefix characters.
	// CommandPrefix       = chars that trigger command but leave message visible
	// SilentCommandPrefix = chars that trigger command and suppress the message
	// A char in both sets is treated as silent.
	const std::string &normalPfx = g_RTVConfig.general.commandPrefix;
	const std::string &silentPfx = g_RTVConfig.general.silentCommandPrefix;

	bool isSilent;
	if (!silentPfx.empty() && silentPfx.find(msg[0]) != std::string::npos)
	{
		isSilent = true;
	}
	else if (!normalPfx.empty() && normalPfx.find(msg[0]) != std::string::npos)
	{
		isSilent = false;
	}
	else
	{
		RETURN_META(MRES_IGNORED);
	}

	// Normal prefix: message stays visible in chat. Silent prefix: suppress it.
	const META_RES cmdReturn = isSilent ? MRES_SUPERCEDE : MRES_IGNORED;

	// Parse command + optional argument
	char cmdBuf[64];
	char argBuf[256] = "";

	const char *space = strchr(msg + 1, ' ');
	if (space)
	{
		size_t cmdLen = static_cast<size_t>(space - (msg + 1));
		if (cmdLen >= sizeof(cmdBuf))
		{
			cmdLen = sizeof(cmdBuf) - 1;
		}
		strncpy(cmdBuf, msg + 1, cmdLen);
		cmdBuf[cmdLen] = '\0';

		// Skip leading whitespace in arg
		const char *argStart = space + 1;
		while (*argStart == ' ')
		{
			argStart++;
		}
		strncpy(argBuf, argStart, sizeof(argBuf) - 1);
		argBuf[sizeof(argBuf) - 1] = '\0';
	}
	else
	{
		strncpy(cmdBuf, msg + 1, sizeof(cmdBuf) - 1);
		cmdBuf[sizeof(cmdBuf) - 1] = '\0';
	}

	for (char *p = cmdBuf; *p; p++)
	{
		*p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
	}

	if (strcmp(cmdBuf, "rtv") == 0)
	{
		if (!RTV_AdminBridge_CanUseCommand(slot, "rtv", 0))
		{
			RTV_PrintToChatT(slot, "You don't have permission to use this command.");
			RETURN_META(cmdReturn);
		}
		if (g_MapVoteManager.IsVoteActive())
		{
			g_MapVoteManager.ShowVoteMenuToPlayer(slot);
		}
		else
		{
			g_RTVManager.CommandHandler(slot,
										[]()
										{
											auto noms = g_NominateManager.GetNominations();
											g_MapVoteManager.StartVote(true, noms);
										});
		}
		RETURN_META(cmdReturn);
	}

	if (strcmp(cmdBuf, "nominate") == 0 || strcmp(cmdBuf, "nom") == 0)
	{
		if (g_RTVConfig.nominate.enabled)
		{
			g_NominateManager.CommandNominate(slot, argBuf);
		}
		else
		{
			RTV_PrintToChatT(slot, "Nominations are disabled.");
		}
		RETURN_META(cmdReturn);
	}

	if (strcmp(cmdBuf, "mapmenu") == 0 || strcmp(cmdBuf, "mm") == 0)
	{
		const std::string &permName = g_RTVConfig.mapchooser.permission;
		uint32_t flag = permName.empty() ? 0 : RTV_ParseFlagName(permName);
		if (!RTV_AdminBridge_CanUseCommand(slot, "mapmenu", flag))
		{
			RTV_PrintToChatT(slot, "You don't have permission to use this command.");
			RETURN_META(cmdReturn);
		}
		ShowMapChooserMenu(slot);
		RETURN_META(cmdReturn);
	}

	if (strcmp(cmdBuf, "listmaps") == 0)
	{
		if (!RTV_AdminBridge_CanUseCommand(slot, "listmaps", 0))
		{
			RTV_PrintToChatT(slot, "You don't have permission to use this command.");
			RETURN_META(cmdReturn);
		}
		g_NominateManager.CommandMaps(slot);
		RETURN_META(cmdReturn);
	}

	if (strcmp(cmdBuf, "reloadmaps") == 0)
	{
		if (!RTV_AdminBridge_CanUseCommand(slot, "reloadmaps", 0))
		{
			RTV_PrintToChatT(slot, "You don't have permission to use this command.");
			RETURN_META(cmdReturn);
		}
		g_NominateManager.CommandReloadMaps(slot);
		RETURN_META(cmdReturn);
	}

	if (strcmp(cmdBuf, "revote") == 0)
	{
		if (!RTV_AdminBridge_CanUseCommand(slot, "revote", 0))
		{
			RTV_PrintToChatT(slot, "You don't have permission to use this command.");
			RETURN_META(cmdReturn);
		}
		g_MapVoteManager.CommandRevote(slot);
		RETURN_META(cmdReturn);
	}

	if (strcmp(cmdBuf, "reloadrtv") == 0)
	{
		const std::string &permName = g_RTVConfig.general.adminPermission;
		uint32_t flag = permName.empty() ? 0 : RTV_ParseFlagName(permName);
		if (!RTV_AdminBridge_CanUseCommand(slot, "reloadrtv", flag))
		{
			RTV_PrintToChatT(slot, "You don't have permission to use this command.");
			RETURN_META(cmdReturn);
		}

		char cfgPath[512];
		snprintf(cfgPath, sizeof(cfgPath), "%s/cfg/cs2rtv/core.cfg", g_SMAPI->GetBaseDir());
		if (RTV_LoadConfig(cfgPath, g_RTVConfig))
		{
			RTV_LoadTranslations();
			RTV_PrintToChatT(slot, "RTV config reloaded.");
		}
		else
		{
			RTV_PrintToChatT(slot, "Failed to reload RTV config.");
		}
		RETURN_META(cmdReturn);
	}

	RETURN_META(MRES_IGNORED);
}

CON_COMMAND_F(mm_rtv, "Rock the vote for a map change", FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!RTV_AdminBridge_CanUseCommand(slot, "rtv", 0))
	{
		RTV_PrintToChatT(slot, "You don't have permission to use this command.");
		return;
	}
	if (g_MapVoteManager.IsVoteActive())
	{
		g_MapVoteManager.ShowVoteMenuToPlayer(slot);
	}
	else
	{
		g_RTVManager.CommandHandler(slot,
									[]()
									{
										auto noms = g_NominateManager.GetNominations();
										g_MapVoteManager.StartVote(true, noms);
									});
	}
}

CON_COMMAND_F(mm_nominate, "Nominate a map for the next vote", FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!g_RTVConfig.nominate.enabled)
	{
		RTV_PrintToChatT(slot, "Nominations are disabled.");
		return;
	}
	const char *arg = args.ArgC() > 1 ? args[1] : "";
	g_NominateManager.CommandNominate(slot, arg);
}

CON_COMMAND_F(mm_listmaps, "List available maps to your console", FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!RTV_AdminBridge_CanUseCommand(slot, "listmaps", 0))
	{
		RTV_PrintToChatT(slot, "You don't have permission to use this command.");
		return;
	}
	g_NominateManager.CommandMaps(slot);
}

CON_COMMAND_F(mm_reloadmaps, "Reload the map list from disk", FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!RTV_AdminBridge_CanUseCommand(slot, "reloadmaps", 0))
	{
		RTV_PrintToChatT(slot, "You don't have permission to use this command.");
		return;
	}
	g_NominateManager.CommandReloadMaps(slot);
}

CON_COMMAND_F(mm_revote, "Change your vote in an active map vote", FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!RTV_AdminBridge_CanUseCommand(slot, "revote", 0))
	{
		RTV_PrintToChatT(slot, "You don't have permission to use this command.");
		return;
	}
	g_MapVoteManager.CommandRevote(slot);
}

CON_COMMAND_F(mm_mapmenu, "Admin: open immediate map change menu", FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE)
{
	int slot = context.GetPlayerSlot().Get();
	const std::string &permName = g_RTVConfig.mapchooser.permission;
	uint32_t flag = permName.empty() ? 0 : RTV_ParseFlagName(permName);
	if (!RTV_AdminBridge_CanUseCommand(slot, "mapmenu", flag))
	{
		RTV_PrintToChatT(slot, "You don't have permission to use this command.");
		return;
	}
	ShowMapChooserMenu(slot);
}

CON_COMMAND_F(mm_reloadrtv, "Admin: reload cs2rtv config from disk", FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE)
{
	int slot = context.GetPlayerSlot().Get();
	const std::string &permName = g_RTVConfig.general.adminPermission;
	uint32_t flag = permName.empty() ? 0 : RTV_ParseFlagName(permName);
	if (!RTV_AdminBridge_CanUseCommand(slot, "reloadrtv", flag))
	{
		RTV_PrintToChatT(slot, "You don't have permission to use this command.");
		return;
	}
	char cfgPath[512];
	snprintf(cfgPath, sizeof(cfgPath), "%s/cfg/cs2rtv/core.cfg", g_SMAPI->GetBaseDir());
	if (RTV_LoadConfig(cfgPath, g_RTVConfig))
	{
		RTV_LoadTranslations();
		RTV_PrintToChatT(slot, "RTV config reloaded.");
	}
	else
	{
		RTV_PrintToChatT(slot, "Failed to reload RTV config.");
	}
}
