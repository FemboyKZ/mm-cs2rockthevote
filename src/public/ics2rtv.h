#ifndef _INCLUDE_ICS2RTV_H_
#define _INCLUDE_ICS2RTV_H_

#include <cstdint>

// Other Metamod plugins can acquire this interface via:
//   ICS2RTV *rtv = (ICS2RTV *)g_SMAPI->MetaFactory(
//       CS2RTV_INTERFACE, nullptr, nullptr);
#define CS2RTV_INTERFACE "ICS2RTV001"

// Public read-only interface for CS2RockTheVote.
// Provides vote/map-change status and access to the loaded map list.
//
// const char* returns point at cs2rockthevote's internal storage and are valid
// until the next map load / maplist reload.
// Copy the string if you need to keep it.
// They are never null for valid indices, invalid indices return "".
class ICS2RTV
{
public:
	// Vote status

	// True while a map vote (RTV or end-of-map) is in progress.
	virtual bool IsVoteActive() = 0;

	// True if the current (or most recent) vote was triggered by !rtv,
	// as opposed to an end-of-map / nextmap vote.
	// Only meaningful when a vote is active or a change is scheduled.
	virtual bool IsRTVVote() = 0;

	// True once a winning map has been chosen and a changelevel is pending.
	virtual bool IsMapChangeScheduled() = 0;

	// RTV tally (the !rtv count accumulated before a vote actually starts)

	// Number of players who have typed !rtv this map.
	virtual int GetRTVVoteCount() = 0;

	// Number of !rtv votes required to trigger the vote,
	// based on the current eligible player count and the configured ratio.
	virtual int GetRTVVotesNeeded() = 0;

	// True if the player in the given slot has already typed !rtv.
	virtual bool HasPlayerRockedVote(int slot) = 0;

	// Map list

	// Number of maps in the loaded map list.
	virtual int GetMapCount() = 0;

	// Clean map name for `index` (the changelevel name, e.g. "de_dust2").
	virtual const char *GetMapName(int index) = 0;

	// Player-facing display label for `index` (e.g. "kz_grotto (T3, Linear)").
	virtual const char *GetMapDisplayName(int index) = 0;

	// Workshop ID for `index`, or "" if the map is not a workshop map.
	virtual const char *GetMapWorkshopId(int index) = 0;

	// Clean name of the map currently running.
	virtual const char *GetCurrentMap() = 0;
};

#endif // _INCLUDE_ICS2RTV_H_
