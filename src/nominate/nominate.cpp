#include "nominate.h"
#include "src/admin/admin_bridge.h"
#include "src/config/config.h"
#include "src/menu/chatmenu.h"
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
		RTV_PrintToChat(slot, "\x07Nominations are currently disabled.");
		return;
	}

	const std::string &nomPerm = g_RTVConfig.nominate.permission;
	if (!nomPerm.empty())
	{
		uint32_t flag = RTV_ParseFlagName(nomPerm);
		if (!RTV_AdminBridge_HasFlag(slot, flag))
		{
			RTV_PrintToChat(slot, "\x07You don't have permission to nominate.");
			return;
		}
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
		if (!extPerm.empty())
		{
			uint32_t flag = RTV_ParseFlagName(extPerm);
			if (!RTV_AdminBridge_HasFlag(slot, flag))
			{
				RTV_PrintToChat(slot, "\x07You don't have permission to nominate workshop maps by ID.");
				return;
			}
		}

		const MapEntry *existing = g_MapLister.FindByWorkshopId(arg);
		if (existing)
		{
			NominateMap(slot, existing);
			return;
		}

		std::string wsId(arg);
		RTV_PrintToChat(slot, "\x01Looking up workshop map \x04%s\x01...", wsId.c_str());
		g_MapLister.LookupByWorkshopIdAsync(wsId,
											[this, slot, wsId](MapEntry e)
											{
												if (e.mapName.empty())
												{
													META_CONPRINTF("[CS2RTV] Workshop lookup failed for ID %s\n", wsId.c_str());
													RTV_PrintToChat(slot, "\x07Workshop map \x04%s\x07 not found.", wsId.c_str());
													return;
												}
												const MapEntry *added = g_MapLister.AddDynamicMap(e);
												if (added)
												{
													META_CONPRINTF("[CS2RTV] Workshop map '%s' added dynamically from API.\n",
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
		if (!extPerm.empty())
		{
			uint32_t flag = RTV_ParseFlagName(extPerm);
			if (!RTV_AdminBridge_HasFlag(slot, flag))
			{
				RTV_PrintToChat(slot, "\x07Map \x04%s\x07 not found in map list.", arg);
				return;
			}
		}

		std::string query(arg);
		RTV_PrintToChat(slot, "\x01Looking up map \x04%s\x01 via API...", query.c_str());
		g_MapLister.LookupByNameAsync(query,
									  [this, slot, query](MapEntry e)
									  {
										  if (!e.mapName.empty())
										  {
											  const MapEntry *added = g_MapLister.AddDynamicMap(e);
											  META_CONPRINTF("[CS2RTV] Map '%s' added dynamically from CS2KZ API.\n", e.mapName.c_str());
											  if (added)
											  {
												  NominateMap(slot, added);
											  }
										  }
										  else
										  {
											  META_CONPRINTF("[CS2RTV] API lookup for '%s' returned no results.\n", query.c_str());
											  RTV_PrintToChat(slot, "\x07Map \x04%s\x07 not found.", query.c_str());
										  }
									  });
		return;
	}

	if (!entry && matches.size() > 1)
	{
		CGlobalVars *globals = GetGameGlobals();
		float curtime = globals ? globals->curtime : 0.0f;

		ChatMenuDef def;
		def.title = "Matching maps";
		def.exitButton = true;
		def.closeOnSelect = true;

		for (auto *m : matches)
		{
			def.AddItem(g_MapLister.GetDisplayLabel(*m), [this, m](int playerSlot) { NominateMap(playerSlot, m); });
		}
		g_ChatMenus.ShowMenu(slot, def, curtime);
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
		RTV_PrintToClient(slot, "  %s", g_MapLister.GetDisplayLabel(e).c_str());
	}
}

void NominateManager::CommandReloadMaps(int slot)
{
	if (g_MapVoteManager.IsVoteActive() || g_MapVoteManager.IsChangeScheduled())
	{
		g_MapVoteManager.Reset();
		RTV_ChatToAll("\x07Map list reloaded by admin - active vote cancelled.");
	}

	int count = g_MapLister.Reload();
	if (count < 0)
	{
		RTV_PrintToChat(slot, "\x07"
							  "Failed to reload map list.");
	}
	else
	{
		RTV_PrintToChat(slot, "\x04Map list reloaded. \x01(%d maps)", count);
	}
}

void NominateManager::ShowNominateMenu(int slot)
{
	const auto &maps = g_MapLister.GetMaps();
	if (maps.empty())
	{
		RTV_PrintToChat(slot, "\x07No maps in the map list.");
		return;
	}

	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;

	ChatMenuDef def;
	def.title = "Nominate a map";
	def.exitButton = true;
	def.closeOnSelect = true;

	for (const auto &e : maps)
	{
		bool disabled = (e.mapName == m_currentMap);

		bool alreadyNom = m_nomCounts.count(e.mapName) > 0;
		std::string label = g_MapLister.GetDisplayLabel(e);
		if (alreadyNom)
		{
			label += " [nominated]";
		}
		if (disabled)
		{
			label += " [current]";
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

	g_ChatMenus.ShowMenu(slot, def, curtime);
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
		RTV_PrintToChat(slot, "\x07You cannot nominate the current map.");
		return;
	}

	int limit = g_RTVConfig.nominate.nominateLimit;
	auto &playerList = m_playerNoms[slot];

	for (const auto &n : playerList)
	{
		if (n == mapName)
		{
			RTV_PrintToChat(slot, "\x07You already nominated \x04%s\x07.", display.c_str());
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
	RTV_ChatToAll("\x04%s\x01 nominated \x04%s\x01 for the next map.", pName, display.c_str());
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
