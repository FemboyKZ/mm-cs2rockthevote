#include "admin_bridge.h"
#include "mmu/interface_bridge.h"
#include "mmu/log.h"
#include "src/common.h"

static mmu::InterfaceBridge<ICS2Admin> s_admin(CS2ADMIN_INTERFACE);

void RTV_AdminBridge_Init()
{
	if (s_admin.Refresh() == mmu::BridgeChange::Loaded)
	{
		MMU_LOG_INFO("mm-cs2admin found - admin bridge active.\n");
	}
	else
	{
		MMU_LOG_WARN("mm-cs2admin not found - admin commands will be blocked.\n");
	}
}

void RTV_AdminBridge_Refresh()
{
	switch (s_admin.Refresh())
	{
		case mmu::BridgeChange::Loaded:
			MMU_LOG_INFO("mm-cs2admin loaded - admin bridge active.\n");
			break;
		case mmu::BridgeChange::Unloaded:
			MMU_LOG_INFO("mm-cs2admin unloaded - admin commands will be blocked.\n");
			break;
		case mmu::BridgeChange::Unchanged:
			break;
	}
}

void RTV_AdminBridge_Shutdown()
{
	s_admin.Shutdown();
}

bool RTV_AdminBridge_Available()
{
	return s_admin.Available();
}

bool RTV_AdminBridge_HasFlag(int slot, uint32_t flag)
{
	// Console always passes
	if (slot < 0)
	{
		return true;
	}

	// Admin plugin not loaded - deny access (restrictive fallback)
	if (!s_admin)
	{
		return false;
	}

	return s_admin->HasFlag(slot, flag);
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
	if (!s_admin)
	{
		return defaultFlag == 0;
	}

	return s_admin->CanUseCommand(slot, commandName, "cs2rtv", defaultFlag);
}
