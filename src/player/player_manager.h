#ifndef _INCLUDE_RTV_PLAYER_MANAGER_H_
#define _INCLUDE_RTV_PLAYER_MANAGER_H_

#include "src/common.h"
#include "game/players.h"
#include <string>

struct PlayerInfo : mmu::Player
{
	int teamNum = 0; // 1=spec, 2=T, 3=CT (refreshed from the controller in GetEligiblePlayerCount)
};

class RTVPlayerManager : public mmu::Players<PlayerInfo>
{
public:
	// Name for announcements. Never null.
	const char *DisplayName(int slot);

	// Number of connected, non-fake human players
	int GetHumanPlayerCount() const;

	// Number of connected non-fake players eligible to vote
	// (spec excluded if general.includeSpectator = false).
	// Refreshes each counted player's teamNum from their controller.
	int GetEligiblePlayerCount();
	bool IsEligible(int slot);
};

extern RTVPlayerManager g_RTVPlayerManager;

#endif // _INCLUDE_RTV_PLAYER_MANAGER_H_
