#ifndef _INCLUDE_RTV_MAP_LISTER_H_
#define _INCLUDE_RTV_MAP_LISTER_H_

#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct MapEntry
{
	std::string displayName; // Full display name, e.g. "kz_grotto (T3, Linear)"
	std::string mapName;     // Clean name for changelevel, e.g. "kz_grotto"
	std::string workshopId;  // Workshop ID if present, else empty
	bool isWorkshop = false;
	// CS2KZ nub_tier range across all courses (1-10, 0 = unknown).
	// min == max for single-course maps.
	int classicTierMin = 0;
	int classicTierMax = 0;
	int vanillaTierMin = 0;
	int vanillaTierMax = 0;
};

// CS2KZ nub_tier ranges for a map, cached by clean map name.
struct TierRange
{
	int classicMin = 0;
	int classicMax = 0;
	int vanillaMin = 0;
	int vanillaMax = 0;
};

class MapLister
{
public:
	// Load maps from file. Returns number of maps loaded, or -1 on error.
	// If the file doesn't exist, triggers auto-generate from the CS2KZ API.
	int LoadFromFile(const char *path);

	// Reload using the last used path.
	int Reload();

	// Dynamically add a map (from API lookup / off-maplist nomination).
	// Does not write to disk. Returns pointer to the entry.
	const MapEntry *AddDynamicMap(const MapEntry &entry);

	const std::vector<MapEntry> &GetMaps() const
	{
		return m_maps;
	}

	// Find exact match by display name or map name (case-insensitive).
	const MapEntry *FindExact(const std::string &name) const;

	// Find exact match by workshop ID.
	const MapEntry *FindByWorkshopId(const std::string &workshopId) const;

	// Find all maps whose display/map name contains the query string.
	std::vector<const MapEntry *> FindMatching(const std::string &query) const;

	// Resolve a user input string to a single map (exact first, then partial).
	// Returns nullptr if no match or multiple matches (caller should show menu).
	// outMatches is populated with all partial matches when nullptr is returned.
	const MapEntry *Resolve(const std::string &input, std::vector<const MapEntry *> *outMatches) const;

	bool IsLoaded() const
	{
		return !m_maps.empty();
	}

	// Player-facing label for a map: displayName (or mapName) plus the CS2KZ tier
	// annotation ("CKZ: x VNL: x") when DisplayKzTiers is enabled.
	// When colorize is true the tier values are wrapped in chat color codes.
	// resetColor is the color restored after each colored tier value, so trailing
	// text keeps the surrounding row color (e.g. "\x08" grey for disabled rows).
	std::string GetDisplayLabel(const MapEntry &e, bool colorize = true, const char *resetColor = "\x01") const;

	// Async API lookups (run on background thread; callback on same thread).
	// DO NOT call game engine APIs from the callback - set a flag and handle on next GameFrame tick.

	// Look up a map by workshop ID via CS2KZ API, then Steam fallback.
	// callback(entry) where entry.mapName is empty on failure.
	void LookupByWorkshopIdAsync(const std::string &workshopId, std::function<void(MapEntry)> callback) const;

	// Look up a map by name via CS2KZ API.
	void LookupByNameAsync(const std::string &name, std::function<void(MapEntry)> callback) const;

	// Fetch all approved maps from CS2KZ API and write maplist.txt.
	// Called automatically when LoadFromFile() returns missing file.
	void GenerateMaplistAsync(const std::string &outputPath) const;

	// Validate all workshop maps via Steam API.
	// Dead maps are reported to server console and optionally Discord webhook.
	void ValidateMapsAsync() const;

	// Fetch classic+vanilla tiers for all approved maps from the CS2KZ API into
	// the tier cache, then apply them to currently loaded maps. Async.
	void FetchTiersAsync();

private:
	std::vector<MapEntry> m_maps;
	std::string m_lastPath;

	// Cache of CS2KZ tiers keyed by lowercased clean map name -> {classic, vanilla}.
	// Populated by FetchTiersAsync(); read/written on the game thread only.
	std::unordered_map<std::string, TierRange> m_tierCache;

	// True while a tier fetch is in progress. Prevents overlapping API sweeps
	// when maps change faster than a fetch completes. Set/cleared from both the
	// game thread and the HTTP worker thread, hence atomic.
	std::atomic<bool> m_tierFetchInFlight {false};

	// Fill an entry's tiers from m_tierCache if not already set.
	void ApplyCachedTiers(MapEntry &e) const;

	// Paginate the CS2KZ approved-maps endpoint, parsing tiers into each entry.
	// onComplete is invoked on a BACKGROUND thread with the full list.
	static void FetchAllApprovedMapsAsync(std::function<void(std::vector<MapEntry>)> onComplete);

	// Parse a single line into a MapEntry. Returns false if line should be
	// skipped.
	static bool ParseLine(const std::string &line, MapEntry &out);

	// Strip the annotation part: "kz_grotto (T3)" -> "kz_grotto"
	static std::string StripAnnotation(const std::string &displayName);

	// Build a MapEntry from a CS2KZ API JSON map object string fragment.
	// Returns false if parsing failed.
	static bool ParseCS2KZMapJson(const std::string &jsonObj, MapEntry &out);
};

extern MapLister g_MapLister;

#endif // _INCLUDE_RTV_MAP_LISTER_H_
