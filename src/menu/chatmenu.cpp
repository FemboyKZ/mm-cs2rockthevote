#include "chatmenu.h"
#include "src/lang/translations.h"
#include "src/utils/print_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

ChatMenuHandler g_ChatMenus;

static bool IsNumericInput(const char *text, int &outNum)
{
	// Accept "1", "2", ... "9", "0" - optionally with whitespace
	// Also accept "!1", "!2", etc.
	const char *p = text;
	while (*p == ' ' || *p == '\t')
	{
		p++;
	}
	if (!*p)
	{
		return false;
	}

	// Strip optional leading '!' or '/' (command prefix chars used by some plugins)
	if (*p == '!' || *p == '/')
	{
		p++;
	}

	// At most 2 chars (0-9 or "10" etc. we don't need beyond 9)
	if (!isdigit(static_cast<unsigned char>(*p)))
	{
		return false;
	}

	char *end = nullptr;
	long v = strtol(p, &end, 10);
	if (end == p)
	{
		return false;
	}

	// Remainder must be whitespace
	while (*end == ' ' || *end == '\t')
	{
		end++;
	}
	if (*end != '\0')
	{
		return false;
	}

	outNum = static_cast<int>(v);
	return true;
}

void ChatMenuHandler::ShowMenu(int slot, const ChatMenuDef &def, float curtime)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}

	PlayerMenu &pm = m_menus[slot];
	pm.active = true;
	pm.def = def;
	pm.page = 0;
	pm.expireTime = (def.duration > 0.0f) ? (curtime + def.duration) : 0.0f;

	RenderPage(slot, curtime);
}

void ChatMenuHandler::CloseMenu(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	m_menus[slot].active = false;
}

bool ChatMenuHandler::ProcessInput(int slot, const char *text, float curtime)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}

	PlayerMenu &pm = m_menus[slot];
	if (!pm.active)
	{
		return false;
	}

	// Strip the outer quotes that CS2's say command wraps around the message
	std::string msg(text);
	if (msg.size() >= 2 && msg.front() == '"' && msg.back() == '"')
	{
		msg = msg.substr(1, msg.size() - 2);
	}

	int num;
	if (!IsNumericInput(msg.c_str(), num))
	{
		return false;
	}

	const auto &items = pm.def.items;
	int pageCount = (static_cast<int>(items.size()) + MENU_ITEMS_PER_PAGE - 1) / MENU_ITEMS_PER_PAGE;
	bool hasMore = (pm.page + 1 < pageCount);
	bool hasPrev = (pm.page > 0);

	int pageStart = pm.page * MENU_ITEMS_PER_PAGE;
	int pageEnd = (std::min)(pageStart + MENU_ITEMS_PER_PAGE, static_cast<int>(items.size()));
	int pageItems = pageEnd - pageStart;

	// Navigation key layout:
	//   Items occupy slots 1 .. pageItems
	//   Next page  : MENU_ITEMS_PER_PAGE + 1 (8)  - only if there is a next page
	//   Prev page  : MENU_ITEMS_PER_PAGE + 2 (9)  - only if there is a prev page
	//   Exit       : 0                             - always when exitButton=true

	if (num == 0 && pm.def.exitButton)
	{
		pm.active = false;
		return true; // consume but no callback
	}

	if (num == MENU_ITEMS_PER_PAGE + 1 && hasMore)
	{
		pm.page++;
		RenderPage(slot, curtime);
		return true;
	}

	if (num == MENU_ITEMS_PER_PAGE + 2 && hasPrev)
	{
		pm.page--;
		RenderPage(slot, curtime);
		return true;
	}

	// Regular item selection
	if (num >= 1 && num <= pageItems)
	{
		int idx = pageStart + (num - 1);
		const ChatMenuItem &item = items[idx];

		if (item.disabled)
		{
			// Re-render so the player can try again
			RenderPage(slot, curtime);
			return true;
		}

		if (pm.def.closeOnSelect)
		{
			pm.active = false;
		}

		if (item.callback)
		{
			item.callback(slot);
		}

		return true;
	}

	// Number typed but didn't match anything - consume so it doesn't appear in
	// chat
	return true;
}

void ChatMenuHandler::Tick(float curtime)
{
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		PlayerMenu &pm = m_menus[i];
		if (!pm.active)
		{
			continue;
		}
		if (pm.expireTime > 0.0f && curtime >= pm.expireTime)
		{
			pm.active = false;
		}
	}
}

void ChatMenuHandler::OnPlayerDisconnect(int slot)
{
	CloseMenu(slot);
}

bool ChatMenuHandler::HasMenu(int slot) const
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}
	return m_menus[slot].active;
}

// Rendering
void ChatMenuHandler::RenderPage(int slot, float /*curtime*/)
{
	const PlayerMenu &pm = m_menus[slot];
	if (!pm.active)
	{
		return;
	}

	const auto &def = pm.def;
	const auto &items = def.items;

	int pageCount = (static_cast<int>(items.size()) + MENU_ITEMS_PER_PAGE - 1) / MENU_ITEMS_PER_PAGE;
	if (pageCount == 0)
	{
		pageCount = 1;
	}

	bool hasMore = (pm.page + 1 < pageCount);
	bool hasPrev = (pm.page > 0);

	int pageStart = pm.page * MENU_ITEMS_PER_PAGE;
	int pageEnd = (std::min)(pageStart + MENU_ITEMS_PER_PAGE, static_cast<int>(items.size()));
	int pageItems = pageEnd - pageStart;

	// Title row
	char buf[256];
	if (pageCount > 1)
	{
		snprintf(buf, sizeof(buf), "\x04-- %s \x01(page %d/%d) --", def.title.c_str(), pm.page + 1, pageCount);
	}
	else
	{
		snprintf(buf, sizeof(buf), "\x04-- %s --", def.title.c_str());
	}

	// Send each line separately so CS2 doesn't clip them
	RTV_PrintToChat(slot, "%s", buf);

	for (int i = 0; i < pageItems; i++)
	{
		const ChatMenuItem &item = items[pageStart + i];
		if (item.disabled)
		{
			snprintf(buf, sizeof(buf), "\x08#%d %s", i + 1, item.text.c_str());
		}
		else
		{
			snprintf(buf, sizeof(buf), "\x01#%d %s", i + 1, item.text.c_str());
		}
		RTV_PrintToChat(slot, "%s", buf);
	}

	if (hasMore)
	{
		snprintf(buf, sizeof(buf), "\x01#%d \x04-> %s", MENU_ITEMS_PER_PAGE + 1, RTV_Translate(slot, "Next Page").c_str());
		RTV_PrintToChat(slot, "%s", buf);
	}
	if (hasPrev)
	{
		snprintf(buf, sizeof(buf), "\x01#%d \x04-> %s", MENU_ITEMS_PER_PAGE + 2, RTV_Translate(slot, "Previous Page").c_str());
		RTV_PrintToChat(slot, "%s", buf);
	}
	if (def.exitButton)
	{
		snprintf(buf, sizeof(buf), "\x01#0 \x04-> %s", RTV_Translate(slot, "Exit").c_str());
		RTV_PrintToChat(slot, "%s", buf);
	}
}
