#ifndef _INCLUDE_RTV_CONFIG_H_
#define _INCLUDE_RTV_CONFIG_H_

#include "interfaces/cs2menus/menu_style.h"
#include "utils/config_blocks.h"

#include <string>

// The !rtv petition, not the vote it opens.
struct RtvCfg
{
	bool enabled = true;
	int votePercentage = 51; // of eligible players
	int reminderInterval = 120;
	int cooldown = 30;
	int mapStartDelay = 30;
};

// The ballot itself, whoever opened it.
struct MapVoteCfg
{
	bool enabled = true;
	int options = 6;
	int excludeRecentMaps = 3;
	int voteDuration = 90;
	int minWinPercentage = 0; // of cast votes, else runoff
	bool runoffEnabled = true;
	int countdownInterval = 15;
	bool chatChoiceReminder = true;
	int chatChoiceInterval = 15;
	bool revoteEnabled = true;
	bool voteNotice = false; // a notice instead of the panorama ballot, which takes the mouse
	int mapChangeDelay = 5;
	int workshopDownloadTimeout = 120; // seconds to wait for an absent workshop map
};

// Auto-opens a ballot near the end of the map.
struct EndOfMapVoteCfg
{
	bool enabled = false;
	int triggerTime = 180; // seconds of map time left when it fires
};

struct ExtendCfg
{
	bool enabled = true;
	int minutes = 60;          // added per extension
	int max = 0;               // per map, 0 = unlimited (the convar max still applies)
	int roundTimeCap = 1440;   // raise the mp_roundtime max to this, 0 = leave the engine's
	std::string mode = "auto"; // "auto" | "roundtime" | "timelimit"
};

struct NominateCfg
{
	bool enabled = true;
	int max = 1; // per player and map, 0 = unlimited
};

// Admin flag needed for each, blank = everyone.
struct PermissionsCfg
{
	std::string extend = "changemap";
	std::string nominate;
	std::string nominateExternal = "changemap"; // maps not in the local maplist
	std::string admin = "root";
};

struct DiscordCfg
{
	std::string webhookUrl; // dead-map alerts, blank = off
};

struct GeneralCfg : mmu::config::GeneralBlock
{
	bool includeSpectator = true;
	std::string chatPrefix = "\x07[RTV]\x01 "; // red "[RTV]" + default
	bool kzApiMaplist = false;                 // CS2KZ API approved maps instead of maplist.txt
	bool mapValidationEnabled = false;
	std::string steamApiKey = "";
	std::string displayKzTiers = "off";  // "off" | "both" | "classic"/"ckz" | "vanilla"/"vnl"
	std::string kzTierFormat = "number"; // "number" (e.g. 3) | "text" (e.g. medium)

	// Each prefix is a set of trigger characters. The one chat hints name.
	char HintPrefix() const
	{
		return !commandPrefix.empty() ? commandPrefix[0] : !silentCommandPrefix.empty() ? silentCommandPrefix[0] : '!';
	}
};

struct RTVConfig
{
	RtvCfg rtv;
	MapVoteCfg mapvote;
	EndOfMapVoteCfg endOfMapVote;
	ExtendCfg extend;
	NominateCfg nominate;
	PermissionsCfg permissions;
	DiscordCfg discord;
	GeneralCfg general;

	MenuStyleBlock menu;

	mmu::config::LogBlock log;
};

// Load/parse cfg/cs2rtv/core.cfg. Returns true on success.
bool RTV_LoadConfig(const char *path, RTVConfig &config);

extern RTVConfig g_RTVConfig;

#endif // _INCLUDE_RTV_CONFIG_H_
