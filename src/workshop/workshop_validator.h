#ifndef _INCLUDE_RTV_WORKSHOP_VALIDATOR_H_
#define _INCLUDE_RTV_WORKSHOP_VALIDATOR_H_

#include "steam/steam_gameserver.h"

#include <string>

extern CSteamGameServerAPIContext g_RTVSteamAPI;

// Verify that a workshop addon's files exist on disk before issuing
// `host_workshop_map <id>`. If the addon's content folder is missing, or it
// exists but contains no .vpk file, the engine would refuse to mount the
// addon, fail to find any maps, and fall back to the 'error' map.
//
// When that desync is detected, the matching entry is removed from
// appworkshop_730.acf and Steam is asked to re-read it. The engine will then
// see the addon as not-installed and download it fresh on the next map command.
//
// Returns true if any state was changed (folder was bad and ACF was edited).
bool RTV_EnsureWorkshopMapReady(const std::string &workshopId);

#endif // _INCLUDE_RTV_WORKSHOP_VALIDATOR_H_
