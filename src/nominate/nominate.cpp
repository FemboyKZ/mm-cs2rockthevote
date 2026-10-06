#include "nominate.h"
#include "utils/log.h"
#include "src/admin/admin_bridge.h"
#include "src/config/config.h"
#include "src/lang/translations.h"
#include "src/menu/map_menu.h"
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
	m_mapSerial++;
}

bool NominateManager::CallerStillPresent(int slot, uint64_t steamid64, uint32_t mapSerial) const
{
	if (mapSerial != m_mapSerial)
	{
		return false;
	}
	const PlayerInfo *pi = g_RTVPlayerManager.GetPlayer(slot);
	return pi && pi->connected && pi->steamid64 == steamid64;
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
	uint32_t nomFlag = nomPerm.empty() ? 0 : ParseAdminFlagName(nomPerm);
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

	// An API lookup outlives the request: the slot can empty, be reused, or be on the next map by the time it lands.
	PlayerInfo *caller = g_RTVPlayerManager.GetPlayer(slot);
	uint64_t callerId = caller ? caller->steamid64 : 0;
	uint32_t callerSerial = m_mapSerial;

	if (LooksLikeWorkshopId(arg))
	{
		const std::string &extPerm = g_RTVConfig.nominate.externalNominatePermission;
		uint32_t extFlag = extPerm.empty() ? 0 : ParseAdminFlagName(extPerm);
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
											[this, slot, callerId, callerSerial, wsId](MapEntry e)
											{
												if (!CallerStillPresent(slot, callerId, callerSerial))
												{
													return;
												}
												if (e.mapName.empty())
												{
													MMU_LOG_WARN("Workshop lookup failed for ID %s\n", wsId.c_str());
													RTV_PrintToChatT(slot, "Workshop map %s not found.", wsId.c_str());
													return;
												}
												const MapEntry *added = g_MapLister.AddDynamicMap(e);
												if (added)
												{
													MMU_LOG_INFO("Workshop map '%s' added dynamically from API.\n", added->mapName.c_str());
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
		uint32_t extFlag = extPerm.empty() ? 0 : ParseAdminFlagName(extPerm);
		if (!RTV_AdminBridge_CanUseCommand(slot, "nominate_ext", extFlag))
		{
			RTV_PrintToChatT(slot, "Map %s not found in map list.", arg);
			return;
		}

		std::string query(arg);
		RTV_PrintToChatT(slot, "Looking up map %s via API...", query.c_str());
		g_MapLister.LookupByNameAsync(query,
									  [this, slot, callerId, callerSerial, query](MapEntry e)
									  {
										  if (!CallerStillPresent(slot, callerId, callerSerial))
										  {
											  return;
										  }
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
		std::vector<std::string> names;
		for (auto *m : matches)
		{
			names.push_back(m->mapName);
		}
		ShowMapMenu(slot, "Matching maps", std::move(names));
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
	std::vector<const MapEntry *> sorted;
	sorted.reserve(maps.size());
	for (const auto &e : maps)
	{
		sorted.push_back(&e);
	}
	SortMapsByName(sorted);
	for (const MapEntry *e : sorted)
	{
		RTV_PrintToClient(slot, "  %s", g_MapLister.GetDisplayLabel(*e, false).c_str());
	}
}

void NominateManager::CommandReloadMaps(int slot)
{
	if (g_MapLister.UsesApiPool())
	{
		RTV_PrintToChatT(slot, "Refreshing the map pool from the CS2KZ API...");

		PlayerInfo *caller = g_RTVPlayerManager.GetPlayer(slot);
		uint64_t callerId = caller ? caller->steamid64 : 0;
		uint32_t callerSerial = m_mapSerial;

		g_MapLister.RefreshAsync(
			[this, slot, callerId, callerSerial](int count)
			{
				// The server console has no slot to go stale.
				if (slot >= 0 && !CallerStillPresent(slot, callerId, callerSerial))
				{
					return;
				}
				if (count < 0)
				{
					RTV_PrintToChatT(slot, "Failed to refresh the map pool.");
				}
				else
				{
					RTV_PrintToChatT(slot, "Map pool refreshed. (%d maps)", count);
				}
			});
		return;
	}

	if (g_MapVoteManager.IsVoteActive() || g_MapVoteManager.IsChangeScheduled())
	{
		g_MapVoteManager.CancelVote();
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
	ShowMapMenu(slot, "Nominate a map", {});
}

void NominateManager::ShowMapMenu(int slot, const char *title, std::vector<std::string> only)
{
	MapMenu menu;
	menu.title = title;
	menu.only = std::move(only);
	menu.mark = [this](int playerSlot, const MapEntry &e, bool &disabled)
	{
		std::string mark;
		if (m_nomCounts.count(e.mapName) > 0)
		{
			mark += " " + RTV_Translate(playerSlot, "[nominated]");
		}
		disabled = (e.mapName == m_currentMap);
		if (disabled)
		{
			mark += " " + RTV_Translate(playerSlot, "[current]");
		}
		return mark;
	};
	// By name, a dynamic add or reload while the menu is open leaves the row's copy behind.
	menu.onPick = [this](int playerSlot, const MapEntry &e)
	{
		const MapEntry *entry = g_MapLister.FindExact(e.mapName);
		if (entry)
		{
			NominateMap(playerSlot, entry);
		}
	};
	RTV_ShowMapMenu(slot, menu);
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

	const char *pName = g_RTVPlayerManager.DisplayName(slot);
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
