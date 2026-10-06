#include "menu_bridge.h"
#include "mmu/log.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/utils/print_utils.h"

#include <vector>

RTVMenuBridge g_RTVMenus;

void RTVMenuBridge::Init()
{
	Refresh();
	if (!m_menus)
	{
		MMU_LOG_WARN("mm-cs2menus not found - votes and map menus are unavailable.\n");
	}
}

void RTVMenuBridge::Refresh()
{
	switch (m_menus.Refresh())
	{
		case mmu::BridgeChange::Unloaded:
			MMU_LOG_WARN("mm-cs2menus unloaded - votes and map menus are unavailable.\n");
			break;
		case mmu::BridgeChange::Loaded:
			MMU_LOG_INFO("mm-cs2menus found - menus enabled.\n");
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
	// "default" resolves per viewer, so ask what actually rendered.
	return m_menus && m_menus->HasMenu(slot) && m_menus->GetActiveMenuType(slot) == MenuType::Chat;
}

void RTVMenuBridge::ShowMenu(int slot, const RTVMenuDef &def)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	if (!m_menus)
	{
		RTV_PrintToChatT(slot, "Menus need the mm-cs2menus plugin.");
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
		m_menus->SetMenuTextFeatures(h, kMenuTextIndex);
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
	}
}

bool RTVMenuBridge::HasMenu(int slot)
{
	return m_menus && m_menus->HasMenu(slot);
}
