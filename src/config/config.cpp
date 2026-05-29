#include "config.h"
#include "vendor/mm-cs2admin/src/config/kv_parser.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

RTVPluginConfig g_RTVConfig;

static std::string ToLower(const std::string &s)
{
	std::string r = s;
	std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return r;
}

static std::string ResolveColorTags(const std::string &input)
{
	struct ColorTag
	{
		const char *tag;
		const char *code;
	};

	static const ColorTag tags[] = {
		{"{default}", "\x01"}, {"{darkred}", "\x02"}, {"{purple}", "\x03"},   {"{green}", "\x04"},    {"{olive}", "\x05"}, {"{lime}", "\x06"},
		{"{red}", "\x07"},     {"{grey}", "\x08"},    {"{yellow}", "\x09"},   {"{bluegrey}", "\x0A"}, {"{blue}", "\x0B"},  {"{darkblue}", "\x0C"},
		{"{grey2}", "\x0D"},   {"{orchid}", "\x0E"},  {"{lightred}", "\x0F"}, {"{gold}", "\x10"},
	};
	std::string out = input;
	for (auto &t : tags)
	{
		size_t pos;
		while ((pos = out.find(t.tag)) != std::string::npos)
		{
			out.replace(pos, strlen(t.tag), t.code);
		}
	}
	return out;
}

static void ConfigHandler(const std::string &section, const std::string &key, const std::string &value, void *userdata)
{
	RTVPluginConfig *cfg = static_cast<RTVPluginConfig *>(userdata);
	std::string sec = ToLower(section);
	std::string k = ToLower(key);

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
		else if (k == "mapchangedelay")
		{
			cfg->rtv.mapChangeDelay = std::atoi(value.c_str());
		}
		else if (k == "cooldownduration")
		{
			cfg->rtv.cooldownDuration = std::atoi(value.c_str());
		}
		else if (k == "mapstartdelay")
		{
			cfg->rtv.mapStartDelay = std::atoi(value.c_str());
		}
		else if (k == "endofmapvote")
		{
			cfg->rtv.endOfMapVote = (value != "0");
		}
		else if (k == "endofmapvotetime")
		{
			cfg->rtv.endOfMapVoteTime = std::atoi(value.c_str());
		}
	}
	else if (sec == "mapvote")
	{
		if (k == "enabled")
		{
			cfg->mapvote.enabled = (value != "0");
		}
		else if (k == "mapstoshow")
		{
			cfg->mapvote.mapsToShow = std::atoi(value.c_str());
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
		else if (k == "enablerevote")
		{
			cfg->mapvote.enableRevote = (value != "0");
		}
	}
	else if (sec == "nominate")
	{
		if (k == "enabled")
		{
			cfg->nominate.enabled = (value != "0");
		}
		else if (k == "nominatelimit")
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
		if (k == "commands")
		{
			cfg->mapchooser.commands = value;
		}
		else if (k == "permission")
		{
			cfg->mapchooser.permission = value;
		}
	}
	else if (sec == "general")
	{
		if (k == "chatprefix")
		{
			cfg->general.chatPrefix = ResolveColorTags(value);
		}
		else if (k == "includespectator")
		{
			cfg->general.includeSpectator = (value != "0");
		}
		else if (k == "adminpermission")
		{
			cfg->general.adminPermission = value;
		}
		else if (k == "enablemapvalidation")
		{
			cfg->general.enableMapValidation = (value != "0");
		}
		else if (k == "steamapikey")
		{
			cfg->general.steamApiKey = value;
		}
		else if (k == "discordwebhook")
		{
			cfg->general.discordWebhook = value;
		}
		else if (k == "kztiermode")
		{
			cfg->general.kzTierMode = value;
		}
		else if (k == "commandprefix")
		{
			cfg->general.commandPrefix = value;
		}
		else if (k == "silentcommandprefix")
		{
			cfg->general.silentCommandPrefix = value;
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

	kv::ParseSection(file, root.value, ConfigHandler, &config);
	return true;
}
