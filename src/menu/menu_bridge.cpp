#include "menu_bridge.h"
#include "mmu/log.h"
#include "src/common.h"
#include "src/config/config.h"

#include "interfaces/cs2menus/ics2menus.h"

#include <vector>

RTVMenuBridge g_RTVMenus;

RTVMenuBridge::RTVMenuBridge() : m_menus(CS2MENUS_INTERFACE) {}

void RTVMenuBridge::Init()
{
	Refresh();
}

void RTVMenuBridge::Refresh()
{
	switch (m_menus.Refresh())
	{
		case mmu::BridgeChange::Unloaded:
			// Handles belonged to the unloaded instance.
			for (int i = 0; i <= MAXPLAYERS; i++)
			{
				m_extHandle[i] = kInvalidMenuHandle;
			}
			MMU_LOG_INFO("mm-cs2menus unloaded - using built-in chat menus.\n");
			break;
		case mmu::BridgeChange::Loaded:
			MMU_LOG_INFO("mm-cs2menus found - menu rendering delegated to it.\n");
			break;
		case mmu::BridgeChange::Unchanged:
			break;
	}
}

void RTVMenuBridge::Shutdown()
{
	if (m_menus)
	{
		// Cancel everything we displayed so the menu plugin doesn't keep lambdas that capture our (about-to-unload) code.
		// CancelMenu fires the end callback, which clears the handle and destroys the menu.
		for (int i = 0; i <= MAXPLAYERS; i++)
		{
			if (m_extHandle[i] != kInvalidMenuHandle)
			{
				m_menus->CancelMenu(i);
			}
		}
	}

	m_menus.Shutdown();
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		m_extHandle[i] = kInvalidMenuHandle;
	}
}

bool RTVMenuBridge::Available() const
{
	return m_menus.Available();
}

bool RTVMenuBridge::UsesChatInput() const
{
	// The fallback in-plugin menu is always a chat menu.
	if (!m_menus)
	{
		return true;
	}
	// Default delegates to the menu plugin's own config, which may resolve to chat per viewer,
	// so only suppress the hint when HTML is forced.
	return g_RTVConfig.menu.Type() != MenuType::Html;
}

void RTVMenuBridge::ShowMenu(int slot, const ChatMenuDef &def, float curtime)
{
	if (!m_menus)
	{
		g_ChatMenus.ShowMenu(slot, def, curtime);
		return;
	}
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}

	// Copy the per-item callbacks so the menu plugin can invoke them after this call returns.
	// The select callback receives an absolute item index matching the order we AddItem them.
	std::vector<MenuItemCallback> callbacks;
	callbacks.reserve(def.items.size());
	for (const auto &item : def.items)
	{
		callbacks.push_back(item.callback);
	}

	MenuHandle h = m_menus->CreateMenu(g_RTVConfig.menu.Type(), def.title.c_str(),
									   [callbacks](MenuHandle, int s, int item)
									   {
										   if (item >= 0 && item < static_cast<int>(callbacks.size()) && callbacks[item])
										   {
											   callbacks[item](s);
										   }
									   });
	if (h == kInvalidMenuHandle)
	{
		return;
	}

	for (const auto &item : def.items)
	{
		m_menus->AddItem(h, item.text.c_str(), "", item.disabled);
	}
	m_menus->SetExitButton(h, def.exitButton);
	m_menus->SetCloseOnSelect(h, def.closeOnSelect);

	g_RTVConfig.menu.ApplyKeys(m_menus.Get(), h);

	// One-shot: free the menu when the display ends, and forget the handle.
	m_menus->SetMenuEndCallback(h,
								[this](MenuHandle menu, int s, MenuEndReason)
								{
									if (s >= 0 && s <= MAXPLAYERS && m_extHandle[s] == menu)
									{
										m_extHandle[s] = kInvalidMenuHandle;
									}
									if (m_menus)
									{
										m_menus->DestroyMenu(menu);
									}
								});

	// Record before DisplayMenu:
	// displaying replaces any current menu for the slot and fires its end callback, which must not clear the handle we just set.
	m_extHandle[slot] = h;
	m_menus->DisplayMenu(h, slot, def.duration);
}

void RTVMenuBridge::CloseMenu(int slot)
{
	if (m_menus)
	{
		m_menus->CancelMenu(slot);
		return;
	}
	g_ChatMenus.CloseMenu(slot);
}

bool RTVMenuBridge::HasMenu(int slot)
{
	if (m_menus)
	{
		return m_menus->HasMenu(slot);
	}
	return g_ChatMenus.HasMenu(slot);
}

bool RTVMenuBridge::ProcessInput(int slot, const char *text, float curtime)
{
	// The external plugin drives its own input (it hooks "say" itself),
	// so there's nothing for us to consume in that case.
	if (m_menus)
	{
		return false;
	}
	return g_ChatMenus.ProcessInput(slot, text, curtime);
}

void RTVMenuBridge::Tick(float curtime)
{
	// Only the built-in backend needs ticking, the external plugin ticks itself.
	g_ChatMenus.Tick(curtime);
}

void RTVMenuBridge::OnPlayerDisconnect(int slot)
{
	// The external plugin cleans up disconnects via its own ClientDisconnect hook.
	g_ChatMenus.OnPlayerDisconnect(slot);
}
