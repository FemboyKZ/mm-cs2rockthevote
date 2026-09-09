#include "admin_bridge.h"
#include "mmu/log.h"
#include "src/common.h"

static mmu::AdminAccess s_admin("cs2rtv");

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
	return s_admin.HasFlag(slot, flag);
}

bool RTV_AdminBridge_CanUseCommand(int slot, const char *commandName, uint32_t defaultFlag)
{
	return s_admin.CanUseCommand(slot, commandName, defaultFlag);
}
