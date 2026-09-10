#include "player_manager.h"
#include "src/config/config.h"

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

int RTVPlayerManager::GetEligiblePlayerCount() const
{
	const bool includeSpec = g_RTVConfig.general.includeSpectator;
	return m_players.Count(
		[includeSpec](const PlayerInfo &p)
		{
			if (!p.connected || !p.inGame || p.fakePlayer)
			{
				return false;
			}
			return includeSpec || p.teamNum != 1;
		});
}
