#ifndef _INCLUDE_RTV_WHITELIST_BRIDGE_H_
#define _INCLUDE_RTV_WHITELIST_BRIDGE_H_

// Optional integration with mm-cs2whitelist (ICS2Whitelist002 interface).
// If mm-cs2whitelist is not loaded at runtime, all checks return true so that RTV behaves normally on non-whitelisted servers.

#include "vendor/mm-cs2whitelist/src/public/ics2whitelist.h"

// Call once in AllPluginsLoaded() to try to acquire the ICS2Whitelist interface.
void RTV_WhitelistBridge_Init();

// Re-resolve the interface. Call from IMetamodListener::OnPluginLoad and
// OnPluginUnload so we never hold a dangling pointer into a freed plugin.
void RTV_WhitelistBridge_Refresh();

// Drop any cached interface pointer. Call from plugin Unload().
void RTV_WhitelistBridge_Shutdown();

// Returns true if the whitelist plugin is loaded.
bool RTV_WhitelistBridge_Available();

// Returns true if the player at this slot is allowed to use RTV-related commands.
// When the whitelist plugin is not loaded this always returns true.
// When loaded, the player must pass the whitelist check (or be in the confirmed-allowed cache for this map).
// Console (slot < 0) always passes.
bool RTV_WhitelistBridge_IsPlayerAllowed(int slot);

#endif // _INCLUDE_RTV_WHITELIST_BRIDGE_H_
