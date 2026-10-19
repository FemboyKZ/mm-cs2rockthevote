#include "player_manager.h"
#include "src/config/config.h"
#include "sdk/entity/ccsplayercontroller.h"

RTVPlayerManager g_RTVPlayerManager;

const char *RTVPlayerManager::DisplayName(int slot)
{
	// Live from the controller, the stored connect-time name misses a rename.
	CCSPlayerController *controller = CCSPlayerController::FromSlot(slot);
	const char *live = controller ? controller->GetPlayerName() : nullptr;
	if (live && *live)
	{
		return live;
	}
	PlayerInfo *p = Get(slot);
	return (p && !p->name.empty()) ? p->name.c_str() : "Unknown";
}

int RTVPlayerManager::GetHumanPlayerCount() const
{
	return Count([](const PlayerInfo &p) { return p.inGame && !p.fakePlayer; });
}

bool RTVPlayerManager::IsEligible(int slot)
{
	PlayerInfo *p = Get(slot);
	if (!p || !p->connected || !p->inGame || p->fakePlayer)
	{
		return false;
	}

	// Read live rather than tracking team changes, the controller is the only source that cannot go stale.
	if (CCSPlayerController *controller = CCSPlayerController::FromSlot(slot))
	{
		p->teamNum = controller->m_iTeamNum();
	}

	return g_RTVConfig.general.includeSpectator || p->teamNum != CS_TEAM_SPECTATOR;
}

int RTVPlayerManager::GetEligiblePlayerCount()
{
	int count = 0;
	for (int slot = 0; slot < MAXPLAYERS; slot++)
	{
		count += IsEligible(slot) ? 1 : 0;
	}
	return count;
}
