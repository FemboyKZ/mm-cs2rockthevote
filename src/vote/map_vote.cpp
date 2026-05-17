#include "map_vote.h"
#include "src/config/config.h"
#include "src/menu/chatmenu.h"
#include "src/nominate/nominate.h"
#include "src/player/player_manager.h"
#include "src/rtv/rtv_manager.h"
#include "src/timers/timer_system.h"
#include "src/utils/print_utils.h"
#include "src/workshop/workshop_validator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

MapVoteManager g_MapVoteManager;

static std::default_random_engine g_rng(std::random_device {}());

static void DoMapChange(const MapEntry &entry)
{
	char cmd[256];
	if (entry.isWorkshop && !entry.workshopId.empty())
	{
		RTV_EnsureWorkshopMapReady(entry.workshopId);
		snprintf(cmd, sizeof(cmd), "host_workshop_map %s\n", entry.workshopId.c_str());
	}
	else
	{
		snprintf(cmd, sizeof(cmd), "changelevel %s\n", entry.mapName.c_str());
	}
	g_pEngine->ServerCommand(cmd);
}

void MapVoteManager::OnMapStart(const char *currentMap)
{
	Reset();
	m_currentMap = currentMap ? currentMap : "";
}

void MapVoteManager::Reset()
{
	m_voteActive = false;
	m_isRTV = false;
	m_changeScheduled = false;
	m_runoffActive = false;
	m_options.clear();
	m_playerVotes.clear();
	m_voteEndTime = 0.0f;

	g_Timers.KillTimer(m_countdownTimerId);
	m_countdownTimerId = -1;
	g_Timers.KillTimer(m_changeTimerId);
	m_changeTimerId = -1;
	g_Timers.KillTimer(m_reminderTimerId);
	m_reminderTimerId = -1;
	g_Timers.KillTimer(m_verifyTimerId);
	m_verifyTimerId = -1;
	g_Timers.KillTimer(m_failureTimerId);
	m_failureTimerId = -1;

	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		g_ChatMenus.CloseMenu(i);
	}
}

void MapVoteManager::StartVote(bool isRTV, const std::vector<std::string> &nominations)
{
	if (m_voteActive || m_changeScheduled)
	{
		return;
	}

	const MapVoteCfg &cfg = g_RTVConfig.mapvote;

	if (!cfg.enabled)
	{
		RTV_ChatToAll("\x07Map voting is currently disabled.");
		g_RTVManager.OnVoteEndedNoVotes();
		return;
	}

	m_isRTV = isRTV;
	m_voteActive = true;
	m_playerVotes.clear();
	m_runoffActive = false;

	BuildOptions(nominations, isRTV);

	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;
	float duration = static_cast<float>(cfg.voteDuration);
	m_voteEndTime = curtime + duration;

	RTV_ChatToAll("\x04Map vote started! \x01Type a number in chat to vote.");
	SendVoteMenuToAll();

	if (cfg.countdownInterval > 0)
	{
		float iv = static_cast<float>(cfg.countdownInterval);
		m_countdownTimerId = g_Timers.CreateTimer(
			iv,
			[this]()
			{
				if (!m_voteActive)
				{
					return;
				}
				CGlobalVars *g = GetGameGlobals();
				float now = g ? g->curtime : 0.0f;
				int secsLeft = static_cast<int>(m_voteEndTime - now);
				if (secsLeft > 0)
				{
					SendCountdownReminder(secsLeft);
				}
			},
			iv);
	}

	if (cfg.chatChoiceReminder && cfg.chatChoiceInterval > 0)
	{
		float iv = static_cast<float>(cfg.chatChoiceInterval);
		m_reminderTimerId = g_Timers.CreateTimer(
			iv,
			[this]()
			{
				if (!m_voteActive)
				{
					return;
				}
				CGlobalVars *g = GetGameGlobals();
				float curtime2 = g ? g->curtime : 0.0f;
				for (int i = 0; i <= MAXPLAYERS; i++)
				{
					PlayerInfo *pi = g_RTVPlayerManager.GetPlayer(i);
					if (!pi || !pi->connected || pi->fakePlayer)
					{
						continue;
					}
					if (m_playerVotes.count(i))
					{
						continue;
					}
					if (!g_ChatMenus.HasMenu(i))
					{
						ShowVoteMenuToPlayer(i);
					}
				}
			},
			iv);
	}

	m_verifyTimerId = g_Timers.CreateTimer(duration, [this]() { FinishVote(); });
}

void MapVoteManager::BuildOptions(const std::vector<std::string> &nominations, bool includeNoChange)
{
	m_options.clear();
	int total = (std::max)(g_RTVConfig.mapvote.mapsToShow, 1);

	std::vector<std::string> usedNames;
	usedNames.push_back(m_currentMap);

	for (const auto &nom : nominations)
	{
		if (static_cast<int>(m_options.size()) >= total)
		{
			break;
		}
		const MapEntry *e = g_MapLister.FindExact(nom);
		if (!e)
		{
			continue;
		}

		VoteOption opt;
		opt.entry = e;
		opt.label = e->displayName.empty() ? e->mapName : e->displayName;
		m_options.push_back(opt);
		usedNames.push_back(e->mapName);
	}

	int remaining = total - static_cast<int>(m_options.size());
	if (remaining > 0)
	{
		auto randoms = PickRandomMaps(remaining, usedNames);
		for (auto *e : randoms)
		{
			VoteOption opt;
			opt.entry = e;
			opt.label = e->displayName.empty() ? e->mapName : e->displayName;
			m_options.push_back(opt);
		}
	}

	if (includeNoChange)
	{
		VoteOption opt;
		opt.entry = nullptr;
		opt.label = "Don't Change Map";
		m_options.push_back(opt);
	}
}

std::vector<const MapEntry *> MapVoteManager::PickRandomMaps(int count, const std::vector<std::string> &exclude) const
{
	const auto &allMaps = g_MapLister.GetMaps();
	std::vector<const MapEntry *> pool;

	for (const auto &e : allMaps)
	{
		bool excluded = false;
		for (const auto &ex : exclude)
		{
			if (e.mapName == ex || e.displayName == ex)
			{
				excluded = true;
				break;
			}
		}
		if (!excluded)
		{
			pool.push_back(&e);
		}
	}

	std::shuffle(pool.begin(), pool.end(), g_rng);
	if (static_cast<int>(pool.size()) > count)
	{
		pool.resize(count);
	}

	return pool;
}

void MapVoteManager::SendVoteMenuToAll()
{
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		PlayerInfo *pi = g_RTVPlayerManager.GetPlayer(i);
		if (!pi || !pi->connected || pi->fakePlayer)
		{
			continue;
		}
		ShowVoteMenuToPlayer(i);
	}
}

void MapVoteManager::ShowVoteMenuToPlayer(int slot)
{
	if (!m_voteActive)
	{
		return;
	}

	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;

	ChatMenuDef def;
	def.title = "Vote for next map";
	def.duration = (std::max)(m_voteEndTime - curtime, 5.0f);
	def.exitButton = false;
	def.closeOnSelect = true;

	for (int i = 0; i < static_cast<int>(m_options.size()); i++)
	{
		const VoteOption &opt = m_options[i];
		bool alreadyVoted = false;
		auto it = m_playerVotes.find(slot);
		if (it != m_playerVotes.end() && it->second == i)
		{
			alreadyVoted = true;
		}

		char label[128];
		if (alreadyVoted)
		{
			snprintf(label, sizeof(label), "\x04%s \x01[your vote]", opt.label.c_str());
		}
		else
		{
			snprintf(label, sizeof(label), "%s", opt.label.c_str());
		}

		int capturedIndex = i;
		def.AddItem(label,
					[this, capturedIndex](int playerSlot)
					{
						if (!m_voteActive)
						{
							return;
						}

						auto vit = m_playerVotes.find(playerSlot);
						if (vit != m_playerVotes.end())
						{
							// Toggle: clicking the same option removes the vote
							if (vit->second == capturedIndex)
							{
								m_options[capturedIndex].votes = (std::max)(0, m_options[capturedIndex].votes - 1);
								m_playerVotes.erase(vit);
								PlayerInfo *pi = g_RTVPlayerManager.GetPlayer(playerSlot);
								const char *name = pi ? pi->name.c_str() : "Unknown";
								RTV_ChatToAll("\x04%s\x01 removed their vote for \x04%s", name, m_options[capturedIndex].label.c_str());
								return;
							}
							// Switching vote
							m_options[vit->second].votes = (std::max)(0, m_options[vit->second].votes - 1);
							vit->second = capturedIndex;
						}
						else
						{
							m_playerVotes[playerSlot] = capturedIndex;
						}
						m_options[capturedIndex].votes++;

						PlayerInfo *pi = g_RTVPlayerManager.GetPlayer(playerSlot);
						const char *name = pi ? pi->name.c_str() : "Unknown";
						RTV_ChatToAll("\x04%s\x01 voted for \x04%s", name, m_options[capturedIndex].label.c_str());

						// Auto-shorten: if all eligible players voted and >5s remain, end in 5s
						int eligible = (std::max)(g_RTVPlayerManager.GetEligiblePlayerCount(), 1);
						if (static_cast<int>(m_playerVotes.size()) >= eligible)
						{
							CGlobalVars *g = GetGameGlobals();
							float now = g ? g->curtime : 0.0f;
							float timeLeft = m_voteEndTime - now;

							if (timeLeft > 5.0f)
							{
								// Shorten to 5 seconds
								m_voteEndTime = now + 5.0f;
								g_Timers.KillTimer(m_verifyTimerId);
								m_verifyTimerId = g_Timers.CreateTimer(5.0f, [this]() { FinishVote(); });
								RTV_ChatToAll("\x04"
											  "All players voted! Vote ending in \x04"
											  "5\x01 second(s).");
							}
							else
							{
								g_Timers.KillTimer(m_verifyTimerId);
								m_verifyTimerId = -1;
								FinishVote();
							}
						}
					});
	}

	g_ChatMenus.ShowMenu(slot, def, curtime);
}

void MapVoteManager::CommandRevote(int slot)
{
	if (!m_voteActive)
	{
		RTV_PrintToChat(slot, "\x07There is no vote in progress.");
		return;
	}
	if (!g_RTVConfig.mapvote.enableRevote)
	{
		RTV_PrintToChat(slot, "\x07Revoting is not enabled.");
		return;
	}

	auto it = m_playerVotes.find(slot);
	if (it != m_playerVotes.end())
	{
		m_options[it->second].votes = (std::max)(0, m_options[it->second].votes - 1);
		m_playerVotes.erase(it);
	}

	ShowVoteMenuToPlayer(slot);
}

void MapVoteManager::OnPlayerDisconnect(int slot)
{
	auto it = m_playerVotes.find(slot);
	if (it != m_playerVotes.end())
	{
		int idx = it->second;
		if (idx >= 0 && idx < static_cast<int>(m_options.size()))
		{
			m_options[idx].votes = (std::max)(0, m_options[idx].votes - 1);
		}
		m_playerVotes.erase(it);
	}
}

void MapVoteManager::SendCountdownReminder(int secsLeft)
{
	RTV_ChatToAll("\x04Map vote\x01 ends in \x04%d\x01 second(s). Vote now!", secsLeft);
}

void MapVoteManager::FinishVote()
{
	if (!m_voteActive)
	{
		return;
	}

	m_voteActive = false;

	g_Timers.KillTimer(m_countdownTimerId);
	m_countdownTimerId = -1;
	g_Timers.KillTimer(m_reminderTimerId);
	m_reminderTimerId = -1;
	g_Timers.KillTimer(m_verifyTimerId);
	m_verifyTimerId = -1;

	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		g_ChatMenus.CloseMenu(i);
	}

	if (m_options.empty())
	{
		RTV_ChatToAll("\x07Vote ended with no options.");
		g_RTVManager.OnVoteEndedNoVotes();
		return;
	}

	int maxVotes = 0;
	for (const auto &opt : m_options)
	{
		maxVotes = (std::max)(maxVotes, opt.votes);
	}

	if (maxVotes == 0)
	{
		RTV_ChatToAll("\x07Nobody voted. Map will not change.");
		g_RTVManager.OnVoteEndedNoVotes();
		return;
	}

	std::vector<int> topIndices;
	for (int i = 0; i < static_cast<int>(m_options.size()); i++)
	{
		if (m_options[i].votes == maxVotes)
		{
			topIndices.push_back(i);
		}
	}

	int minPct = g_RTVConfig.mapvote.minWinPercentage;
	if (minPct > 0 && topIndices.size() == 1 && !m_runoffActive)
	{
		int total = static_cast<int>(m_playerVotes.size());
		if (total > 0)
		{
			int pct = (maxVotes * 100) / total;
			if (pct < minPct && g_RTVConfig.mapvote.runoffEnabled)
			{
				RTV_ChatToAll("\x04No map reached %d%% - starting runoff vote.", minPct);
				StartRunoff(topIndices);
				return;
			}
		}
	}

	if (topIndices.size() > 1)
	{
		if (!m_runoffActive && g_RTVConfig.mapvote.runoffEnabled)
		{
			RTV_ChatToAll("\x04Tie! Starting runoff vote with the tied maps.");
			StartRunoff(topIndices);
			return;
		}
		RTV_ChatToAll("\x07Tie! The map will \x07NOT\x04 be changed.");
		g_RTVManager.OnVoteEndedNoVotes();
		return;
	}

	int winnerIndex = topIndices[0];

	const VoteOption &winner = m_options[winnerIndex];

	if (!winner.entry)
	{
		RTV_ChatToAll("\x04The map will \x07NOT\x04 be changed.");
		g_RTVManager.OnVoteEndedNoVotes();
		return;
	}

	int delaySecs = g_RTVConfig.rtv.mapChangeDelay;
	RTV_ChatToAll("\x04%s\x01 won the vote! Map changing in \x04%d\x01 second(s).", winner.label.c_str(), delaySecs);

	ScheduleChange(winner, delaySecs);
}

void MapVoteManager::StartRunoff(const std::vector<int> &tiedIndices)
{
	std::vector<VoteOption> runoffOpts;
	for (int idx : tiedIndices)
	{
		VoteOption opt = m_options[idx];
		opt.votes = 0;
		runoffOpts.push_back(opt);
	}
	m_options = runoffOpts;
	m_playerVotes.clear();
	m_runoffActive = true;
	m_voteActive = true;

	CGlobalVars *globals = GetGameGlobals();
	float curtime = globals ? globals->curtime : 0.0f;
	float duration = static_cast<float>(g_RTVConfig.mapvote.voteDuration);
	m_voteEndTime = curtime + duration;

	RTV_ChatToAll("\x04Runoff vote started!");
	SendVoteMenuToAll();

	const MapVoteCfg &cfg = g_RTVConfig.mapvote;

	if (cfg.countdownInterval > 0)
	{
		float iv = static_cast<float>(cfg.countdownInterval);
		m_countdownTimerId = g_Timers.CreateTimer(
			iv,
			[this]()
			{
				if (!m_voteActive)
				{
					return;
				}
				CGlobalVars *g = GetGameGlobals();
				float now = g ? g->curtime : 0.0f;
				int secsLeft = static_cast<int>(m_voteEndTime - now);
				if (secsLeft > 0)
				{
					SendCountdownReminder(secsLeft);
				}
			},
			iv);
	}

	if (cfg.chatChoiceReminder && cfg.chatChoiceInterval > 0)
	{
		float iv = static_cast<float>(cfg.chatChoiceInterval);
		m_reminderTimerId = g_Timers.CreateTimer(
			iv,
			[this]()
			{
				if (!m_voteActive)
				{
					return;
				}
				CGlobalVars *g = GetGameGlobals();
				float curtime2 = g ? g->curtime : 0.0f;
				for (int i = 0; i <= MAXPLAYERS; i++)
				{
					PlayerInfo *pi = g_RTVPlayerManager.GetPlayer(i);
					if (!pi || !pi->connected || pi->fakePlayer)
					{
						continue;
					}
					if (m_playerVotes.count(i))
					{
						continue;
					}
					if (!g_ChatMenus.HasMenu(i))
					{
						ShowVoteMenuToPlayer(i);
					}
				}
			},
			iv);
	}

	m_verifyTimerId = g_Timers.CreateTimer(duration, [this]() { FinishVote(); });
}

void MapVoteManager::ScheduleChange(const VoteOption &winner, int delaySecs)
{
	m_changeScheduled = true;
	g_RTVManager.OnMapChangeScheduled();

	MapEntry captured = *winner.entry;

	m_changeTimerId = g_Timers.CreateTimer(static_cast<float>(delaySecs), [captured]() { DoMapChange(captured); });

	// Failure detection: if OnLevelInit doesn't fire within delay+30s, reset state
	float failureTimeout = static_cast<float>(delaySecs) + 30.0f;
	m_failureTimerId = g_Timers.CreateTimer(failureTimeout,
											[this, captured]()
											{
												// If we're still showing 'change scheduled', the map change failed
												if (m_changeScheduled)
												{
													META_CONPRINTF("[CS2RTV] Map change to '%s' appears to have failed - "
																   "resetting vote state.\n",
																   captured.mapName.c_str());
													m_changeScheduled = false;
													m_voteActive = false;
													g_RTVManager.OnVoteEndedNoVotes();
													g_NominateManager.Reset();
													m_failureTimerId = -1;
												}
											});
}

void MapVoteManager::NotifyMapChangeSucceeded()
{
	g_Timers.KillTimer(m_failureTimerId);
	m_failureTimerId = -1;
}

void MapVoteManager::ExecuteMapChange(const VoteOption &winner)
{
	if (winner.entry)
	{
		DoMapChange(*winner.entry);
	}
}
