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
	// The panorama table's: under its headings, and listed in a popup by a button at the row's end.
	std::vector<std::string> cells;
	std::vector<std::string> details;
};

// A heading of the panorama table: the first is over the items' text, each one after it over `cells` of their cells.
struct RTVMenuColumn
{
	std::string label;
	int cells = 0;
	int sort = 0; // an arrow: 1 up, -1 down
};

// A panorama filter: a click lists the options, the selected one again clears it.
struct RTVMenuChip
{
	std::string label;
	std::vector<std::string> options;
	int selected = -1;
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
	std::vector<RTVMenuChip> chips;
	std::function<void(int slot, int chip, int selected)> onChip;
	std::string emptyText; // in place of the items when there are none
	// Panorama: mm-cs2menus' table layout.
	bool table = false;
	std::vector<RTVMenuColumn> columns;
	std::function<void(int slot, int column)> onColumn;
	// A section's name and its first item.
	std::vector<std::pair<std::string, size_t>> sections;

	// Holds the items added after it.
	void AddSection(const std::string &name)
	{
		sections.push_back({name, items.size()});
	}

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
	// True if a menu shown to `slot` now would be a panorama window.
	bool UsesPanorama(int slot);

	// Tells the player when mm-cs2menus is missing.
	void ShowMenu(int slot, const RTVMenuDef &def);
	void CloseMenu(int slot);
	bool HasMenu(int slot);

	// False for a player whose menus aren't panorama windows.
	bool ShowNotice(int slot, const std::string &title, const std::string &text, const std::string &hint, float seconds, MenuItemCallback onMouse1);
	void HideNotice(int slot);

private:
	CS2MenusClient m_menus;
};

extern RTVMenuBridge g_RTVMenus;

#endif // _INCLUDE_RTV_MENU_BRIDGE_H_
