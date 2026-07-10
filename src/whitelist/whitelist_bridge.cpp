#include "whitelist_bridge.h"
#include "mmu/log.h"
#include "src/common.h"

static ICS2Whitelist *s_pWhitelist = nullptr;

void RTV_WhitelistBridge_Init()
{
	s_pWhitelist = nullptr;

	if (!g_SMAPI)
	{
		return;
	}

	void *iface = g_SMAPI->MetaFactory(CS2WHITELIST_INTERFACE, nullptr, nullptr);
	if (iface)
	{
		s_pWhitelist = static_cast<ICS2Whitelist *>(iface);
		MMU_LOG_INFO("mm-cs2whitelist found - RTV restricted to whitelisted players.\n");
	}
	else
	{
		MMU_LOG_WARN("mm-cs2whitelist not found - RTV available to all players.\n");
	}
}

void RTV_WhitelistBridge_Refresh()
{
	// Called when any sibling plugin loads or unloads.
	// Drop the cached pointer unconditionally and re-resolve.
	ICS2Whitelist *prev = s_pWhitelist;
	s_pWhitelist = nullptr;

	if (!g_SMAPI)
	{
		return;
	}

	void *iface = g_SMAPI->MetaFactory(CS2WHITELIST_INTERFACE, nullptr, nullptr);
	if (iface)
	{
		s_pWhitelist = static_cast<ICS2Whitelist *>(iface);
		if (!prev)
		{
			MMU_LOG_INFO("mm-cs2whitelist loaded - RTV restricted to whitelisted players.\n");
		}
	}
	else if (prev)
	{
		MMU_LOG_INFO("mm-cs2whitelist unloaded - RTV available to all players.\n");
	}
}

void RTV_WhitelistBridge_Shutdown()
{
	s_pWhitelist = nullptr;
}

bool RTV_WhitelistBridge_Available()
{
	return s_pWhitelist != nullptr;
}

bool RTV_WhitelistBridge_IsPlayerAllowed(int slot)
{
	// Console always passes.
	if (slot < 0)
	{
		return true;
	}

	// No whitelist plugin loaded -> permissive (RTV stays open to all players).
	if (!s_pWhitelist)
	{
		return true;
	}

	// Confirmed-allowed cache short-circuits the full check.
	if (s_pWhitelist->IsPlayerWhitelistCached(slot))
	{
		return true;
	}

	// Confirmed-rejected players are about to be kicked.
	if (s_pWhitelist->IsPlayerBlacklisted(slot))
	{
		return false;
	}

	return s_pWhitelist->IsPlayerWhitelisted(slot);
}
