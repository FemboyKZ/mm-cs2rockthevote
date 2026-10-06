#include "menu_bridge.h"
#include "utils/log.h"
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

bool RTVMenuBridge::UsesPanorama(int slot)
{
	return slot >= 0 && slot <= MAXPLAYERS && m_menus && m_menus->GetSlotMenuType(slot, g_RTVConfig.menu.Type()) == MenuType::Panorama;
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

	size_t section = 0;
	for (size_t i = 0; i < def.items.size(); i++)
	{
		const RTVMenuItem &item = def.items[i];
		if (section < def.sections.size() && def.sections[section].second == i)
		{
			m_menus->AddSection(h, def.sections[section++].first.c_str());
		}
		int index = m_menus->AddItem(h, item.text.c_str(), "", item.disabled);
		auto pointers = [](const std::vector<std::string> &strings)
		{
			std::vector<const char *> out;
			for (const std::string &s : strings)
			{
				out.push_back(s.c_str());
			}
			return out;
		};
		if (!item.cells.empty())
		{
			m_menus->SetItemCells(h, index, pointers(item.cells).data(), static_cast<int>(item.cells.size()));
		}
		if (!item.details.empty())
		{
			m_menus->SetItemDetails(h, index, pointers(item.details).data(), static_cast<int>(item.details.size()));
		}
	}
	for (const RTVMenuChip &chip : def.chips)
	{
		std::vector<const char *> options;
		for (const std::string &option : chip.options)
		{
			options.push_back(option.c_str());
		}
		m_menus->AddMenuChip(h, chip.label.c_str(), options.data(), static_cast<int>(options.size()), chip.selected);
	}
	if (def.onChip)
	{
		m_menus->SetMenuChipCallback(h, [onChip = def.onChip](MenuHandle, int s, int chip, int selected) { onChip(s, chip, selected); });
	}
	if (!def.emptyText.empty())
	{
		m_menus->SetMenuEmpty(h, def.emptyText.c_str(), "", false);
	}
	if (def.table)
	{
		m_menus->SetMenuLayout(h, MenuLayout::Table);
	}
	for (const RTVMenuColumn &column : def.columns)
	{
		m_menus->AddMenuColumn(h, column.label.c_str(), column.cells, column.sort);
	}
	if (def.onColumn)
	{
		m_menus->SetMenuColumnCallback(h, [onColumn = def.onColumn](MenuHandle, int s, int column) { onColumn(s, column); });
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

bool RTVMenuBridge::ShowNotice(int slot, const std::string &title, const std::string &text, const std::string &hint, float seconds,
							   MenuItemCallback onMouse1)
{
	if (!UsesPanorama(slot))
	{
		return false;
	}
	return m_menus.ShowNotice(slot, title.c_str(), text.c_str(), hint.c_str(), seconds, std::move(onMouse1));
}

void RTVMenuBridge::HideNotice(int slot)
{
	if (m_menus)
	{
		m_menus.HideNotice(slot);
	}
}
