#ifndef _INCLUDE_RTV_MENU_BRIDGE_H_
#define _INCLUDE_RTV_MENU_BRIDGE_H_

// Routes RTV menus through the mm-cs2menus plugin (ICS2Menus) when it's loaded,
// and falls back to the in-plugin chat menu (g_ChatMenus) when it isn't.
//
// Call sites build the same ChatMenuDef as before and call g_RTVMenus instead of g_ChatMenus,
// this bridge picks the backend per call.

#include "chatmenu.h"

#include <cstdint>

class ICS2Menus;

class RTVMenuBridge
{
public:
	// Try to acquire the ICS2Menus interface. Call from AllPluginsLoaded().
	void Init();
	// Re-resolve the interface. Call from OnPluginLoad / OnPluginUnload.
	void Refresh();
	// Cancel any externally-shown menus and drop the pointer. Call from Unload().
	void Shutdown();

	// True if the external menu plugin is available.
	bool Available() const;

	// True if the vote menu renders as a chat (numbered) menu, so the "type a number in chat" hint applies.
	// False for HTML menus.
	bool UsesChatInput() const;

	// --- Mirrors ChatMenuHandler so call sites are a drop-in swap ---

	void ShowMenu(int slot, const ChatMenuDef &def, float curtime);
	void CloseMenu(int slot);
	bool HasMenu(int slot);
	// Chat input for the fallback backend. Returns true if consumed.
	// When the external plugin owns the menu it handles its own input, so this returns false.
	bool ProcessInput(int slot, const char *text, float curtime);
	void Tick(float curtime);
	void OnPlayerDisconnect(int slot);

private:
	ICS2Menus *m_pMenus = nullptr;
	// External menu handle currently displayed to each slot (0 = none).
	uint32_t m_extHandle[MAXPLAYERS + 1] = {};
};

extern RTVMenuBridge g_RTVMenus;

#endif // _INCLUDE_RTV_MENU_BRIDGE_H_
