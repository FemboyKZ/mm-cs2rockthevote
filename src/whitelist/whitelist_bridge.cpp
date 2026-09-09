#include "whitelist_bridge.h"
#include "mmu/interface_bridge.h"
#include "mmu/log.h"
#include "src/common.h"

static mmu::InterfaceBridge<ICS2Whitelist> s_whitelist(CS2WHITELIST_INTERFACE);

void RTV_WhitelistBridge_Init()
{
	if (s_whitelist.Refresh() == mmu::BridgeChange::Loaded)
	{
		MMU_LOG_INFO("mm-cs2whitelist found - RTV restricted to whitelisted players.\n");
	}
	else
	{
		MMU_LOG_WARN("mm-cs2whitelist not found - RTV available to all players.\n");
	}
}

void RTV_WhitelistBridge_Refresh()
{
	switch (s_whitelist.Refresh())
	{
		case mmu::BridgeChange::Loaded:
			MMU_LOG_INFO("mm-cs2whitelist loaded - RTV restricted to whitelisted players.\n");
			break;
		case mmu::BridgeChange::Unloaded:
			MMU_LOG_INFO("mm-cs2whitelist unloaded - RTV available to all players.\n");
			break;
		case mmu::BridgeChange::Unchanged:
			break;
	}
}

void RTV_WhitelistBridge_Shutdown()
{
	s_whitelist.Shutdown();
}

bool RTV_WhitelistBridge_Available()
{
	return s_whitelist.Available();
}

bool RTV_WhitelistBridge_IsPlayerAllowed(int slot)
{
	// Console always passes.
	if (slot < 0)
	{
		return true;
	}

	// No whitelist plugin loaded -> permissive (RTV stays open to all players).
	if (!s_whitelist)
	{
		return true;
	}

	// Confirmed-allowed cache short-circuits the full check.
	if (s_whitelist->IsPlayerWhitelistCached(slot))
	{
		return true;
	}

	// Confirmed-rejected players are about to be kicked.
	if (s_whitelist->IsPlayerBlacklisted(slot))
	{
		return false;
	}

	return s_whitelist->IsPlayerWhitelisted(slot);
}
