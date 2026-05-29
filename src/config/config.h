#ifndef _INCLUDE_RTV_CONFIG_H_
#define _INCLUDE_RTV_CONFIG_H_

#include <string>

struct RtvCfg
{
	bool enabled = true;
	int votePercentage = 51;
	int reminderInterval = 60;
	int mapChangeDelay = 5;
	int cooldownDuration = 30;
	int mapStartDelay = 30;
	bool endOfMapVote = false;
	int endOfMapVoteTime = 180;
};

struct MapVoteCfg
{
	bool enabled = true;
	int mapsToShow = 6;
	int voteDuration = 90;
	int minWinPercentage = 0;
	bool runoffEnabled = true;
	int countdownInterval = 15;
	bool chatChoiceReminder = true;
	int chatChoiceInterval = 15;
	bool enableRevote = true;
};

struct NominateCfg
{
	bool enabled = true;
	int nominateLimit = 1;
	std::string permission = ""; // blank = everyone
	std::string externalNominatePermission = "changemap";
};

struct MapChooserCfg
{
	// Comma-separated command aliases (without !/mm_ prefix): "mapmenu,mm"
	std::string commands = "mapmenu,mm";
	// Admin flag required to use map chooser; blank = everyone
	std::string permission = "changemap";
};

struct GeneralCfg
{
	bool includeSpectator = true;
	std::string chatPrefix = "\x07[RTV]\x01 "; // red "[RTV]" + default
	std::string adminPermission = "root";
	std::string commandPrefix = "!";       // normal: message visible in chat
	std::string silentCommandPrefix = "/"; // silent: message suppressed
	bool enableMapValidation = false;
	std::string steamApiKey = "";
	std::string discordWebhook = "";
	std::string kzTierMode = "classic"; // "classic" or "vanilla"
};

struct RTVPluginConfig
{
	RtvCfg rtv;
	MapVoteCfg mapvote;
	NominateCfg nominate;
	MapChooserCfg mapchooser;
	GeneralCfg general;
};

// Load/parse cfg/cs2rtv/core.cfg. Returns true on success.
bool RTV_LoadConfig(const char *path, RTVPluginConfig &config);

extern RTVPluginConfig g_RTVConfig;

#endif // _INCLUDE_RTV_CONFIG_H_
