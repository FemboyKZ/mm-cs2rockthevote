#include "config.h"
#include "utils/chat_colors.h"
#include "utils/str.h"
#include "utils/kv_parser.h"
#include "utils/log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

RTVPluginConfig g_RTVConfig;

static void ConfigHandler(const std::string &section, const std::string &key, const std::string &value, void *userdata)
{
	RTVPluginConfig *cfg = static_cast<RTVPluginConfig *>(userdata);
	std::string sec = str::ToLower(section);
	std::string k = str::ToLower(key);

	if (sec == "rtv")
	{
		if (k == "enabled")
		{
			cfg->rtv.enabled = (value != "0");
		}
		else if (k == "votepercentage")
		{
			cfg->rtv.votePercentage = std::atoi(value.c_str());
		}
		else if (k == "reminderinterval")
		{
			cfg->rtv.reminderInterval = std::atoi(value.c_str());
		}
		else if (k == "cooldown" || k == "cooldownduration")
		{
			cfg->rtv.cooldownDuration = std::atoi(value.c_str());
		}
		else if (k == "mapstartdelay")
		{
			cfg->rtv.mapStartDelay = std::atoi(value.c_str());
		}
		// Legacy keys, kept so an existing core.cfg keeps working.
		else if (k == "mapchangedelay")
		{
			cfg->mapvote.mapChangeDelay = std::atoi(value.c_str());
		}
		else if (k == "endofmapvote")
		{
			cfg->endOfMapVote.enabled = (value != "0");
		}
		else if (k == "endofmapvotetime")
		{
			cfg->endOfMapVote.triggerTime = std::atoi(value.c_str());
		}
	}
	else if (sec == "endofmapvote")
	{
		if (k == "enabled")
		{
			cfg->endOfMapVote.enabled = (value != "0");
		}
		else if (k == "triggertime")
		{
			cfg->endOfMapVote.triggerTime = std::atoi(value.c_str());
		}
	}
	else if (sec == "mapvote")
	{
		if (k == "enabled")
		{
			cfg->mapvote.enabled = (value != "0");
		}
		else if (k == "options" || k == "mapstoshow")
		{
			cfg->mapvote.mapsToShow = std::atoi(value.c_str());
		}
		else if (k == "excluderecentmaps")
		{
			cfg->mapvote.excludeRecentMaps = std::atoi(value.c_str());
		}
		else if (k == "voteduration")
		{
			cfg->mapvote.voteDuration = std::atoi(value.c_str());
		}
		else if (k == "minwinpercentage")
		{
			cfg->mapvote.minWinPercentage = std::atoi(value.c_str());
		}
		else if (k == "runoffenabled")
		{
			cfg->mapvote.runoffEnabled = (value != "0");
		}
		else if (k == "countdowninterval")
		{
			cfg->mapvote.countdownInterval = std::atoi(value.c_str());
		}
		else if (k == "chatchoicereminder")
		{
			cfg->mapvote.chatChoiceReminder = (value != "0");
		}
		else if (k == "chatchoiceinterval")
		{
			cfg->mapvote.chatChoiceInterval = std::atoi(value.c_str());
		}
		else if (k == "revoteenabled" || k == "enablerevote")
		{
			cfg->mapvote.enableRevote = (value != "0");
		}
		else if (k == "votenotice")
		{
			cfg->mapvote.voteNotice = (value != "0");
		}
		else if (k == "mapchangedelay")
		{
			cfg->mapvote.mapChangeDelay = std::atoi(value.c_str());
		}
		else if (k == "workshopdownloadtimeout")
		{
			cfg->mapvote.workshopDownloadTimeout = std::atoi(value.c_str());
		}
	}
	else if (sec == "extend")
	{
		if (k == "enabled")
		{
			cfg->extend.enabled = (value != "0");
		}
		else if (k == "minutes")
		{
			cfg->extend.minutes = std::atoi(value.c_str());
		}
		else if (k == "max" || k == "maxextends")
		{
			cfg->extend.maxExtends = std::atoi(value.c_str());
		}
		else if (k == "roundtimecap")
		{
			cfg->extend.roundTimeCap = std::atoi(value.c_str());
		}
		else if (k == "mode")
		{
			cfg->extend.mode = str::ToLower(value);
		}
		else if (k == "permission")
		{
			cfg->extend.permission = value;
		}
	}
	else if (sec == "nominate")
	{
		if (k == "enabled")
		{
			cfg->nominate.enabled = (value != "0");
		}
		else if (k == "max" || k == "nominatelimit")
		{
			cfg->nominate.nominateLimit = std::atoi(value.c_str());
		}
		else if (k == "permission")
		{
			cfg->nominate.permission = value;
		}
		else if (k == "externalnominatepermission")
		{
			cfg->nominate.externalNominatePermission = value;
		}
	}
	else if (sec == "mapchooser")
	{
		if (k == "permission")
		{
			cfg->mapchooser.permission = value;
		}
	}
	else if (sec == "general")
	{
		if (k == "chatprefix")
		{
			cfg->general.chatPrefix = mmu::ResolveColorTags(value);
		}
		else if (k == "includespectator")
		{
			cfg->general.includeSpectator = (value != "0");
		}
		else if (k == "adminpermission")
		{
			cfg->general.adminPermission = value;
		}
		else if (k == "kzapimaplist")
		{
			cfg->general.kzApiMaplist = (value != "0");
		}
		else if (k == "mapvalidationenabled" || k == "enablemapvalidation")
		{
			cfg->general.enableMapValidation = (value != "0");
		}
		else if (k == "steamapikey")
		{
			cfg->general.steamApiKey = value;
		}
		// Old name.
		else if (k == "discordwebhook")
		{
			cfg->general.discordWebhook = value;
		}
		else if (k == "displaykztiers")
		{
			cfg->general.displayKzTiers = value;
		}
		else if (k == "kztierformat")
		{
			cfg->general.kzTierFormat = value;
		}
		else if (mmu::config::ApplyGeneralKey(cfg->general, k, value) || mmu::config::ApplyLogKey(cfg->log, k, value))
		{
			// consumed
		}
		// Old MenuType and MenuNav* keys.
		else if (cfg->menu.ApplyKey(k, value))
		{
			// consumed
		}
	}
	else if (sec == "discord")
	{
		if (k == "webhookurl")
		{
			cfg->general.discordWebhook = value;
		}
	}
	else if (sec == "menu")
	{
		cfg->menu.ApplyKey(k, value);
	}
	else if (sec == "permissions")
	{
		if (k == "extend")
		{
			cfg->extend.permission = value;
		}
		else if (k == "nominate")
		{
			cfg->nominate.permission = value;
		}
		else if (k == "nominateexternal")
		{
			cfg->nominate.externalNominatePermission = value;
		}
		else if (k == "mapchooser")
		{
			cfg->mapchooser.permission = value;
		}
		else if (k == "admin")
		{
			cfg->general.adminPermission = value;
		}
	}
}

bool RTV_LoadConfig(const char *path, RTVPluginConfig &config)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		return false;
	}

	// Expect: "cs2rockthevote" { ... }
	kv::Token root = kv::NextToken(file);
	if (root.kind != kv::TokenType::String)
	{
		return false;
	}

	kv::Token brace = kv::NextToken(file);
	if (brace.kind != kv::TokenType::OpenBrace)
	{
		return false;
	}

	// Fresh, so a removed key reverts to its default.
	RTVPluginConfig fresh;
	if (!kv::ParseSection(file, root.value, ConfigHandler, &fresh))
	{
		return false;
	}

	fresh.mapvote.voteDuration = (std::max)(fresh.mapvote.voteDuration, 1);
	fresh.rtv.votePercentage = (std::min)(fresh.rtv.votePercentage, 100);

	config = std::move(fresh);
	mmu::config::ApplyLogBlock(config.log);
	return true;
}
