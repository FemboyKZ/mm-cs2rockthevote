#include "menu_bridge.h"
#include "mmu/log.h"
#include "src/common.h"
#include "src/config/config.h"

#include <vector>

RTVMenuBridge g_RTVMenus;

void RTVMenuBridge::Init()
{
	Refresh();
}

void RTVMenuBridge::Refresh()
{
	switch (m_menus.Refresh())
	{
		case mmu::BridgeChange::Unloaded:
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
	m_menus.Shutdown();
}

bool RTVMenuBridge::Available() const
{
	return m_menus.Available();
}

bool RTVMenuBridge::UsesChatInput(int slot)
{
	// The fallback in-plugin menu is always a chat menu.
	if (!m_menus)
	{
		return true;
	}
	// "default" resolves per viewer, so ask what actually rendered.
	return m_menus->HasMenu(slot) && m_menus->GetActiveMenuType(slot) == MenuType::Chat;
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
	if (def.mapList)
	{
		m_menus->SetMenuStyle(h, MenuStyle::PagePrefixDelimiter, "_");
	}

	g_RTVConfig.menu.ApplyKeys(m_menus.Get(), h);

	m_menus.Present(slot, h, def.duration,
					[onExit = def.onExit](MenuHandle, int s, MenuEndReason reason)
					{
						if (reason == MenuEndReason::Exit && onExit)
						{
							onExit(s);
						}
					});
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
