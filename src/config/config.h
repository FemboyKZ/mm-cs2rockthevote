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
	std::string displayKzTiers = "off";  // "off" | "both" | "classic"/"ckz" | "vanilla"/"vnl"
	std::string kzTierFormat = "number"; // "number" (e.g. 3) | "text" (e.g. medium)
	std::string defaultLanguage = "en";  // phrase-file key used when a client's language is unknown
	bool logToFile = true;               // mirror log output to addons/cs2rockthevote/logs
	int logRetentionDays = 30;           // delete log files older than this, 0 keeps all
	// Menu rendering style when mm-cs2menus is loaded:
	// "default" delegates to the menu plugin's own config; "chat"/"html" force it.
	std::string menuType = "default";
	// Per-menu HTML nav-key overrides for RTV menus (only used with mm-cs2menus).
	// "default" delegates to the menu plugin's configured key, otherwise a key name
	// (w/s/a/d, e/use, shift/speed, ctrl/duck, space/jump, r/reload, mouse1, mouse2, tab).
	std::string menuNavUp = "default";
	std::string menuNavDown = "default";
	std::string menuNavSelect = "default";
	std::string menuNavBack = "default";
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
