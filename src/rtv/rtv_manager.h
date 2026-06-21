#ifndef _INCLUDE_RTV_RTV_MANAGER_H_
#define _INCLUDE_RTV_RTV_MANAGER_H_

#include "src/common.h"

#include <functional>
#include <unordered_set>

class RTVManager
{
public:
	// Called when a new map loads
	void OnMapStart(const char *mapName);

	// Called when a player disconnects
	void OnPlayerDisconnect(int slot);

	// Handle !rtv from a player slot.
	// The startVoteCallback is invoked (exactly once) when the vote threshold
	// is first reached so that the map-vote manager can kick off the vote.
	using StartVoteCallback = std::function<void()>;
	void CommandHandler(int slot, StartVoteCallback startVote);

	// Called every frame. When the end-of-map-vote setting is on, starts the
	// vote automatically once mp_timelimit is within endOfMapVoteTime seconds
	// of expiring and nobody has triggered !rtv yet. Cheap-throttled internally.
	void CheckEndOfMapVote(StartVoteCallback startVote);

	// Re-open the current vote menu for a player (when vote is already running)
	// The caller owns calling this through the map-vote manager.

	// Notify the RTV system that a vote started (prevents double-starting)
	void OnVoteStarted();

	// Notify the RTV system that the vote ended with no result (allows revoting)
	void OnVoteEndedNoVotes();

	// Notify the RTV system that a map change has been scheduled
	void OnMapChangeScheduled();

	bool IsVoteStarted() const
	{
		return m_voteStarted;
	}

	bool IsMapChangeScheduled() const
	{
		return m_mapChangeScheduled;
	}

	int GetVoteCount() const
	{
		return static_cast<int>(m_votes.size());
	}

	bool HasVoted(int slot) const
	{
		return m_votes.count(slot) > 0;
	}

	// True once enough players have voted
	bool IsThresholdReached(int eligibleCount) const;

	// Number of !rtv votes required to trigger the vote,
	// computed from the current eligible player count.
	int GetVotesNeeded() const;

private:
	std::unordered_set<int> m_votes;
	bool m_voteStarted = false;
	bool m_mapChangeScheduled = false;
	float m_cooldownExpireTime = 0.0f;
	float m_mapStartTime = 0.0f;
	bool m_endOfMapVoteTriggered = false;
	float m_nextEomCheckTime = 0.0f;

	int RequiredVotes(int eligibleCount) const;
	void StartReminderTimer();
	void StopReminderTimer();
	int m_reminderTimerId = -1;
};

extern RTVManager g_RTVManager;

#endif // _INCLUDE_RTV_RTV_MANAGER_H_
