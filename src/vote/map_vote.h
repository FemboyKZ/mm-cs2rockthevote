#ifndef _INCLUDE_RTV_MAP_VOTE_H_
#define _INCLUDE_RTV_MAP_VOTE_H_

#include "src/common.h"
#include "src/maplist/map_lister.h"

#include <string>
#include <unordered_map>
#include <vector>

struct VoteOption
{
	const MapEntry *entry = nullptr; // nullptr = "Don't Change Map"
	std::string label;
	int votes = 0;
};

class MapVoteManager
{
public:
	// Called when a map loads
	void OnMapStart(const char *currentMap);

	// Start a vote. isRTV=true adds a "Don't Change" option.
	// Uses nominations first, fills remainder with random maps.
	void StartVote(bool isRTV, const std::vector<std::string> &nominations);

	// Returns whether a vote is currently in progress
	bool IsVoteActive() const
	{
		return m_voteActive;
	}

	// Returns whether a map change is already scheduled
	bool IsChangeScheduled() const
	{
		return m_changeScheduled;
	}

	// True if the current/most recent vote was an RTV vote (vs end-of-map).
	bool IsRTVVote() const
	{
		return m_isRTV;
	}

	// Clean name of the map currently running.
	const char *GetCurrentMap() const
	{
		return m_currentMap.c_str();
	}

	// Called from OnLevelInit to cancel the failure-detection timer (change succeeded)
	void NotifyMapChangeSucceeded();

	// Re-open the vote menu for a player (e.g. they typed !rtv while vote runs)
	void ShowVoteMenuToPlayer(int slot);

	// Handle !revote command
	void CommandRevote(int slot);

	// Called from RTV on player disconnect to free their vote
	void OnPlayerDisconnect(int slot);

	// Force end any in-progress vote and reset (map end)
	void Reset();

private:
	bool m_voteActive = false;
	bool m_isRTV = false;
	bool m_changeScheduled = false;
	bool m_runoffActive = false;
	std::string m_currentMap;
	std::vector<VoteOption> m_options;
	std::unordered_map<int, int> m_playerVotes; // slot -> option index

	// Timers
	int m_countdownTimerId = -1;
	int m_changeTimerId = -1;
	int m_reminderTimerId = -1;
	int m_verifyTimerId = -1;
	int m_failureTimerId = -1; // detects if map change never fires
	float m_voteEndTime = 0.0f;

	void BuildOptions(const std::vector<std::string> &nominations, bool includeNoChange);
	void SendVoteMenuToAll();
	void SendCountdownReminder(int secsLeft);
	void FinishVote();
	void StartRunoff(const std::vector<int> &tiedIndices);
	void ExecuteMapChange(const VoteOption &winner);
	void ScheduleChange(const VoteOption &winner, int delaySecs);

	// Choose random maps (excluding currentMap and already-chosen ones)
	std::vector<const MapEntry *> PickRandomMaps(int count, const std::vector<std::string> &exclude) const;
};

extern MapVoteManager g_MapVoteManager;

#endif // _INCLUDE_RTV_MAP_VOTE_H_
