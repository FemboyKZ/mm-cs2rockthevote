#ifndef _INCLUDE_RTV_NOMINATE_H_
#define _INCLUDE_RTV_NOMINATE_H_

#include "src/common.h"
#include "src/maplist/map_lister.h"

#include <string>
#include <unordered_map>
#include <vector>

class NominateManager
{
public:
	// Called when a new map loads
	void OnMapStart(const char *currentMap);

	// Handle !nominate [mapname] from a player.
	// If mapname is empty, shows a map-selection ChatMenu.
	void CommandNominate(int slot, const char *arg);

	// Handle !maps command - list all maps to the player's console
	void CommandMaps(int slot) const;

	// Handle !reloadmaps command
	void CommandReloadMaps(int slot);

	// Get the ordered list of nominated map names for use in the vote builder.
	// Returns maps sorted by nomination count (highest first), then FIFO.
	std::vector<std::string> GetNominations() const;

	// Called when a player disconnects (removes their nominations)
	void OnPlayerDisconnect(int slot);

	// Reset nominations (e.g. map start)
	void Reset();

private:
	std::string m_currentMap;
	// slot -> list of nominated mapNames
	std::unordered_map<int, std::vector<std::string>> m_playerNoms;
	// mapName -> total nomination count
	std::unordered_map<std::string, int> m_nomCounts;
	// Bumped each map, so a lookup started on the previous one is recognisable.
	uint32_t m_mapSerial = 0;

	// True if the slot still holds the player who asked and the map has not changed since.
	bool CallerStillPresent(int slot, uint64_t steamid64, uint32_t mapSerial) const;

	void NominateMap(int slot, const MapEntry *entry);
	void ShowNominateMenu(int slot);
};

extern NominateManager g_NominateManager;

#endif // _INCLUDE_RTV_NOMINATE_H_
