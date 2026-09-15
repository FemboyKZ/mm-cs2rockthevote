#include "player_manager.h"
#include "src/config/config.h"
#include "mmu/entity/ccsplayercontroller.h"

RTVPlayerManager g_RTVPlayerManager;

void RTVPlayerManager::OnClientConnected(int slot, const char *name, uint64_t xuid, const char *address, bool fakePlayer)
{
	PlayerInfo *p = m_players.Get(slot);
	if (!p)
	{
		return;
	}

	p->Reset();
	p->connected = true;
	p->steamid64 = xuid;
	p->name = name ? name : "";
	p->fakePlayer = fakePlayer;
}

void RTVPlayerManager::OnClientDisconnect(int slot)
{
	if (PlayerInfo *p = m_players.Get(slot))
	{
		p->Reset();
	}
}

void RTVPlayerManager::OnClientPutInServer(int slot)
{
	if (PlayerInfo *p = m_players.Get(slot))
	{
		p->inGame = true;
	}
}

PlayerInfo *RTVPlayerManager::GetPlayer(int slot)
{
	return m_players.Get(slot);
}

int RTVPlayerManager::GetHumanPlayerCount() const
{
	return m_players.Count([](const PlayerInfo &p) { return p.connected && p.inGame && !p.fakePlayer; });
}

int RTVPlayerManager::GetEligiblePlayerCount()
{
	const bool includeSpec = g_RTVConfig.general.includeSpectator;
	int count = 0;

	for (int slot = 0; slot <= MAXPLAYERS; slot++)
	{
		PlayerInfo *p = m_players.Get(slot);
		if (!p->connected || !p->inGame || p->fakePlayer)
		{
			continue;
		}

		// Read live rather than tracking team changes, the controller is the only source that cannot go stale.
		if (CCSPlayerController *controller = CCSPlayerController::FromSlot(slot))
		{
			p->teamNum = controller->m_iTeamNum();
		}

		if (!includeSpec && p->teamNum == CS_TEAM_SPECTATOR)
		{
			continue;
		}
		count++;
	}

	return count;
}
