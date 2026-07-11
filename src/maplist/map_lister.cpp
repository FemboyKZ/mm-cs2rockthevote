#include "map_lister.h"
#include "mmu/log.h"
#include "src/common.h"
#include "src/config/config.h"
#include "mmu/http_client.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

MapLister g_MapLister;

static std::string TrimStr(const std::string &s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	size_t end = s.find_last_not_of(" \t\r\n");
	if (start == std::string::npos)
	{
		return "";
	}
	return s.substr(start, end - start + 1);
}

static std::string ToLowerStr(const std::string &s)
{
	std::string r = s;
	std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return r;
}

// Returns true if DisplayKzTiers is set to anything other than off/none.
// outMode receives the lowercased config value.
static bool TierDisplayEnabled(std::string &outMode)
{
	outMode = ToLowerStr(g_RTVConfig.general.displayKzTiers);
	return !(outMode.empty() || outMode == "off" || outMode == "none" || outMode == "0");
}

// CS2KZ nub_tier name -> integer (1-10). Returns 0 for unknown.
static int TierNameToInt(const std::string &tierStr)
{
	if (tierStr == "very-easy")
	{
		return 1;
	}
	if (tierStr == "easy")
	{
		return 2;
	}
	if (tierStr == "medium")
	{
		return 3;
	}
	if (tierStr == "advanced")
	{
		return 4;
	}
	if (tierStr == "hard")
	{
		return 5;
	}
	if (tierStr == "very-hard")
	{
		return 6;
	}
	if (tierStr == "extreme")
	{
		return 7;
	}
	if (tierStr == "death")
	{
		return 8;
	}
	if (tierStr == "unfeasible")
	{
		return 9;
	}
	if (tierStr == "impossible")
	{
		return 10;
	}
	return 0;
}

// Integer tier (1-10) -> CS2KZ tier name.
static const char *IntToTierName(int tier)
{
	switch (tier)
	{
		case 1:
			return "very-easy";
		case 2:
			return "easy";
		case 3:
			return "medium";
		case 4:
			return "advanced";
		case 5:
			return "hard";
		case 6:
			return "very-hard";
		case 7:
			return "extreme";
		case 8:
			return "death";
		case 9:
			return "unfeasible";
		case 10:
			return "impossible";
		default:
			return "";
	}
}

// Render a tier per KzTierFormat: "text" -> tier name, otherwise the number.
static std::string FormatTier(int tier)
{
	if (ToLowerStr(g_RTVConfig.general.kzTierFormat) == "text")
	{
		return IntToTierName(tier);
	}
	return std::to_string(tier);
}

// Closest in-game chat color to the CS2KZ website tier palette
// (cs2kz-website src/utils/index.ts tierColorMap).
static const char *TierColorCode(int tier)
{
	switch (tier)
	{
		case 1:
			return CHAT_COLOR_LIME; // very-easy  #6bc96f
		case 2:
			return CHAT_COLOR_GREEN; // easy       #33bd3a
		case 3:
			return CHAT_COLOR_OLIVE; // medium     #d8e302
		case 4:
			return CHAT_COLOR_YELLOW; // advanced   #ffc107
		case 5:
			return CHAT_COLOR_GOLD; // hard       #e37910
		case 6:
			return CHAT_COLOR_LIGHTRED; // very-hard  #e34202
		case 7:
			return CHAT_COLOR_RED; // extreme    #e31c02
		case 8:
			return CHAT_COLOR_PURPLE; // death      #bb02db
		case 9:
			return CHAT_COLOR_ORCHID; // unfeasible #e800e1
		case 10:
			return CHAT_COLOR_GREY; // impossible #d1d1d1
		default:
			return CHAT_COLOR_DEFAULT;
	}
}

std::string MapLister::StripAnnotation(const std::string &displayName)
{
	// "kz_grotto (T3, Linear)" -> "kz_grotto"
	size_t pos = displayName.find(" (");
	if (pos != std::string::npos)
	{
		return displayName.substr(0, pos);
	}
	return displayName;
}

bool MapLister::ParseLine(const std::string &rawLine, MapEntry &out)
{
	std::string line = TrimStr(rawLine);

	// Skip blank lines and comments
	if (line.empty() || line[0] == '#' || line[0] == '/' || line[0] == ';')
	{
		return false;
	}

	// Format: "displayname:workshopid" or just "mapname"
	// The colon is used as the separator, but map names don't contain colons,
	// while workshop IDs are pure digits (e.g. "3070321829").
	// Edge-case: "kz_grotto (T3, Linear):3129698096"

	size_t colonPos = line.rfind(':');
	if (colonPos != std::string::npos)
	{
		std::string potentialId = TrimStr(line.substr(colonPos + 1));
		// Check if everything after the colon looks like a workshop ID (all digits)
		bool allDigits = !potentialId.empty() && std::all_of(potentialId.begin(), potentialId.end(), ::isdigit);

		if (allDigits)
		{
			out.displayName = TrimStr(line.substr(0, colonPos));
			out.workshopId = potentialId;
			out.mapName = StripAnnotation(out.displayName);
			out.isWorkshop = true;
			return true;
		}
	}

	// No workshop ID - plain map name
	out.displayName = line;
	out.workshopId = "";
	out.mapName = StripAnnotation(line);
	out.isWorkshop = false;
	return true;
}

int MapLister::LoadFromFile(const char *path)
{
	m_maps.clear();
	m_lastPath = path;

	FILE *fp = fopen(path, "r");
	if (!fp)
	{
		MMU_LOG_WARN("maplist.txt not found at '%s' - attempting "
					   "auto-generate from CS2KZ API.\n",
					   path);
		GenerateMaplistAsync(path);
		return -1;
	}

	char line[512];
	while (fgets(line, sizeof(line), fp))
	{
		MapEntry entry;
		if (ParseLine(std::string(line), entry))
		{
			m_maps.push_back(std::move(entry));
		}
	}

	fclose(fp);

	// Apply any already-cached CS2KZ tiers to the freshly loaded entries.
	for (auto &e : m_maps)
	{
		ApplyCachedTiers(e);
	}

	// Fetch CS2KZ tiers for display if enabled and not yet cached.
	std::string tierMode;
	if (TierDisplayEnabled(tierMode) && m_tierCache.empty())
	{
		FetchTiersAsync();
	}

	// Optionally validate workshop maps in background
	if (g_RTVConfig.general.enableMapValidation && !g_RTVConfig.general.steamApiKey.empty())
	{
		ValidateMapsAsync();
	}

	return static_cast<int>(m_maps.size());
}

int MapLister::Reload()
{
	if (m_lastPath.empty())
	{
		return -1;
	}
	return LoadFromFile(m_lastPath.c_str());
}

const MapEntry *MapLister::FindExact(const std::string &name) const
{
	std::string lower = ToLowerStr(name);
	for (const auto &entry : m_maps)
	{
		if (ToLowerStr(entry.displayName) == lower)
		{
			return &entry;
		}
		if (ToLowerStr(entry.mapName) == lower)
		{
			return &entry;
		}
	}
	return nullptr;
}

std::vector<const MapEntry *> MapLister::FindMatching(const std::string &query) const
{
	std::string lower = ToLowerStr(query);
	std::vector<const MapEntry *> results;
	for (const auto &entry : m_maps)
	{
		if (ToLowerStr(entry.displayName).find(lower) != std::string::npos || ToLowerStr(entry.mapName).find(lower) != std::string::npos)
		{
			results.push_back(&entry);
		}
	}
	return results;
}

const MapEntry *MapLister::Resolve(const std::string &input, std::vector<const MapEntry *> *outMatches) const
{
	// Exact match first
	const MapEntry *exact = FindExact(input);
	if (exact)
	{
		return exact;
	}

	// Partial matches
	std::vector<const MapEntry *> matches = FindMatching(input);
	if (matches.size() == 1)
	{
		return matches[0];
	}

	if (outMatches)
	{
		*outMatches = std::move(matches);
	}
	return nullptr;
}

const MapEntry *MapLister::FindByWorkshopId(const std::string &workshopId) const
{
	for (const auto &e : m_maps)
	{
		if (e.isWorkshop && e.workshopId == workshopId)
		{
			return &e;
		}
	}
	return nullptr;
}

const MapEntry *MapLister::AddDynamicMap(const MapEntry &entry)
{
	// Avoid duplicates
	const MapEntry *existing = FindExact(entry.mapName);
	if (!existing && !entry.workshopId.empty())
	{
		existing = FindByWorkshopId(entry.workshopId);
	}
	if (existing)
	{
		return existing;
	}

	m_maps.push_back(entry);
	return &m_maps.back();
}

// Minimal JSON helpers (no third-party deps)

// Extract the value of a JSON string field named `key` from a flat object.
// Handles only simple string values. Returns empty string if not found.
static std::string JsonGetString(const std::string &json, const char *key)
{
	// Pattern:  "key": "value"
	std::string search = "\"";
	search += key;
	search += "\"";
	size_t pos = json.find(search);
	if (pos == std::string::npos)
	{
		return "";
	}

	// Skip  "key"  :  whitespace
	pos += search.size();
	while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t'))
	{
		pos++;
	}

	if (pos >= json.size() || json[pos] != '"')
	{
		return "";
	}

	pos++; // skip opening quote
	std::string result;
	while (pos < json.size() && json[pos] != '"')
	{
		if (json[pos] == '\\' && pos + 1 < json.size())
		{
			pos++;
			switch (json[pos])
			{
				case '"':
					result += '"';
					break;
				case '\\':
					result += '\\';
					break;
				case 'n':
					result += '\n';
					break;
				case 'r':
					result += '\r';
					break;
				default:
					result += json[pos];
					break;
			}
		}
		else
		{
			result += json[pos];
		}
		pos++;
	}
	return result;
}

// Enumerate top-level array elements `[{...},{...}]` - calls cb for each object
// string.
static void JsonForEachObject(const std::string &json, std::function<void(const std::string &)> cb)
{
	// Find the opening '[' (skip leading whitespace / field name)
	size_t start = json.find('[');
	if (start == std::string::npos)
	{
		// Maybe the response is just a JSON object wrapper: {"maps":[...]}
		// Fall back to finding first '['
		return;
	}

	size_t pos = start + 1;
	int depth = 0;
	size_t objStart = std::string::npos;

	while (pos < json.size())
	{
		char c = json[pos];
		if (c == '{')
		{
			if (depth == 0)
			{
				objStart = pos;
			}
			depth++;
		}
		else if (c == '}')
		{
			depth--;
			if (depth == 0 && objStart != std::string::npos)
			{
				cb(json.substr(objStart, pos - objStart + 1));
				objStart = std::string::npos;
			}
		}
		else if (c == '"')
		{
			// Skip string contents
			pos++;
			while (pos < json.size() && json[pos] != '"')
			{
				if (json[pos] == '\\')
				{
					pos++;
				}
				pos++;
			}
		}
		else if (c == ']' && depth == 0)
		{
			break;
		}
		pos++;
	}
}

// Parse the nub_tier for one mode for EVERY course, in course order.
// modeKey must be the quoted key, e.g. "\"classic\"" or "\"vanilla\"".
// Appends each course's tier (1-10) to out; a course with no tier for this mode is skipped.
// API structure: map.courses[].filters.{vanilla|classic}.nub_tier (string)
static void ParseTierListForMode(const std::string &jsonObj, const char *modeKey, std::vector<int> &out)
{
	out.clear();

	size_t pos = 0;
	while (true)
	{
		size_t filtersPos = jsonObj.find("\"filters\"", pos);
		if (filtersPos == std::string::npos)
		{
			break;
		}
		size_t nextFilters = jsonObj.find("\"filters\"", filtersPos + 9);
		size_t bound = (nextFilters == std::string::npos) ? jsonObj.size() : nextFilters;
		pos = bound;

		size_t modePos = jsonObj.find(modeKey, filtersPos);
		if (modePos == std::string::npos || modePos >= bound)
		{
			continue;
		}
		size_t nubPos = jsonObj.find("\"nub_tier\"", modePos);
		if (nubPos == std::string::npos || nubPos >= bound)
		{
			continue;
		}
		size_t colon = jsonObj.find(':', nubPos + 10);
		if (colon == std::string::npos || colon >= bound)
		{
			continue;
		}
		size_t q1 = jsonObj.find('"', colon + 1);
		if (q1 == std::string::npos || q1 >= bound)
		{
			continue;
		}
		size_t q2 = jsonObj.find('"', q1 + 1);
		if (q2 == std::string::npos || q2 >= bound)
		{
			continue;
		}

		int tier = TierNameToInt(jsonObj.substr(q1 + 1, q2 - q1 - 1));
		if (tier > 0)
		{
			out.push_back(tier);
		}
	}
}

// CS2KZ API parsing
bool MapLister::ParseCS2KZMapJson(const std::string &jsonObj, MapEntry &out)
{
	// Expected fields: "name" (map name), "workshop_id" (string)
	// We also look into courses[].filters for nub_tier (all courses)
	std::string name = JsonGetString(jsonObj, "name");
	std::string wsId = JsonGetString(jsonObj, "workshop_id");

	if (name.empty())
	{
		return false;
	}

	out.mapName = name;
	out.workshopId = wsId;
	out.isWorkshop = !wsId.empty();

	// Parse per-course classic and vanilla tiers so DisplayKzTiers can list them.
	ParseTierListForMode(jsonObj, "\"classic\"", out.classicTiers);
	ParseTierListForMode(jsonObj, "\"vanilla\"", out.vanillaTiers);

	// Global maps come straight from the API with no baked tier annotation.
	// tiers are shown live via DisplayKzTiers / GetDisplayLabel.
	// Manual annotations on non-global maps live only in maplist.txt.
	out.displayName = name;

	return true;
}

void MapLister::LookupByWorkshopIdAsync(const std::string &workshopId, std::function<void(MapEntry)> callback) const
{
	// 1) Try CS2KZ API
	std::string cs2kzUrl = "https://api.cs2kz.org/maps?workshop_id=" + workshopId + "&state=approved";
	mmu::http::Get(
		cs2kzUrl,
		[workshopId, callback](bool ok, std::string body)
		{
			if (ok && !body.empty())
			{
				// CS2KZ returns an array; grab first object
				MapEntry found;
				bool parsed = false;
				JsonForEachObject(body,
								  [&](const std::string &obj)
								  {
									  if (!parsed && MapLister::ParseCS2KZMapJson(obj, found))
									  {
										  parsed = true;
									  }
								  });
				if (parsed)
				{
					// Dispatch to game thread: callback touches game state.
					MapEntry captured = std::move(found);
					mmu::http::QueueMainThread([callback, captured]() mutable { callback(std::move(captured)); });
					return;
				}
			}

			// 2) Fallback: Steam GetPublishedFileDetails
			std::string steamUrl = "https://api.steampowered.com/ISteamRemoteStorage/"
								   "GetPublishedFileDetails/v1/";
			const std::string &steamKey = g_RTVConfig.general.steamApiKey;
			if (!steamKey.empty())
			{
				steamUrl += "?key=" + steamKey;
			}
			std::string postBody = "itemcount=1&publishedfileids[0]=" + workshopId;
			// Steam's v1 endpoint uses POST with form-encoded data.
			mmu::http::PostForm(
				steamUrl, postBody,
				[workshopId, callback](bool ok2, std::string body2)
				{
					if (ok2 && !body2.empty())
					{
						// Response:
						// {"response":{"publishedfiledetails":[{"publishedfileid":"...","title":"...","result":1}]}}
						std::string title = JsonGetString(body2, "title");
						if (!title.empty())
						{
							MapEntry fallback;
							fallback.mapName = title;
							fallback.displayName = title;
							fallback.workshopId = workshopId;
							fallback.isWorkshop = true;
							MapEntry captured = std::move(fallback);
							mmu::http::QueueMainThread([callback, captured]() mutable { callback(std::move(captured)); });
							return;
						}
						MMU_LOG_INFO("Steam API returned no title for %s (result=9?), trying Workshop page.\n", workshopId.c_str());
					}

					// 3) Fallback: scrape the Steam Workshop page <title> tag.
					// The page title is "Steam Workshop::MAP NAME" for public items.
					std::string pageUrl = "https://steamcommunity.com/sharedfiles/filedetails?id=" + workshopId;
					mmu::http::Get(pageUrl,
								[workshopId, callback](bool ok3, std::string body3)
								{
									MapEntry fallback;
									if (ok3 && !body3.empty())
									{
										// Look for <title>Steam Workshop::MAP NAME</title>
										const std::string prefix = "Steam Workshop::";
										size_t p = body3.find(prefix);
										if (p != std::string::npos)
										{
											p += prefix.size();
											size_t end = body3.find('<', p);
											if (end == std::string::npos)
											{
												end = body3.size();
											}
											std::string title = body3.substr(p, end - p);
											// Trim trailing whitespace
											while (!title.empty()
												   && (title.back() == ' ' || title.back() == '\r' || title.back() == '\n' || title.back() == '\t'))
											{
												title.pop_back();
											}
											if (!title.empty())
											{
												fallback.mapName = title;
												fallback.displayName = title;
												fallback.workshopId = workshopId;
												fallback.isWorkshop = true;
												MMU_LOG_INFO("Workshop page title for %s: '%s'\n", workshopId.c_str(), title.c_str());
											}
										}
									}
									if (fallback.mapName.empty())
									{
										MMU_LOG_WARN("Workshop page lookup also failed for %s.\n", workshopId.c_str());
									}
									MapEntry captured = std::move(fallback);
									mmu::http::QueueMainThread([callback, captured]() mutable { callback(std::move(captured)); });
								});
				});
		});
}

void MapLister::LookupByNameAsync(const std::string &name, std::function<void(MapEntry)> callback) const
{
	std::string url = "https://api.cs2kz.org/maps?name=" + name + "&state=approved&limit=5";
	mmu::http::Get(url,
				[callback](bool ok, std::string body)
				{
					MapEntry found;
					if (ok && !body.empty())
					{
						JsonForEachObject(body,
										  [&](const std::string &obj)
										  {
											  if (found.mapName.empty())
											  {
												  MapLister::ParseCS2KZMapJson(obj, found);
											  }
										  });
					}
					// Dispatch to game thread: callback touches game state.
					MapEntry captured = std::move(found);
					mmu::http::QueueMainThread([callback, captured]() mutable { callback(std::move(captured)); });
				});
}

void MapLister::FetchAllApprovedMapsAsync(std::function<void(std::vector<MapEntry>)> onComplete)
{
	// Paginate CS2KZ API to get all approved maps.
	// We fetch page 0 first, then continue until we get an empty result.
	struct State
	{
		std::vector<MapEntry> collected;
		int offset = 0;
		std::function<void(std::vector<MapEntry>)> done;
	};

	auto state = std::make_shared<State>();
	state->done = std::move(onComplete);

	// Recursive lambda via shared_ptr to allow self-reference
	struct Fetcher
	{
		std::shared_ptr<State> st;

		void Fetch(std::shared_ptr<Fetcher> self)
		{
			std::string url = "https://api.cs2kz.org/maps?state=approved&limit=500&offset=" + std::to_string(st->offset);

			mmu::http::Get(url,
						[this, self](bool ok, std::string body) mutable
						{
							if (!ok || body.empty())
							{
								st->done(std::move(st->collected));
								return;
							}

							int countBefore = static_cast<int>(st->collected.size());
							JsonForEachObject(body,
											  [&](const std::string &obj)
											  {
												  MapEntry e;
												  if (MapLister::ParseCS2KZMapJson(obj, e))
												  {
													  st->collected.push_back(std::move(e));
												  }
											  });

							int added = static_cast<int>(st->collected.size()) - countBefore;
							if (added > 0)
							{
								st->offset += 500;
								Fetch(self);
							}
							else
							{
								st->done(std::move(st->collected));
							}
						});
		}
	};

	auto fetcher = std::make_shared<Fetcher>();
	fetcher->st = state;
	fetcher->Fetch(fetcher);
}

void MapLister::GenerateMaplistAsync(const std::string &outputPath) const
{
	std::string out = outputPath;
	FetchAllApprovedMapsAsync(
		[out](std::vector<MapEntry> maps)
		{
			if (maps.empty())
			{
				MMU_LOG_INFO("No maps returned from CS2KZ API.\n");
				return;
			}

			FILE *fp = fopen(out.c_str(), "w");
			if (!fp)
			{
				MMU_LOG_WARN("Cannot write '%s'.\n", out.c_str());
				return;
			}

			for (const auto &e : maps)
			{
				if (e.isWorkshop && !e.workshopId.empty())
				{
					fprintf(fp, "%s:%s\n", e.displayName.c_str(), e.workshopId.c_str());
				}
				else
				{
					fprintf(fp, "%s\n", e.mapName.c_str());
				}
			}
			fclose(fp);

			MMU_LOG_INFO("Wrote %d maps to '%s'.\n", static_cast<int>(maps.size()), out.c_str());
		});
}

void MapLister::FetchTiersAsync()
{
	// Only one paginated sweep at a time. Guards against rapid map changes
	// re-triggering a fetch before the first one's results are merged.
	bool expected = false;
	if (!m_tierFetchInFlight.compare_exchange_strong(expected, true))
	{
		return;
	}

	FetchAllApprovedMapsAsync(
		[this](std::vector<MapEntry> maps)
		{
			if (maps.empty())
			{
				// Failed / cancelled - clear the latch so a later load can retry.
				m_tierFetchInFlight.store(false);
				return;
			}

			// Build name -> per-course tier lists on the background thread.
			auto cache = std::make_shared<std::unordered_map<std::string, TierLists>>();
			for (const auto &e : maps)
			{
				if (e.mapName.empty())
				{
					continue;
				}
				(*cache)[ToLowerStr(e.mapName)] = {e.classicTiers, e.vanillaTiers};
			}

			// Merge into live state on the game thread (touches m_maps / m_tierCache).
			mmu::http::QueueMainThread(
				[this, cache]()
				{
					for (const auto &kv : *cache)
					{
						m_tierCache[kv.first] = kv.second;
					}
					for (auto &e : m_maps)
					{
						ApplyCachedTiers(e);
					}
					m_tierFetchInFlight.store(false);
					MMU_LOG_INFO("Loaded CS2KZ tiers for %d maps.\n", static_cast<int>(cache->size()));
				});
		});
}

void MapLister::ApplyCachedTiers(MapEntry &e) const
{
	if (!e.classicTiers.empty() || !e.vanillaTiers.empty())
	{
		return;
	}
	auto it = m_tierCache.find(ToLowerStr(e.mapName));
	if (it != m_tierCache.end())
	{
		e.classicTiers = it->second.classic;
		e.vanillaTiers = it->second.vanilla;
	}
}

std::string MapLister::GetDisplayLabel(const MapEntry &e, bool colorize, const char *resetColor) const
{
	std::string base = e.displayName.empty() ? e.mapName : e.displayName;

	std::string mode;
	if (!TierDisplayEnabled(mode))
	{
		return base;
	}

	// Use the clean map name as the base so we don't duplicate any baked "(Tn)".
	std::string clean = e.mapName.empty() ? base : e.mapName;

	bool wantClassic = (mode == "both" || mode == "classic" || mode == "ckz");
	bool wantVanilla = (mode == "both" || mode == "vanilla" || mode == "vnl");
	if (!wantClassic && !wantVanilla)
	{
		// Unrecognized value - default to showing both.
		wantClassic = wantVanilla = true;
	}

	// Collect the mode parts to show.
	struct TierPart
	{
		const char *labelColor;
		const char *label;
		const std::vector<int> *tiers;
	};

	std::vector<TierPart> parts;
	if (wantClassic && !e.classicTiers.empty())
	{
		parts.push_back({CHAT_COLOR_RED, "CKZ", &e.classicTiers});
	}
	if (wantVanilla && !e.vanillaTiers.empty())
	{
		parts.push_back({CHAT_COLOR_GREEN, "VNL", &e.vanillaTiers});
	}
	if (parts.empty())
	{
		return clean;
	}

	const char *def = colorize ? CHAT_COLOR_DEFAULT : "";

	// Render a mode's tier value. Single course -> one tier (honoring KzTierFormat).
	// Multiple courses -> each course's tier as a number joined by "/" (e.g. "1/2/2/3");
	// capped at MAX_COURSES with a trailing "..." when there are more.
	// Each number is colored by its own tier; the "/" stays default.
	const size_t MAX_COURSES = 5;
	auto renderValue = [&](const std::vector<int> &tiers) -> std::string
	{
		if (tiers.size() == 1)
		{
			if (colorize)
			{
				return std::string(TierColorCode(tiers[0])) + FormatTier(tiers[0]);
			}
			return FormatTier(tiers[0]);
		}

		size_t shown = (tiers.size() < MAX_COURSES) ? tiers.size() : MAX_COURSES;
		std::string s;
		for (size_t i = 0; i < shown; i++)
		{
			if (i > 0)
			{
				s += def;
				s += "/";
			}
			if (colorize)
			{
				s += TierColorCode(tiers[i]);
			}
			s += std::to_string(tiers[i]);
		}
		if (tiers.size() > MAX_COURSES)
		{
			s += def;
			s += "...";
		}
		return s;
	};

	// Format: " [CKZ: x | VNL: x]".
	// Color codes (0x01-0x10) only render in chat/menus, not the console.
	std::string suffix = " ";
	suffix += def;
	suffix += "[";
	for (size_t i = 0; i < parts.size(); i++)
	{
		if (i > 0)
		{
			suffix += def;
			suffix += " | ";
		}
		suffix += colorize ? parts[i].labelColor : "";
		suffix += parts[i].label;
		suffix += def;
		suffix += ": ";
		suffix += renderValue(*parts[i].tiers);
	}
	suffix += def;
	suffix += "]";
	if (colorize)
	{
		suffix += resetColor; // restore the surrounding row color
	}

	return clean + suffix;
}

void MapLister::ValidateMapsAsync() const
{
	// Build list of workshop maps to validate
	std::vector<MapEntry> workshopMaps;
	for (const auto &e : m_maps)
	{
		if (e.isWorkshop && !e.workshopId.empty())
		{
			workshopMaps.push_back(e);
		}
	}

	if (workshopMaps.empty())
	{
		return;
	}

	// Steam accepts up to 100 items per request
	const int BATCH = 100;
	for (int start = 0; start < static_cast<int>(workshopMaps.size()); start += BATCH)
	{
		int end = (std::min)(start + BATCH, static_cast<int>(workshopMaps.size()));
		std::vector<MapEntry> batch(workshopMaps.begin() + start, workshopMaps.begin() + end);

		std::string postBody = "itemcount=" + std::to_string(batch.size());
		for (int i = 0; i < static_cast<int>(batch.size()); i++)
		{
			postBody += "&publishedfileids[" + std::to_string(i) + "]=" + batch[i].workshopId;
		}

		std::string apiKey = g_RTVConfig.general.steamApiKey;
		std::string webhook = g_RTVConfig.general.discordWebhook;

		mmu::http::PostForm("https://api.steampowered.com/ISteamRemoteStorage/"
						 "GetPublishedFileDetails/v1/"
						 "?key="
							 + apiKey,
						 postBody,
						 [batch, webhook](bool ok, std::string body)
						 {
							 if (!ok)
							 {
								 return;
							 }

							 // Check each map; result != 1 means it's dead/removed
							 for (const auto &e : batch)
							 {
								 // Find the entry for this ID
								 size_t idPos = body.find("\"" + e.workshopId + "\"");
								 if (idPos == std::string::npos)
								 {
									 continue;
								 }

								 // Extract result field in the object containing this ID
								 size_t objStart = body.rfind('{', idPos);
								 size_t objEnd = body.find('}', idPos);
								 if (objStart == std::string::npos || objEnd == std::string::npos)
								 {
									 continue;
								 }

								 std::string obj = body.substr(objStart, objEnd - objStart + 1);
								 std::string result = JsonGetString(obj, "result");
								 if (result != "1" && result != "")
								 {
									 MMU_LOG_INFO("Dead workshop map detected: %s (id=%s, "
													"result=%s)\n",
													e.displayName.c_str(), e.workshopId.c_str(), result.c_str());

									 if (!webhook.empty())
									 {
										 std::string msg = "Dead workshop map: " + e.displayName + " (ID: " + e.workshopId + ")";
										 std::string json = "{\"content\":\"" + msg + "\"}";
										 mmu::http::Post(webhook, json, nullptr);
									 }
								 }
							 }
						 });
	}
}
