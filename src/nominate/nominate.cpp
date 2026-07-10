#include "nominate.h"
#include "mmu/log.h"
#include "src/admin/admin_bridge.h"
#include "src/config/config.h"
#include "src/lang/translations.h"
#include "src/menu/chatmenu.h"
#include "src/menu/menu_bridge.h"
#include "src/player/player_manager.h"
#include "src/utils/print_utils.h"
#include "src/vote/map_vote.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

NominateManager g_NominateManager;

static bool LooksLikeWorkshopId(const char *s)
{
	if (!s || !*s)
	{
		return false;
	}
	int len = 0;
	while (s[len])
	{
		if (!isdigit((unsigned char)s[len]))
		{
			return false;
		}
		len++;
	}
	return len >= 6;
}

void NominateManager::OnMapStart(const char *currentMap)
{
	Reset();
	m_currentMap = currentMap ? currentMap : "";
}

void NominateManager::Reset()
{
	m_playerNoms.clear();
	m_nomCounts.clear();
}

void NominateManager::OnPlayerDisconnect(int slot)
{
	auto it = m_playerNoms.find(slot);
	if (it == m_playerNoms.end())
	{
		return;
	}

	for (const auto &mapName : it->second)
	{
		auto cnt = m_nomCounts.find(mapName);
		if (cnt != m_nomCounts.end())
		{
			cnt->second--;
			if (cnt->second <= 0)
			{
				m_nomCounts.erase(cnt);
			}
		}
	}
	m_playerNoms.erase(it);
}

void NominateManager::CommandNominate(int slot, const char *arg)
{
	if (!g_RTVConfig.nominate.enabled)
	{
		RTV_PrintToChatT(slot, "Nominations are currently disabled.");
		return;
	}

	// flag 0 = open by default.
	const std::string &nomPerm = g_RTVConfig.nominate.permission;
	uint32_t nomFlag = nomPerm.empty() ? 0 : RTV_ParseFlagName(nomPerm);
	if (!RTV_AdminBridge_CanUseCommand(slot, "nominate", nomFlag))
	{
		RTV_PrintToChatT(slot, "You don't have permission to nominate.");
		return;
	}

	if (!arg || arg[0] == '\0')
	{
		ShowNominateMenu(slot);
		return;
	}

	// Strip leading and trailing whitespace
	const char *trimmed = arg;
	while (*trimmed == ' ' || *trimmed == '\t')
	{
		trimmed++;
	}
	std::string argStr(trimmed);
	while (!argStr.empty() && (argStr.back() == ' ' || argStr.back() == '\t'))
	{
		argStr.pop_back();
	}
	arg = argStr.c_str();

	if (!*arg)
	{
		ShowNominateMenu(slot);
		return;
	}

	if (LooksLikeWorkshopId(arg))
	{
		const std::string &extPerm = g_RTVConfig.nominate.externalNominatePermission;
		uint32_t extFlag = extPerm.empty() ? 0 : RTV_ParseFlagName(extPerm);
		if (!RTV_AdminBridge_CanUseCommand(slot, "nominate_ext", extFlag))
		{
			RTV_PrintToChatT(slot, "You don't have permission to nominate workshop maps by ID.");
			return;
		}

		const MapEntry *existing = g_MapLister.FindByWorkshopId(arg);
		if (existing)
		{
			NominateMap(slot, existing);
			return;
		}

		std::string wsId(arg);
		RTV_PrintToChatT(slot, "Looking up workshop map %s...", wsId.c_str());
		g_MapLister.LookupByWorkshopIdAsync(wsId,
											[this, slot, wsId](MapEntry e)
											{
												if (e.mapName.empty())
												{
													MMU_LOG_WARN("Workshop lookup failed for ID %s\n", wsId.c_str());
													RTV_PrintToChatT(slot, "Workshop map %s not found.", wsId.c_str());
													return;
												}
												const MapEntry *added = g_MapLister.AddDynamicMap(e);
												if (added)
												{
													MMU_LOG_INFO("Workshop map '%s' added dynamically from API.\n",
																   added->mapName.c_str());
													NominateMap(slot, added);
												}
											});
		return;
	}

	std::vector<const MapEntry *> matches;
	const MapEntry *entry = g_MapLister.Resolve(arg, &matches);

	if (!entry && matches.empty())
	{
		const std::string &extPerm = g_RTVConfig.nominate.externalNominatePermission;
		uint32_t extFlag = extPerm.empty() ? 0 : RTV_ParseFlagName(extPerm);
		if (!RTV_AdminBridge_CanUseCommand(slot, "nominate_ext", extFlag))
		{
			RTV_PrintToChatT(slot, "Map %s not found in map list.", arg);
			return;
		}

		std::string query(arg);
		RTV_PrintToChatT(slot, "Looking up map %s via API...", query.c_str());
		g_MapLister.LookupByNameAsync(query,
									  [this, slot, query](MapEntry e)
									  {
										  if (!e.mapName.empty())
										  {
											  const MapEntry *added = g_MapLister.AddDynamicMap(e);
											  MMU_LOG_INFO("Map '%s' added dynamically from CS2KZ API.\n", e.mapName.c_str());
											  if (added)
											  {
												  NominateMap(slot, added);
											  }
										  }
										  else
										  {
											  MMU_LOG_INFO("API lookup for '%s' returned no results.\n", query.c_str());
											  RTV_PrintToChatT(slot, "Map %s not found.", query.c_str());
										  }
									  });
		return;
	}

	if (!entry && matches.size() > 1)
	{
		CGlobalVars *globals = GetGameGlobals();
		float curtime = globals ? globals->curtime : 0.0f;

		ChatMenuDef def;
		def.title = RTV_Translate(slot, "Matching maps");
		def.exitButton = true;
		def.closeOnSelect = true;

		for (auto *m : matches)
		{
			def.AddItem(g_MapLister.GetDisplayLabel(*m), [this, m](int playerSlot) { NominateMap(playerSlot, m); });
		}
		g_RTVMenus.ShowMenu(slot, def, curtime);
		return;
	}

	NominateMap(slot, entry ? entry : matches[0]);
}

void NominateManager::CommandMaps(int slot) const
{
	const auto &maps = g_MapLister.GetMaps();
	if (maps.empty())
	{
		RTV_PrintToClient(slot, "No maps loaded.");
		return;
	}
	RTV_PrintToClient(slot, "Available maps (%d):", static_cast<int>(maps.size()));
	for (const auto &e : maps)
	{
		RTV_PrintToClient(slot, "  %s", g_MapLister.GetDisplayLabel(e, false).c_str());
	}
}

void NominateManager::CommandReloadMaps(int slot)
{
	if (g_MapVoteManager.IsVoteActive() || g_MapVoteManager.IsChangeScheduled())
	{
		g_MapVoteManager.Reset();
		RTV_ChatToAllT("Map list reloaded by admin - active vote cancelled.");
	}

	int count = g_MapLister.Reload();
	if (count < 0)
	{
		RTV_PrintToChatT(slot, "Failed to reload map list.");
	}
	else
	{
		RTV_PrintToChatT(slot, "Map list reloaded. (%d maps)", count);
	}
}

void NominateManager::ShowNominateMenu(int slot)
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
	def.title = RTV_Translate(slot, "Nominate a map");
	def.exitButton = true;
	def.closeOnSelect = true;

	for (const auto &e : maps)
	{
		bool disabled = (e.mapName == m_currentMap);

		bool alreadyNom = m_nomCounts.count(e.mapName) > 0;
		// Disabled rows render grey (\x08); keep trailing text grey after the tier.
		std::string label = g_MapLister.GetDisplayLabel(e, true, disabled ? "\x08" : "\x01");
		if (alreadyNom)
		{
			label += " " + RTV_Translate(slot, "[nominated]");
		}
		if (disabled)
		{
			label += " " + RTV_Translate(slot, "[current]");
		}

		std::string capturedMapName = e.mapName;
		def.AddItem(
			label,
			[this, capturedMapName](int playerSlot)
			{
				const MapEntry *entry = g_MapLister.FindExact(capturedMapName);
				if (entry)
				{
					NominateMap(playerSlot, entry);
				}
			},
			disabled);
	}

	g_RTVMenus.ShowMenu(slot, def, curtime);
}

void NominateManager::NominateMap(int slot, const MapEntry *entry)
{
	if (!entry)
	{
		return;
	}

	const std::string &mapName = entry->mapName;
	std::string display = g_MapLister.GetDisplayLabel(*entry);

	if (mapName == m_currentMap)
	{
		RTV_PrintToChatT(slot, "You cannot nominate the current map.");
		return;
	}

	int limit = g_RTVConfig.nominate.nominateLimit;
	auto &playerList = m_playerNoms[slot];

	for (const auto &n : playerList)
	{
		if (n == mapName)
		{
			RTV_PrintToChatT(slot, "You already nominated %s.", display.c_str());
			return;
		}
	}

	if (limit > 0 && static_cast<int>(playerList.size()) >= limit)
	{
		// Remove oldest nomination to make room
		const std::string &oldest = playerList.front();
		auto cnt = m_nomCounts.find(oldest);
		if (cnt != m_nomCounts.end())
		{
			cnt->second--;
			if (cnt->second <= 0)
			{
				m_nomCounts.erase(cnt);
			}
		}
		playerList.erase(playerList.begin());
	}

	playerList.push_back(mapName);
	m_nomCounts[mapName]++;

	PlayerInfo *pi = g_RTVPlayerManager.GetPlayer(slot);
	const char *pName = pi ? pi->name.c_str() : "Unknown";
	RTV_ChatToAllT("%s nominated %s for the next map.", pName, display.c_str());
}

std::vector<std::string> NominateManager::GetNominations() const
{
	std::vector<std::pair<int, std::string>> ranked;
	for (const auto &kv : m_nomCounts)
	{
		ranked.push_back({kv.second, kv.first});
	}

	std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

	std::vector<std::string> result;
	for (const auto &p : ranked)
	{
		result.push_back(p.second);
	}

	return result;
}
