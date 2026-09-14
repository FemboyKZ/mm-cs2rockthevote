#ifndef _INCLUDE_RTV_CHATMENU_H_
#define _INCLUDE_RTV_CHATMENU_H_

#include "src/common.h"

#include <functional>
#include <string>
#include <vector>

static constexpr int MENU_ITEMS_PER_PAGE = 7;

using MenuItemCallback = std::function<void(int slot)>;

struct ChatMenuItem
{
	std::string text;
	MenuItemCallback callback;
	bool disabled = false; // greyed out, not selectable
};

struct ChatMenuDef
{
	std::string title;
	std::vector<ChatMenuItem> items;
	float duration = 0.0f; // 0 = no timeout
	bool exitButton = true;
	bool closeOnSelect = true;
	MenuItemCallback onExit; // closed with the exit option

	void AddItem(const std::string &text, MenuItemCallback cb, bool disabled = false)
	{
		items.push_back({text, std::move(cb), disabled});
	}
};

// Per-player active menu state
struct PlayerMenu
{
	bool active = false;
	ChatMenuDef def;
	int page = 0;
	float expireTime = 0.0f; // absolute game time; 0 = no expire
};

class ChatMenuHandler
{
public:
	// Show a menu to a player. Replaces any existing active menu.
	void ShowMenu(int slot, const ChatMenuDef &def, float curtime);

	// Close a player's active menu without triggering any callback.
	void CloseMenu(int slot);

	// Process a chat input string from a player.
	// Returns true if the input was consumed (was a valid menu selection).
	bool ProcessInput(int slot, const char *text, float curtime);

	// Expire menus whose timeout has elapsed. Called from GameFrame.
	void Tick(float curtime);

	// Called when a player disconnects to clean up their menu state.
	void OnPlayerDisconnect(int slot);

	bool HasMenu(int slot) const;

private:
	PlayerMenu m_menus[MAXPLAYERS + 1];

	// Send the current page render to the player.
	void RenderPage(int slot, float curtime);
};

extern ChatMenuHandler g_ChatMenus;

#endif // _INCLUDE_RTV_CHATMENU_H_
