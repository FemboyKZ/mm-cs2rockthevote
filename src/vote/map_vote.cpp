#include "map_vote.h"
#include "mmu/log.h"
#include "src/config/config.h"
#include "src/lang/translations.h"
#include "src/menu/chatmenu.h"
#include "src/menu/menu_bridge.h"
#include "src/nominate/nominate.h"
#include "src/player/player_manager.h"
#include "src/rtv/rtv_manager.h"
#include "src/timelimit/timelimit.h"
#include "src/timers/timer_system.h"
#include "src/public/forwards.h"
#include "src/utils/print_utils.h"
#include "mmu/workshop.h"

extern CSteamGameServerAPIContext g_RTVSteamAPI;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

MapVoteManager g_MapVoteManager;

static std::default_random_engine g_rng(std::random_device {}());

static void DoMapChange(const MapEntry &entry)
{
	char cmd[256];
	if (entry.isWorkshop && !entry.workshopId.empty())
	{
		mmu::EnsureWorkshopMapReady(entry.workshopId, g_RTVSteamAPI);
		snprintf(cmd, sizeof(cmd), "host_workshop_map %s\n", entry.workshopId.c_str());
	}
	else
	{
		snprintf(cmd, sizeof(cmd), "changelevel %s\n", entry.mapName.c_str());
	}
	g_pEngine->ServerCommand(cmd);
}

static CConVarRef<CUtlString> &NextLevelRef()
{
	static CConVarRef<CUtlString> ref("nextlevel");
	return ref;
}

static bool NextLevelUsable()
{
	return NextLevelRef().IsValidRef() && NextLevelRef().IsConVarDataAvailable();
}

// Backstop for a map ending on its own before our changelevel lands.
// A workshop map is not mounted until host_workshop_map runs and the engine only resolves a bare name here,
// so there is nothing safe to hand it for those.
static void SetNextLevel(const MapEntry &entry)
{
	if (entry.isWorkshop || !NextLevelUsable())
	{
		return;
	}
	NextLevelRef().Set(entry.mapName.c_str());
}

// We change maps ourselves, so the engine never consumes nextlevel and it would still point at the map we just landed on,
// reloading it forever.
static void ClearConsumedNextLevel(const char *currentMap)
{
	if (!currentMap || !currentMap[0] || !NextLevelUsable())
	{
		return;
	}
	const char *pending = NextLevelRef().Get().Get();
	if (pending && V_stricmp(pending, currentMap) == 0)
	{
		NextLevelRef().Set("");
	}
}

void MapVoteManager::OnMapStart(const char *currentMap)
{
	Reset();
	m_currentMap = currentMap ? currentMap : "";
	ClearConsumedNextLevel(m_currentMap.c_str());
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
	m_changeAttempts = 0;

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
	g_Timers.KillTimer(m_downloadTimerId);
	m_downloadTimerId = -1;

	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		g_RTVMenus.CloseMenu(i);
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
		RTV_ChatToAllT("Map voting is currently disabled.");
		g_RTVManager.OnVoteEndedNoVotes();
		return;
	}

	if (g_CS2RTVForwards.FireOnMapVoteStart(isRTV))
	{
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

	if (g_RTVMenus.UsesChatInput())
	{
		RTV_ChatToAllT("Map vote started! Type a number in chat to vote.");
	}
	else
	{
		RTV_ChatToAllT("Map vote started!");
	}
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
					if (!g_RTVMenus.HasMenu(i))
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
		opt.label = g_MapLister.GetDisplayLabel(*e);
		opt.announce = opt.label;
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
			opt.label = g_MapLister.GetDisplayLabel(*e);
			opt.announce = opt.label;
			m_options.push_back(opt);
		}
	}

	// Only RTV votes get "Don't Change Map".
	// On an end-of-map vote it would win and the map would still die at the limit, so extending is the real choice.
	if (includeNoChange)
	{
		VoteOption opt;
		opt.kind = VoteOptionKind::NoChange;
		opt.label = "Don't Change Map";
		opt.announce = opt.label;
		m_options.push_back(opt);
	}

	if (g_RTVTimeLimit.IsExtendAvailable())
	{
		// The convar max can leave less headroom than the configured step.
		int minutes = g_RTVConfig.extend.minutes;
		int headroom = g_RTVTimeLimit.GetHeadroomMinutes();
		if (headroom >= 0)
		{
			minutes = (std::min)(minutes, headroom);
		}

		if (minutes > 0)
		{
			char announce[64];
			snprintf(announce, sizeof(announce), "Extend Map (+%d min)", minutes);

			VoteOption opt;
			opt.kind = VoteOptionKind::Extend;
			opt.label = "Extend Map (+%d min)";
			opt.announce = announce;
			opt.extendMinutes = minutes;
			m_options.push_back(opt);
		}
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
	def.title = RTV_Translate(slot, "Vote for next map");
	def.duration = (std::max)(m_voteEndTime - curtime, 5.0f);
	def.exitButton = true;
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

		// The no-change and extend options carry a phrase key as their label, so translate them per viewer.
		// Real map options use their language-neutral display name.
		std::string optLabel = opt.kind == VoteOptionKind::Map ? opt.label : RTV_Translate(slot, opt.label.c_str());

		if (opt.kind == VoteOptionKind::Extend)
		{
			char extendLabel[128];
			snprintf(extendLabel, sizeof(extendLabel), optLabel.c_str(), opt.extendMinutes);
			optLabel = extendLabel;
		}

		char label[128];
		if (alreadyVoted)
		{
			snprintf(label, sizeof(label), "\x04%s \x01%s", optLabel.c_str(), RTV_Translate(slot, "[your vote]").c_str());
		}
		else
		{
			snprintf(label, sizeof(label), "%s", optLabel.c_str());
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
								RTV_ChatToAllT("%s removed their vote for %s", name, m_options[capturedIndex].announce.c_str());
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
						RTV_ChatToAllT("%s voted for %s", name, m_options[capturedIndex].announce.c_str());

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
								RTV_ChatToAllT("All players voted! Vote ending in 5 second(s).");
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

	g_RTVMenus.ShowMenu(slot, def, curtime);
}

void MapVoteManager::CommandRevote(int slot)
{
	if (!m_voteActive)
	{
		RTV_PrintToChatT(slot, "There is no vote in progress.");
		return;
	}
	if (!g_RTVConfig.mapvote.enableRevote)
	{
		RTV_PrintToChatT(slot, "Revoting is not enabled.");
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
	RTV_ChatToAllT("Map vote ends in %d second(s). Vote now!", secsLeft);
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
		g_RTVMenus.CloseMenu(i);
	}

	if (m_options.empty())
	{
		RTV_ChatToAllT("Vote ended with no options.");
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
		RTV_ChatToAllT("Nobody voted. Map will not change.");
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
				RTV_ChatToAllT("No map reached %d%% - starting runoff vote.", minPct);
				StartRunoff(topIndices);
				return;
			}
		}
	}

	if (topIndices.size() > 1)
	{
		if (!m_runoffActive && g_RTVConfig.mapvote.runoffEnabled)
		{
			RTV_ChatToAllT("Tie! Starting runoff vote with the tied maps.");
			StartRunoff(topIndices);
			return;
		}
		RTV_ChatToAllT("Tie! The map will NOT be changed.");
		g_RTVManager.OnVoteEndedNoVotes();
		return;
	}

	int winnerIndex = topIndices[0];

	const VoteOption &winner = m_options[winnerIndex];

	if (winner.kind == VoteOptionKind::Extend)
	{
		ApplyExtendWin(winner.extendMinutes);
		return;
	}

	if (winner.kind == VoteOptionKind::NoChange || !winner.entry)
	{
		RTV_ChatToAllT("The map will NOT be changed.");
		g_RTVManager.OnVoteEndedNoVotes();
		return;
	}

	g_CS2RTVForwards.FireOnMapVoteEnd(winner.entry->mapName.c_str(), m_isRTV);

	int delaySecs = g_RTVConfig.mapvote.mapChangeDelay;
	RTV_ChatToAllT("%s won the vote! Map changing in %d second(s).", winner.label.c_str(), delaySecs);

	ScheduleChange(winner, delaySecs);
}

// By value, not by option ref: the failure path restarts the vote, clearing m_options.
void MapVoteManager::ApplyExtendWin(int minutes)
{
	ExtendResult res = g_RTVTimeLimit.Extend(minutes);
	RTV_AnnounceExtend(res);

	g_RTVManager.OnVoteEndedNoVotes();

	if (res.applied)
	{
		g_RTVManager.OnMapExtended();
		return;
	}

	// The cap moved between building the option and the vote ending,
	// so the map still needs somewhere to go.
	RTV_ChatToAllT("Map is ending soon - starting the next-map vote...");
	g_RTVManager.OnVoteStarted();
	StartVote(false, g_NominateManager.GetNominations());
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

	RTV_ChatToAllT("Runoff vote started!");
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
					if (!g_RTVMenus.HasMenu(i))
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
	m_changeAttempts = 0;
	g_RTVManager.OnMapChangeScheduled();

	MapEntry captured = *winner.entry;

	// Covers the map ending on its own before the changelevel below lands,
	// and stops cs2kz-metamod filling nextlevel with the launch map instead of the winner.
	SetNextLevel(captured);

	g_CS2RTVForwards.FireOnMapChangeScheduled(captured.mapName.c_str(), delaySecs);

	m_changeTimerId = g_Timers.CreateTimer(static_cast<float>(delaySecs), [this, captured]() { BeginMapChange(captured); });

	ArmChangeFailureTimer(captured, static_cast<float>(delaySecs) + 30.0f);
}

static const char *MapLabel(const MapEntry &entry)
{
	return entry.displayName.empty() ? entry.mapName.c_str() : entry.displayName.c_str();
}

void MapVoteManager::BeginMapChange(const MapEntry &entry)
{
	uint64_t fileId = entry.isWorkshop ? std::strtoull(entry.workshopId.c_str(), nullptr, 10) : 0;

	if (fileId == 0 || mmu::workshop::IsReady(fileId, g_RTVSteamAPI))
	{
		DoMapChange(entry);
		return;
	}

	WaitForWorkshopMap(entry);
}

void MapVoteManager::WaitForWorkshopMap(const MapEntry &entry)
{
	uint64_t fileId = std::strtoull(entry.workshopId.c_str(), nullptr, 10);

	// The wait is deliberate, so the failure timer must not call it a failed change.
	g_Timers.KillTimer(m_failureTimerId);
	m_failureTimerId = -1;

	if (g_RTVConfig.mapvote.workshopDownloadTimeout <= 0)
	{
		MMU_LOG_WARN("Workshop map '%s' (%s) is not installed and waiting is disabled, abandoning the change.\n", entry.mapName.c_str(),
					 entry.workshopId.c_str());
		AbortChange();
		return;
	}

	if (!mmu::workshop::StartDownload(fileId, g_RTVSteamAPI))
	{
		MMU_LOG_WARN("Workshop map '%s' (%s) is not installed and no download could be started.\n", entry.mapName.c_str(), entry.workshopId.c_str());
		AbortChange();
		return;
	}

	CGlobalVars *globals = GetGameGlobals();
	float now = globals ? globals->curtime : 0.0f;
	m_downloadDeadline = now + static_cast<float>(g_RTVConfig.mapvote.workshopDownloadTimeout);
	m_nextProgressAnnounce = now + 10.0f;

	MMU_LOG_INFO("Downloading workshop map '%s' (%s) before changing.\n", entry.mapName.c_str(), entry.workshopId.c_str());
	RTV_ChatToAllT("Downloading %s, the map will change once it finishes.", MapLabel(entry));

	MapEntry captured = entry;
	g_Timers.KillTimer(m_downloadTimerId);
	m_downloadTimerId = g_Timers.CreateTimer(
		1.0f,
		[this, captured, fileId]()
		{
			CGlobalVars *g = GetGameGlobals();
			float curtime = g ? g->curtime : 0.0f;

			if (mmu::workshop::IsReady(fileId, g_RTVSteamAPI))
			{
				g_Timers.KillTimer(m_downloadTimerId);
				m_downloadTimerId = -1;
				DoMapChange(captured);
				ArmChangeFailureTimer(captured, 30.0f);
				return;
			}

			if (curtime >= m_downloadDeadline)
			{
				g_Timers.KillTimer(m_downloadTimerId);
				m_downloadTimerId = -1;
				MMU_LOG_WARN("Workshop map '%s' (%llu) did not download in time, staying on the current map.\n", captured.mapName.c_str(),
							 static_cast<unsigned long long>(fileId));
				RTV_ChatToAllT("%s could not be downloaded in time. Staying on the current map.", MapLabel(captured));
				AbortChange();
				return;
			}

			if (curtime >= m_nextProgressAnnounce)
			{
				m_nextProgressAnnounce = curtime + 10.0f;
				uint64_t done = 0, total = 0;
				if (mmu::workshop::DownloadProgress(fileId, g_RTVSteamAPI, done, total))
				{
					RTV_ChatToAllT("Downloading %s... %d%%", MapLabel(captured), static_cast<int>((done * 100) / total));
				}
			}
		},
		1.0f);
}

// Hands the map back to the players rather than leaving a change half-scheduled.
void MapVoteManager::AbortChange()
{
	g_Timers.KillTimer(m_downloadTimerId);
	m_downloadTimerId = -1;
	g_Timers.KillTimer(m_failureTimerId);
	m_failureTimerId = -1;

	m_changeScheduled = false;
	m_voteActive = false;
	g_RTVManager.OnVoteEndedNoVotes();
	g_NominateManager.Reset();
}

// Fires when OnLevelInit hasn't arrived in time, meaning the change never took.
void MapVoteManager::ArmChangeFailureTimer(const MapEntry &entry, float timeout)
{
	g_Timers.KillTimer(m_failureTimerId);
	m_failureTimerId = g_Timers.CreateTimer(
		timeout,
		[this, entry]()
		{
			m_failureTimerId = -1;
			if (!m_changeScheduled)
			{
				return;
			}

			// nextlevel only helps non-workshop maps, so retry the command itself before giving the map back to the players.
			if (++m_changeAttempts < kMaxChangeAttempts)
			{
				MMU_LOG_WARN("Map change to '%s' did not take, retrying (%d/%d).\n", entry.mapName.c_str(), m_changeAttempts + 1, kMaxChangeAttempts);
				DoMapChange(entry);
				ArmChangeFailureTimer(entry, 30.0f);
				return;
			}

			MMU_LOG_WARN("Map change to '%s' failed %d time(s) - resetting vote state.\n", entry.mapName.c_str(), kMaxChangeAttempts);
			m_changeScheduled = false;
			m_voteActive = false;
			g_RTVManager.OnVoteEndedNoVotes();
			g_NominateManager.Reset();
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
