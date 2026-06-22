#include "menu_bridge.h"
#include "src/common.h"
#include "src/config/config.h"

#include "vendor/mm-cs2menus/src/public/ics2menus.h"

#include <vector>

RTVMenuBridge g_RTVMenus;

// Map the RTV config's MenuType to the plugin enum.
// "default" (and anything unknown) delegates the style choice to mm-cs2menus' own config.
static MenuType ConfiguredMenuType()
{
	const std::string &type = g_RTVConfig.general.menuType;
	if (type == "chat")
	{
		return MenuType::Chat;
	}
	if (type == "html")
	{
		return MenuType::Html;
	}
	return MenuType::Default;
}

// Map an RTV config key name to a MenuButton. "default" (and anything unknown)
// returns MenuButton::Default, which tells the menu plugin to use its own binding.
static MenuButton ParseMenuButton(const std::string &name)
{
	if (name == "none" || name == "off")
	{
		return MenuButton::None; // disable this action (single-key scroll)
	}
	if (name == "w" || name == "forward")
	{
		return MenuButton::W;
	}
	if (name == "s" || name == "back")
	{
		return MenuButton::S;
	}
	if (name == "a" || name == "left" || name == "moveleft")
	{
		return MenuButton::A;
	}
	if (name == "d" || name == "right" || name == "moveright")
	{
		return MenuButton::D;
	}
	if (name == "e" || name == "use" || name == "interact")
	{
		return MenuButton::Use;
	}
	if (name == "shift" || name == "speed" || name == "walk")
	{
		return MenuButton::Speed;
	}
	if (name == "ctrl" || name == "duck" || name == "crouch")
	{
		return MenuButton::Duck;
	}
	if (name == "space" || name == "jump")
	{
		return MenuButton::Jump;
	}
	if (name == "r" || name == "reload")
	{
		return MenuButton::Reload;
	}
	if (name == "mouse1" || name == "attack")
	{
		return MenuButton::Attack;
	}
	if (name == "mouse2" || name == "attack2")
	{
		return MenuButton::Attack2;
	}
	if (name == "tab" || name == "score")
	{
		return MenuButton::Score;
	}
	return MenuButton::Default; // "default" / unknown -> inherit menu plugin's key
}

void RTVMenuBridge::Init()
{
	Refresh();
}

void RTVMenuBridge::Refresh()
{
	ICS2Menus *prev = m_pMenus;
	ICS2Menus *now = nullptr;

	if (g_SMAPI)
	{
		now = static_cast<ICS2Menus *>(g_SMAPI->MetaFactory(CS2MENUS_INTERFACE, nullptr, nullptr));
	}

	// If the menu plugin unloaded, our handles belong to a now-dead instance.
	if (prev && !now)
	{
		for (int i = 0; i <= MAXPLAYERS; i++)
		{
			m_extHandle[i] = kInvalidMenuHandle;
		}
		META_CONPRINTF("[CS2RTV] mm-cs2menus unloaded - using built-in chat menus.\n");
	}
	else if (!prev && now)
	{
		META_CONPRINTF("[CS2RTV] mm-cs2menus found - menu rendering delegated to it.\n");
	}

	m_pMenus = now;
}

void RTVMenuBridge::Shutdown()
{
	if (m_pMenus)
	{
		// Cancel everything we displayed so the menu plugin doesn't keep lambdas
		// that capture our (about-to-unload) code.
		// CancelMenu fires the end callback, which clears the handle and destroys the menu.
		for (int i = 0; i <= MAXPLAYERS; i++)
		{
			if (m_extHandle[i] != kInvalidMenuHandle)
			{
				m_pMenus->CancelMenu(i);
			}
		}
	}

	m_pMenus = nullptr;
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		m_extHandle[i] = kInvalidMenuHandle;
	}
}

bool RTVMenuBridge::Available() const
{
	return m_pMenus != nullptr;
}

void RTVMenuBridge::ShowMenu(int slot, const ChatMenuDef &def, float curtime)
{
	if (!m_pMenus)
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

	MenuHandle h = m_pMenus->CreateMenu(ConfiguredMenuType(), def.title.c_str(),
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
		m_pMenus->AddItem(h, item.text.c_str(), "", item.disabled);
	}
	m_pMenus->SetExitButton(h, def.exitButton);
	m_pMenus->SetCloseOnSelect(h, def.closeOnSelect);

	// Apply RTV's configured HTML nav-key overrides.
	// MenuButton::Default delegates back to the menu plugin's own binding.
	m_pMenus->SetMenuKey(h, MenuNavAction::Up, ParseMenuButton(g_RTVConfig.general.menuNavUp));
	m_pMenus->SetMenuKey(h, MenuNavAction::Down, ParseMenuButton(g_RTVConfig.general.menuNavDown));
	m_pMenus->SetMenuKey(h, MenuNavAction::Select, ParseMenuButton(g_RTVConfig.general.menuNavSelect));
	m_pMenus->SetMenuKey(h, MenuNavAction::Back, ParseMenuButton(g_RTVConfig.general.menuNavBack));

	// One-shot: free the menu when the display ends, and forget the handle.
	m_pMenus->SetMenuEndCallback(h,
								 [this](MenuHandle menu, int s, MenuEndReason)
								 {
									 if (s >= 0 && s <= MAXPLAYERS && m_extHandle[s] == menu)
									 {
										 m_extHandle[s] = kInvalidMenuHandle;
									 }
									 if (m_pMenus)
									 {
										 m_pMenus->DestroyMenu(menu);
									 }
								 });

	// Record before DisplayMenu: displaying replaces any current menu for the slot
	// and fires its end callback, which must not clear the handle we just set.
	m_extHandle[slot] = h;
	m_pMenus->DisplayMenu(h, slot, def.duration);
}

void RTVMenuBridge::CloseMenu(int slot)
{
	if (m_pMenus)
	{
		m_pMenus->CancelMenu(slot);
		return;
	}
	g_ChatMenus.CloseMenu(slot);
}

bool RTVMenuBridge::HasMenu(int slot)
{
	if (m_pMenus)
	{
		return m_pMenus->HasMenu(slot);
	}
	return g_ChatMenus.HasMenu(slot);
}

bool RTVMenuBridge::ProcessInput(int slot, const char *text, float curtime)
{
	// The external plugin drives its own input (it hooks "say" itself),
	// so there's nothing for us to consume in that case.
	if (m_pMenus)
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
