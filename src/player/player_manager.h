#ifndef _INCLUDE_RTV_PLAYER_MANAGER_H_
#define _INCLUDE_RTV_PLAYER_MANAGER_H_

#include "src/common.h"
#include "mmu/player_table.h"
#include <string>

struct PlayerInfo
{
	bool connected = false;
	bool inGame = false; // true once ClientPutInServer fires (excludes challenging/half-connected slots)
	uint64_t steamid64 = 0;
	std::string name;
	bool fakePlayer = false;
	int teamNum = 0; // 1=spec, 2=T, 3=CT (updated on ClientCommand/GameEvents)

	void Reset()
	{
		connected = false;
		inGame = false;
		steamid64 = 0;
		name.clear();
		fakePlayer = false;
		teamNum = 0;
	}
};

class RTVPlayerManager
{
public:
	void OnClientConnected(int slot, const char *name, uint64_t xuid, const char *address, bool fakePlayer);
	void OnClientPutInServer(int slot);
	void OnClientDisconnect(int slot);

	PlayerInfo *GetPlayer(int slot);

	// Number of connected, non-fake human players
	int GetHumanPlayerCount() const;

	// Number of connected non-fake players eligible to vote
	// (spec excluded if general.includeSpectator = false)
	int GetEligiblePlayerCount() const;

private:
	mmu::PlayerTable<PlayerInfo> m_players;
};

extern RTVPlayerManager g_RTVPlayerManager;

#endif // _INCLUDE_RTV_PLAYER_MANAGER_H_
