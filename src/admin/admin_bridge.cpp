#include "admin_bridge.h"
#include "mmu/log.h"
#include "src/common.h"

static ICS2Admin *s_pAdmin = nullptr;

void RTV_AdminBridge_Init()
{
	s_pAdmin = nullptr;

	if (!g_SMAPI)
	{
		return;
	}

	// g_SMAPI->MetaFactory() searches all plugin factories by interface name.
	void *iface = g_SMAPI->MetaFactory(CS2ADMIN_INTERFACE, nullptr, nullptr);
	if (iface)
	{
		s_pAdmin = static_cast<ICS2Admin *>(iface);
		MMU_LOG_INFO("mm-cs2admin found - admin bridge active.\n");
	}
	else
	{
		MMU_LOG_WARN("mm-cs2admin not found - admin commands will be blocked.\n");
	}
}

void RTV_AdminBridge_Refresh()
{
	// Called when any sibling plugin loads or unloads.
	// Drop the cached pointer unconditionally and re-resolve
	ICS2Admin *prev = s_pAdmin;
	s_pAdmin = nullptr;

	if (!g_SMAPI)
	{
		return;
	}

	void *iface = g_SMAPI->MetaFactory(CS2ADMIN_INTERFACE, nullptr, nullptr);
	if (iface)
	{
		s_pAdmin = static_cast<ICS2Admin *>(iface);
		if (!prev)
		{
			MMU_LOG_INFO("mm-cs2admin loaded - admin bridge active.\n");
		}
	}
	else if (prev)
	{
		MMU_LOG_INFO("mm-cs2admin unloaded - admin commands will be blocked.\n");
	}
}

void RTV_AdminBridge_Shutdown()
{
	s_pAdmin = nullptr;
}

bool RTV_AdminBridge_Available()
{
	return s_pAdmin != nullptr;
}

uint32_t RTV_ParseFlagName(const std::string &name)
{
	// Full named-string map matching CS2AdminFlag enum values
	if (name == "reservation")
	{
		return CS2ADMIN_FLAG_RESERVATION;
	}
	if (name == "generic")
	{
		return CS2ADMIN_FLAG_GENERIC;
	}
	if (name == "kick")
	{
		return CS2ADMIN_FLAG_KICK;
	}
	if (name == "ban")
	{
		return CS2ADMIN_FLAG_BAN;
	}
	if (name == "unban")
	{
		return CS2ADMIN_FLAG_UNBAN;
	}
	if (name == "slay")
	{
		return CS2ADMIN_FLAG_SLAY;
	}
	if (name == "changemap")
	{
		return CS2ADMIN_FLAG_CHANGEMAP;
	}
	if (name == "convars")
	{
		return CS2ADMIN_FLAG_CONVARS;
	}
	if (name == "config")
	{
		return CS2ADMIN_FLAG_CONFIG;
	}
	if (name == "chat")
	{
		return CS2ADMIN_FLAG_CHAT;
	}
	if (name == "vote")
	{
		return CS2ADMIN_FLAG_VOTE;
	}
	if (name == "password")
	{
		return CS2ADMIN_FLAG_PASSWORD;
	}
	if (name == "rcon")
	{
		return CS2ADMIN_FLAG_RCON;
	}
	if (name == "cheats")
	{
		return CS2ADMIN_FLAG_CHEATS;
	}
	if (name == "custom1")
	{
		return CS2ADMIN_FLAG_CUSTOM1;
	}
	if (name == "custom2")
	{
		return CS2ADMIN_FLAG_CUSTOM2;
	}
	if (name == "custom3")
	{
		return CS2ADMIN_FLAG_CUSTOM3;
	}
	if (name == "custom4")
	{
		return CS2ADMIN_FLAG_CUSTOM4;
	}
	if (name == "custom5")
	{
		return CS2ADMIN_FLAG_CUSTOM5;
	}
	if (name == "custom6")
	{
		return CS2ADMIN_FLAG_CUSTOM6;
	}
	if (name == "root")
	{
		return CS2ADMIN_FLAG_ROOT;
	}
	// Single SourceMod letter a-z: a=(1<<0) ... y=(1<<24), z=root
	if (name.size() == 1)
	{
		char c = (char)tolower((unsigned char)name[0]);
		if (c >= 'a' && c <= 'z')
		{
			return (c == 'z') ? CS2ADMIN_FLAG_ROOT : (1u << (c - 'a'));
		}
	}
	return CS2ADMIN_FLAG_ROOT;
}

bool RTV_AdminBridge_HasFlag(int slot, uint32_t flag)
{
	// Console always passes
	if (slot < 0)
	{
		return true;
	}

	// Admin plugin not loaded - deny access (restrictive fallback)
	if (!s_pAdmin)
	{
		return false;
	}

	return s_pAdmin->HasFlag(slot, flag);
}

bool RTV_AdminBridge_CanUseCommand(int slot, const char *commandName, uint32_t defaultFlag)
{
	// Console always passes
	if (slot < 0)
	{
		return true;
	}

	// Admin plugin not loaded. Open commands (defaultFlag 0) stay open,
	// commands with a configured flag stay blocked (restrictive fallback).
	if (!s_pAdmin)
	{
		return defaultFlag == 0;
	}

	return s_pAdmin->CanUseCommand(slot, commandName, "cs2rtv", defaultFlag);
}
