#ifndef _INCLUDE_RTV_ADMIN_BRIDGE_H_
#define _INCLUDE_RTV_ADMIN_BRIDGE_H_

// Optional integration with mm-cs2admin (ICS2Admin002 interface).
// If mm-cs2admin is not loaded at runtime, all HasFlag calls return false.

#include "vendor/mm-cs2admin/src/public/ics2admin.h"
#include <cstdint>
#include <string>

// Call once in AllPluginsLoaded() to try to acquire the ICS2Admin interface.
void RTV_AdminBridge_Init();

// Re-resolve the interface. Call from IMetamodListener::OnPluginLoad and
// OnPluginUnload so we never hold a dangling pointer into a freed plugin.
void RTV_AdminBridge_Refresh();

// Drop any cached interface pointer. Call from plugin Unload().
void RTV_AdminBridge_Shutdown();

// Returns true if the admin plugin is available.
bool RTV_AdminBridge_Available();

// Returns true if slot has the given flag (or root).
// Returns false if the admin plugin is not loaded (restrictive: no plugin = no
// access). slot < 0 = server console - always returns true.
bool RTV_AdminBridge_HasFlag(int slot, uint32_t flag);

// Converts a flag name string to a bitmask.
// Accepts: named strings ("changemap", "root", "reservation"),
// single SourceMod letters ("a"-"z"), or defaults to root for unknown input.
uint32_t RTV_ParseFlagName(const std::string &name);

#endif // _INCLUDE_RTV_ADMIN_BRIDGE_H_
