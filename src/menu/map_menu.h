#ifndef _INCLUDE_RTV_MAP_MENU_H_
#define _INCLUDE_RTV_MAP_MENU_H_

// The map list as a menu: !nominate's, the matches of a partial name and !mapmenu's.
// A panorama viewer gets a table: the tiers in columns, a tier filter per mode and sorting by a click on a heading.

#include "src/maplist/map_lister.h"

#include <functional>
#include <string>
#include <vector>

struct MapMenu
{
	std::string title; // a phrase key
	// Just these maps by name, all of them when empty.
	std::vector<std::string> only;
	// What follows a map's name, and whether its row can be picked. Optional.
	std::function<std::string(int slot, const MapEntry &e, bool &disabled)> mark;
	// `e` is the row's copy of the entry, as it was when the menu was built.
	std::function<void(int slot, const MapEntry &e)> onPick;
};

struct MapMenuView
{
	// Only maps with a course of this tier, 0 for any.
	int classicTier = 0;
	int vanillaTier = 0;
	// 0 by name, 1 by a map's first CKZ tier, 2 by its first VNL one.
	int sort = 0;
	bool descending = false;
};

void RTV_ShowMapMenu(int slot, const MapMenu &menu, const MapMenuView &view = {});

#endif // _INCLUDE_RTV_MAP_MENU_H_
