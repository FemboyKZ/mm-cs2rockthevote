#ifndef _INCLUDE_RTV_CONFIG_H_
#define _INCLUDE_RTV_CONFIG_H_

#include <string>

// The !rtv petition, not the vote it opens.
struct RtvCfg
{
	bool enabled = true;
	int votePercentage = 51; // of eligible players
	int reminderInterval = 120;
	int cooldownDuration = 30;
	int mapStartDelay = 30;
};

// The ballot itself, whoever opened it.
struct MapVoteCfg
{
	bool enabled = true;
	int mapsToShow = 6;
	int voteDuration = 90;
	int minWinPercentage = 0; // of cast votes, else runoff
	bool runoffEnabled = true;
	int countdownInterval = 15;
	bool chatChoiceReminder = true;
	int chatChoiceInterval = 15;
	bool enableRevote = true;
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
	int maxExtends = 0;        // per map, 0 = unlimited (the convar max still applies)
	int roundTimeCap = 1440;   // raise the mp_roundtime max to this, 0 = leave the engine's
	std::string mode = "auto"; // "auto" | "roundtime" | "timelimit"
	std::string permission = "changemap";
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
	EndOfMapVoteCfg endOfMapVote;
	ExtendCfg extend;
	NominateCfg nominate;
	MapChooserCfg mapchooser;
	GeneralCfg general;
};

// Load/parse cfg/cs2rtv/core.cfg. Returns true on success.
bool RTV_LoadConfig(const char *path, RTVPluginConfig &config);

extern RTVPluginConfig g_RTVConfig;

#endif // _INCLUDE_RTV_CONFIG_H_
