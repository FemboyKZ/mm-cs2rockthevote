#ifndef _INCLUDE_RTV_MENU_BRIDGE_H_
#define _INCLUDE_RTV_MENU_BRIDGE_H_

// Shows RTV menus through the mm-cs2menus plugin (ICS2Menus). Without it there are no menus.

#include "interfaces/cs2menus/menus_client.h"

#include <functional>
#include <string>
#include <vector>

using MenuItemCallback = std::function<void(int slot)>;

struct RTVMenuItem
{
	std::string text;
	MenuItemCallback callback;
	bool disabled = false; // greyed out, not selectable
};

struct RTVMenuDef
{
	std::string title;
	std::vector<RTVMenuItem> items;
	float duration = 0.0f; // 0 = no timeout
	bool exitButton = true;
	bool closeOnSelect = true;
	MenuItemCallback onExit; // closed with the exit option
	// mm-cs2menus panorama page labels skip map prefixes like "kz_", matching SortByName in nominate.cpp.
	bool mapList = false;

	void AddItem(const std::string &text, MenuItemCallback cb, bool disabled = false)
	{
		items.push_back({text, std::move(cb), disabled});
	}
};

class RTVMenuBridge
{
public:
	// Try to acquire the ICS2Menus interface. Call from AllPluginsLoaded().
	void Init();
	// Re-resolve the interface. Call from OnPluginLoad / OnPluginUnload.
	void Refresh();
	// Cancel any shown menus and drop the pointer. Call from Unload().
	void Shutdown();

	// True if the menu plugin is available.
	bool Available() const;

	// True if `slot`'s open menu renders as a chat (numbered) menu, so the "type a number in chat" hint applies.
	bool UsesChatInput(int slot);

	// Tells the player when mm-cs2menus is missing.
	void ShowMenu(int slot, const RTVMenuDef &def);
	void CloseMenu(int slot);
	bool HasMenu(int slot);

private:
	CS2MenusClient m_menus;
};

extern RTVMenuBridge g_RTVMenus;

#endif // _INCLUDE_RTV_MENU_BRIDGE_H_
