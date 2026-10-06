#include "map_menu.h"
#include "menu_bridge.h"
#include "src/lang/translations.h"
#include "src/utils/print_utils.h"

#include <algorithm>
#include <vector>

void RTV_ShowMapMenu(int slot, const MapMenu &menu, const MapMenuView &view)
{
	const auto &maps = g_MapLister.GetMaps();
	if (maps.empty())
	{
		RTV_PrintToChatT(slot, "No maps in the map list.");
		return;
	}

	RTVMenuDef def;
	def.title = RTV_Translate(slot, menu.title.c_str());
	def.exitButton = true;
	def.closeOnSelect = true;
	def.mapList = true;
	def.table = g_RTVMenus.UsesPanorama(slot);

	auto hasTier = [](const std::vector<int> &tiers, int tier) { return tier == 0 || std::find(tiers.begin(), tiers.end(), tier) != tiers.end(); };
	std::vector<const MapEntry *> sorted;
	sorted.reserve(maps.size());
	for (const auto &e : maps)
	{
		const bool listed = menu.only.empty() || std::find(menu.only.begin(), menu.only.end(), e.mapName) != menu.only.end();
		if (listed && hasTier(e.classicTiers, view.classicTier) && hasTier(e.vanillaTiers, view.vanillaTier))
		{
			sorted.push_back(&e);
		}
	}
	SortMapsByName(sorted);

	auto tierOf = [&view](const MapEntry *e)
	{
		const std::vector<int> &tiers = view.sort == 1 ? e->classicTiers : e->vanillaTiers;
		return tiers.empty() ? 0 : tiers[0];
	};
	if (view.sort != 0)
	{
		// By name within a tier. Maps without one go last either way.
		std::stable_sort(sorted.begin(), sorted.end(),
						 [&](const MapEntry *a, const MapEntry *b)
						 {
							 const int x = tierOf(a);
							 const int y = tierOf(b);
							 if ((x == 0) != (y == 0))
							 {
								 return y == 0;
							 }
							 return view.descending ? x > y : x < y;
						 });
	}
	else if (view.descending)
	{
		std::reverse(sorted.begin(), sorted.end());
	}

	const int tierCells = g_MapLister.TierCells();
	if (def.table)
	{
		def.emptyText = RTV_Translate(slot, "No maps match the filter.");

		bool classic = false;
		bool vanilla = false;
		MapLister::TierModes(classic, vanilla);
		auto arrow = [&view](int sort) { return view.sort != sort ? 0 : view.descending ? -1 : 1; };
		// What each heading sorts by, as MapMenuView::sort. Chip n is heading n + 1's mode.
		std::vector<int> sorts = {0};
		def.columns.push_back({RTV_Translate(slot, "Map"), 0, arrow(0)});
		for (int mode = 1; mode <= 2; mode++)
		{
			if (!(mode == 1 ? classic : vanilla))
			{
				continue;
			}
			const char *label = mode == 1 ? "CKZ" : "VNL";
			def.columns.push_back({label, tierCells, arrow(mode)});
			sorts.push_back(mode);

			RTVMenuChip chip;
			chip.label = label;
			for (int tier = 1; tier <= 10; tier++)
			{
				chip.options.push_back(std::to_string(tier));
			}
			chip.selected = (mode == 1 ? view.classicTier : view.vanillaTier) - 1;
			def.chips.push_back(std::move(chip));
		}
		// A second click on the sorted heading turns the order around.
		def.onColumn = [menu, view, sorts](int playerSlot, int column)
		{
			MapMenuView next = view;
			next.descending = view.sort == sorts[column] && !view.descending;
			next.sort = sorts[column];
			RTV_ShowMapMenu(playerSlot, menu, next);
		};
		def.onChip = [menu, view, sorts](int playerSlot, int chip, int selected)
		{
			MapMenuView next = view;
			(sorts[chip + 1] == 1 ? next.classicTier : next.vanillaTier) = selected + 1;
			RTV_ShowMapMenu(playerSlot, menu, next);
		};
	}

	int section = -1;
	for (const MapEntry *entry : sorted)
	{
		const MapEntry &e = *entry;
		bool disabled = false;
		const std::string mark = menu.mark ? menu.mark(slot, e, disabled) : std::string();
		std::vector<std::string> cells;
		// Disabled rows render grey (\x08), and so does the text after the tier.
		std::string label =
			def.table ? g_MapLister.GetNameAndCells(e, tierCells, cells) : g_MapLister.GetDisplayLabel(e, true, disabled ? "\x08" : "\x01");
		// Sorted by tier, the table's keys are the tiers.
		if (view.sort != 0 && tierOf(entry) != section)
		{
			section = tierOf(entry);
			def.AddSection(section != 0 ? std::to_string(section) : "-");
		}
		// A copy, since the list can reallocate or be cleared while the menu is open.
		def.AddItem(label + mark, [onPick = menu.onPick, copy = e](int playerSlot) { onPick(playerSlot, copy); }, disabled);
		if (def.table)
		{
			def.items.back().cells = std::move(cells);
			def.items.back().details = g_MapLister.GetTierDetails(e);
		}
	}

	g_RTVMenus.ShowMenu(slot, def);
}
