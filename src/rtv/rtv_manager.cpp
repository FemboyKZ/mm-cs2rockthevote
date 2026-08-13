#include "rtv_manager.h"
#include "src/config/config.h"
#include "src/player/player_manager.h"
#include "src/timelimit/timelimit.h"
#include "src/timers/timer_system.h"
#include "src/utils/print_utils.h"
#include "src/whitelist/whitelist_bridge.h"

#include <algorithm>
#include <cmath>

RTVManager g_RTVManager;

int RTVManager::RequiredVotes(int eligibleCount) const
{
	int pct = g_RTVConfig.rtv.votePercentage;
	return static_cast<int>(std::ceil(eligibleCount * (pct / 100.0)));
}

void RTVManager::OnMapStart(const char * /*mapName*/)
{
	m_votes.clear();
	m_voteStarted = false;
	m_mapChangeScheduled = false;
	m_cooldownExpireTime = 0.0f;
	m_endOfMapVoteTriggered = false;
	m_nextEomCheckTime = 0.0f;
	StopReminderTimer();

	CGlobalVars *globals = GetGameGlobals();
	m_mapStartTime = globals ? globals->curtime : 0.0f;

	g_RTVTimeLimit.OnMapStart(m_mapStartTime);
}

void RTVManager::OnPlayerDisconnect(int slot)
{
	m_votes.erase(slot);
}

void RTVManager::OnVoteStarted()
{
	m_voteStarted = true;
	StopReminderTimer();
}

void RTVManager::OnVoteEndedNoVotes()
{
	m_votes.clear();
	m_voteStarted = false;
	m_mapChangeScheduled = false;

	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;
	m_cooldownExpireTime = curtime + static_cast<float>(g_RTVConfig.rtv.cooldownDuration);
}

void RTVManager::OnMapChangeScheduled()
{
	m_mapChangeScheduled = true;
	StopReminderTimer();
}

void RTVManager::OnMapExtended()
{
	float timeLeft = 0.0f;
	if (g_RTVTimeLimit.GetTimeLeftSeconds(timeLeft) && timeLeft > static_cast<float>(g_RTVConfig.endOfMapVote.triggerTime))
	{
		m_endOfMapVoteTriggered = false;
	}
	m_nextEomCheckTime = 0.0f;
}

bool RTVManager::IsThresholdReached(int eligibleCount) const
{
	return GetVoteCount() >= RequiredVotes(eligibleCount);
}

int RTVManager::GetVotesNeeded() const
{
	int eligible = (std::max)(g_RTVPlayerManager.GetEligiblePlayerCount(), 1);
	return RequiredVotes(eligible);
}

void RTVManager::CommandHandler(int slot, StartVoteCallback startVote)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}

	const RtvCfg &cfg = g_RTVConfig.rtv;

	if (!cfg.enabled)
	{
		RTV_PrintToChatT(slot, "RTV is currently disabled.");
		return;
	}

	// Whitelist gate: when mm-cs2whitelist is loaded, only whitelisted players
	// may trigger RTV.  This prevents non-whitelisted joiners from spamming
	// !rtv during the brief window before the whitelist kick fires.
	if (RTV_WhitelistBridge_Available() && !RTV_WhitelistBridge_IsPlayerAllowed(slot))
	{
		RTV_PrintToChatT(slot, "You must be whitelisted to use RTV.");
		return;
	}

	if (m_mapChangeScheduled)
	{
		RTV_PrintToChatT(slot, "A map change is already scheduled.");
		return;
	}

	if (m_voteStarted)
	{
		// If a vote is already running, the caller (plugin main) will re-open the
		// vote menu for this player instead.
		return;
	}

	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;

	if (cfg.mapStartDelay > 0 && (curtime - m_mapStartTime) < cfg.mapStartDelay)
	{
		int secs = static_cast<int>(cfg.mapStartDelay - (curtime - m_mapStartTime));
		RTV_PrintToChatT(slot, "RTV is not available yet. Wait %d more second(s).", secs);
		return;
	}

	if (m_cooldownExpireTime > 0.0f && curtime < m_cooldownExpireTime)
	{
		int secs = static_cast<int>(m_cooldownExpireTime - curtime);
		RTV_PrintToChatT(slot, "RTV is on cooldown. Wait %d more second(s).", secs);
		return;
	}

	int eligible = (std::max)(g_RTVPlayerManager.GetEligiblePlayerCount(), 1);
	int required = RequiredVotes(eligible);

	if (HasVoted(slot))
	{
		int count = GetVoteCount();
		RTV_PrintToChatT(slot, "You already voted. (%d/%d needed)", count, required);

		if (IsThresholdReached(eligible))
		{
			// Should have started already. re-trigger just in case
			StopReminderTimer();
			OnVoteStarted();
			startVote();
		}
		return;
	}

	m_votes.insert(slot);
	int count = GetVoteCount();

	PlayerInfo *pi = g_RTVPlayerManager.GetPlayer(slot);
	const char *name = pi ? pi->name.c_str() : "Unknown";

	RTV_ChatToAllT("%s wants to rock the vote. (%d/%d needed - type !rtv to vote)", name, count, required);

	if (IsThresholdReached(eligible))
	{
		RTV_ChatToAllT("RTV threshold reached! Starting vote...");
		StopReminderTimer();
		OnVoteStarted();
		startVote();
	}
	else
	{
		StartReminderTimer();
	}
}

void RTVManager::CheckEndOfMapVote(StartVoteCallback startVote)
{
	// Deliberately not gated on RtvCfg::enabled, since no petition is involved.
	const EndOfMapVoteCfg &cfg = g_RTVConfig.endOfMapVote;

	if (!cfg.enabled)
	{
		return;
	}

	// Nothing to do if a vote already started, a change is queued, or we already
	// fired the end-of-map vote this map.
	if (m_endOfMapVoteTriggered || m_voteStarted || m_mapChangeScheduled)
	{
		return;
	}

	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;

	// Throttle: reading the convar + math every frame is wasteful. Once/sec is
	// plenty for a threshold measured in minutes.
	if (curtime < m_nextEomCheckTime)
	{
		return;
	}
	m_nextEomCheckTime = curtime + 1.0f;

	float timeLeft = 0.0f;
	if (!g_RTVTimeLimit.GetTimeLeftSeconds(timeLeft))
	{
		return;
	}

	if (timeLeft <= static_cast<float>(cfg.triggerTime))
	{
		m_endOfMapVoteTriggered = true;
		RTV_ChatToAllT("Map is ending soon - starting the next-map vote...");
		StopReminderTimer();
		OnVoteStarted();
		startVote();
	}
}

void RTVManager::StartReminderTimer()
{
	StopReminderTimer();
	int interval = g_RTVConfig.rtv.reminderInterval;
	if (interval <= 0)
	{
		return;
	}

	m_reminderTimerId = g_Timers.CreateTimer(
		static_cast<float>(interval),
		[this]()
		{
			if (m_voteStarted || m_mapChangeScheduled)
			{
				StopReminderTimer();
				return;
			}
			int eligible = (std::max)(g_RTVPlayerManager.GetEligiblePlayerCount(), 1);
			int required = RequiredVotes(eligible);
			int count = GetVoteCount();
			int need = (std::max)(required - count, 0);
			if (need > 0)
			{
				RTV_ChatToAllT("Type !rtv to vote for a map change. (%d more vote(s) needed)", need);
			}
			else
			{
				StopReminderTimer();
			}
		},
		static_cast<float>(interval));
}

void RTVManager::StopReminderTimer()
{
	g_Timers.KillTimer(m_reminderTimerId);
	m_reminderTimerId = -1;
}
