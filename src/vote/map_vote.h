#ifndef _INCLUDE_RTV_MAP_VOTE_H_
#define _INCLUDE_RTV_MAP_VOTE_H_

#include "mmu/workshop.h"

#include "src/common.h"
#include "src/maplist/map_lister.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class VoteOptionKind
{
	Map,
	NoChange,
	Extend,
};

struct VoteOption
{
	// A copy, since a dynamic map added mid-vote can reallocate the map list under a pointer.
	MapEntry entry; // set only for Map options
	VoteOptionKind kind = VoteOptionKind::Map;
	std::string label;    // map display name, or a phrase key for the other kinds
	std::string announce; // chat-safe text for the "X voted for Y" lines
	int extendMinutes = 0;
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

	// Admin cancel of a running vote or scheduled change.
	// Unlike Reset the map keeps running, so the RTV gates have to reopen with it.
	void CancelVote();

	// Immediate admin change to one map, through the same workshop download wait as the vote path.
	// Change to this map right away, as !mapmenu does. False when it is a workshop map the server does not have.
	bool ChangeMapNow(const MapEntry &entry);

private:
	bool m_voteActive = false;
	bool m_isRTV = false;
	bool m_changeScheduled = false;
	bool m_runoffActive = false;
	std::string m_currentMap;
	// Clean name of the scheduled winner, so a cancel can take the nextlevel backstop back down.
	std::string m_scheduledMap;
	std::vector<VoteOption> m_options;
	std::unordered_map<int, int> m_playerVotes; // slot -> option index
	std::unordered_set<int> m_dismissed;        // closed the vote menu without voting

	// Timers
	int m_countdownTimerId = -1;
	int m_changeTimerId = -1;
	int m_reminderTimerId = -1;
	int m_verifyTimerId = -1;
	int m_failureTimerId = -1; // detects if map change never fires
	float m_voteEndTime = 0.0f;

	void BuildOptions(const std::vector<std::string> &nominations, bool includeNoChange);
	void ApplyExtendWin(int minutes);
	void SendVoteMenuToAll();
	void SendCountdownReminder(int secsLeft);
	void SendChoiceReminders();
	void FinishVote();
	void StartRunoff(const std::vector<int> &tiedIndices);
	void ExecuteMapChange(const VoteOption &winner);
	void ScheduleChange(const VoteOption &winner, int delaySecs);
	void ArmChangeFailureTimer(const MapEntry &entry, float timeout);

	// host_workshop_map on an addon that isn't on disk drops the server onto the "error" map,
	// so an absent winner is downloaded first and only then loaded.
	void BeginMapChange(const MapEntry &entry);
	bool WaitForWorkshopMap(const MapEntry &entry, bool fromVote);
	void AbortChange();

	int m_downloadTimerId = -1;
	mmu::workshop::PendingDownload m_pendingDownload;

	// Total tries, so one retry before the map goes back to the players.
	static constexpr int kMaxChangeAttempts = 2;
	int m_changeAttempts = 0;

	// Choose random maps (excluding currentMap and already-chosen ones)
	std::vector<const MapEntry *> PickRandomMaps(int count, const std::vector<std::string> &exclude) const;
};

extern MapVoteManager g_MapVoteManager;

#endif // _INCLUDE_RTV_MAP_VOTE_H_
