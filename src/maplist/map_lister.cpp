#include "map_lister.h"
#include "utils/str.h"
#include "utils/log.h"
#include "utils/maplist.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/timers/timer_system.h"
#include "utils/discord.h"
#include "utils/http_client.h"
#include "utils/json_doc.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <unordered_set>

MapLister g_MapLister;

// Returns true if DisplayKzTiers is set to anything other than off/none.
// outMode receives the lowercased config value.
static bool TierDisplayEnabled(std::string &outMode)
{
	outMode = str::ToLower(g_RTVConfig.general.displayKzTiers);
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
	if (str::ToLower(g_RTVConfig.general.kzTierFormat) == "text")
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
	return mmu::StripMapAnnotation(displayName);
}

bool MapLister::ParseLine(const std::string &rawLine, MapEntry &out)
{
	mmu::MapListEntry parsed;
	if (!mmu::ParseMapListLine(rawLine, parsed))
	{
		return false;
	}

	// Tiers are filled in later by ApplyCachedTiers.
	out.displayName = parsed.displayName;
	out.mapName = parsed.mapName;
	out.workshopId = parsed.workshopId;
	out.isWorkshop = parsed.isWorkshop;
	return true;
}

void MapLister::LoadForMap(const char *path)
{
	m_apiPool = g_RTVConfig.general.kzApiMaplist;
	if (!m_apiPool)
	{
		LoadFromFile(path);
		return;
	}

	if (!m_poolLoaded)
	{
		// Left over from file mode.
		m_maps.clear();
	}
	if (NeedsRefresh())
	{
		RefreshAsync();
	}
}

bool MapLister::NeedsRefresh() const
{
	if (m_refreshInFlight)
	{
		return false;
	}
	if (!m_poolLoaded)
	{
		return true;
	}
	return std::chrono::steady_clock::now() - m_lastRefresh >= std::chrono::seconds(kRefreshIntervalSeconds);
}

void MapLister::RefreshAsync(std::function<void(int)> onDone)
{
	if (m_refreshInFlight)
	{
		if (onDone)
		{
			onDone(-1);
		}
		return;
	}
	m_refreshInFlight = true;

	FetchAllApprovedMapsAsync(
		[this, onDone](std::vector<MapEntry> maps)
		{
			auto fresh = std::make_shared<std::vector<MapEntry>>(std::move(maps));

			// Touches m_maps, so merge on the game thread.
			mmu::http::QueueMainThread(
				[this, onDone, fresh]()
				{
					m_refreshInFlight = false;

					if (fresh->empty())
					{
						MMU_LOG_WARN("CS2KZ map pool fetch failed or returned no maps.%s\n", m_poolLoaded ? " Keeping the previous pool." : "");
						if (!m_poolLoaded)
						{
							g_Timers.KillTimer(m_retryTimerId);
							m_retryTimerId = g_Timers.CreateTimer(kRetrySeconds,
																  [this]()
																  {
																	  if (PoolPending())
																	  {
																		  RefreshAsync();
																	  }
																  });
						}
						if (onDone)
						{
							onDone(-1);
						}
						return;
					}

					ApplyPool(std::move(*fresh));
					MMU_LOG_INFO("Map pool loaded: %d maps from the CS2KZ API.\n", static_cast<int>(m_maps.size()));

					if (g_RTVConfig.general.mapValidationEnabled && !g_RTVConfig.general.steamApiKey.empty())
					{
						ValidateMapsAsync();
					}

					if (onDone)
					{
						onDone(static_cast<int>(m_maps.size()));
					}
				});
		});
}

void MapLister::ApplyPool(std::vector<MapEntry> fresh)
{
	std::vector<MapEntry> old = std::move(m_maps);
	m_maps = std::move(fresh);

	// Keep off-pool nominations resolvable.
	for (auto &e : old)
	{
		if (!e.dynamic)
		{
			continue;
		}
		bool inPool = FindExact(e.mapName) != nullptr || (!e.workshopId.empty() && FindByWorkshopId(e.workshopId) != nullptr);
		if (!inPool)
		{
			m_maps.push_back(std::move(e));
		}
	}

	m_poolLoaded = true;
	m_lastRefresh = std::chrono::steady_clock::now();
}

int MapLister::LoadFromFile(const char *path)
{
	m_maps.clear();
	m_poolLoaded = false;
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

	std::unordered_set<std::string> seen;
	char line[512];
	while (fgets(line, sizeof(line), fp))
	{
		MapEntry entry;
		if (!ParseLine(std::string(line), entry))
		{
			continue;
		}
		if (!seen.insert(str::ToLower(entry.mapName)).second)
		{
			MMU_LOG_WARN("maplist.txt lists '%s' more than once, keeping the first entry.\n", entry.mapName.c_str());
			continue;
		}
		m_maps.push_back(std::move(entry));
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
	if (g_RTVConfig.general.mapValidationEnabled && !g_RTVConfig.general.steamApiKey.empty())
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

	// Keep off-list nominations resolvable, as ApplyPool does.
	std::vector<MapEntry> old = std::move(m_maps);
	int count = LoadFromFile(m_lastPath.c_str());
	for (const auto &e : old)
	{
		if (e.dynamic)
		{
			AddDynamicMap(e);
		}
	}
	return count;
}

const MapEntry *MapLister::FindExact(const std::string &name) const
{
	std::string lower = str::ToLower(name);
	for (const auto &entry : m_maps)
	{
		if (str::ToLower(entry.displayName) == lower)
		{
			return &entry;
		}
		if (str::ToLower(entry.mapName) == lower)
		{
			return &entry;
		}
	}
	return nullptr;
}

std::vector<const MapEntry *> MapLister::FindMatching(const std::string &query) const
{
	std::string lower = str::ToLower(query);
	std::vector<const MapEntry *> results;
	for (const auto &entry : m_maps)
	{
		if (str::ToLower(entry.displayName).find(lower) != std::string::npos || str::ToLower(entry.mapName).find(lower) != std::string::npos)
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
	m_maps.back().dynamic = true;
	return &m_maps.back();
}

// Percent-encode a query parameter value, so a space, '#' or '&' in a map name cannot break or extend the URL.
static std::string UrlEncodeQuery(const std::string &value)
{
	static const char *kHex = "0123456789ABCDEF";
	std::string out;
	out.reserve(value.size() + 8);
	for (unsigned char c : value)
	{
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			out += static_cast<char>(c);
			continue;
		}
		out += '%';
		out += kHex[c >> 4];
		out += kHex[c & 0x0F];
	}
	return out;
}

static void ForEachApiMap(const std::string &body, const std::function<void(const nlohmann::json &)> &cb)
{
	const nlohmann::json doc = nlohmann::json::parse(body, nullptr, false);
	const nlohmann::json *values = doc.is_array() ? &doc : mmu::json::Field(doc, "values");
	if (!values || !values->is_array())
	{
		return;
	}
	for (const nlohmann::json &map : *values)
	{
		if (map.is_object())
		{
			cb(map);
		}
	}
}

// A course with no tier for the mode is skipped.
// API structure: map.courses[].filters.{vanilla|classic}.nub_tier (string)
static void ParseTierListForMode(const nlohmann::json &map, const char *mode, std::vector<int> &out)
{
	out.clear();
	const nlohmann::json *courses = mmu::json::Field(map, "courses");
	if (!courses || !courses->is_array())
	{
		return;
	}
	for (const nlohmann::json &course : *courses)
	{
		const nlohmann::json *filters = mmu::json::Field(course, "filters");
		const nlohmann::json *filter = filters ? mmu::json::Field(*filters, mode) : nullptr;
		int tier = filter ? TierNameToInt(mmu::json::StringField(*filter, "nub_tier")) : 0;
		if (tier > 0)
		{
			out.push_back(tier);
		}
	}
}

// Null when the reply has none.
static const nlohmann::json *PublishedFileDetails(const nlohmann::json &doc)
{
	const nlohmann::json *response = mmu::json::Field(doc, "response");
	const nlohmann::json *files = response ? mmu::json::Field(*response, "publishedfiledetails") : nullptr;
	return files && files->is_array() ? files : nullptr;
}

// False without a name.
static bool ParseApiMap(const nlohmann::json &map, MapEntry &out)
{
	std::string name = mmu::json::StringField(map, "name");
	std::string wsId = mmu::json::ScalarField(map, "workshop_id");

	if (name.empty())
	{
		return false;
	}

	out.mapName = name;
	out.workshopId = wsId;
	out.isWorkshop = !wsId.empty();

	// Parse per-course classic and vanilla tiers so DisplayKzTiers can list them.
	ParseTierListForMode(map, "classic", out.classicTiers);
	ParseTierListForMode(map, "vanilla", out.vanillaTiers);

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
	// Copied: the callbacks run off the game thread.
	std::string steamKey = g_RTVConfig.general.steamApiKey;
	mmu::http::Get(cs2kzUrl,
				   [workshopId, callback, steamKey](bool ok, std::string body)
				   {
					   if (ok && !body.empty())
					   {
						   // CS2KZ returns an array; grab first object
						   MapEntry found;
						   bool parsed = false;
						   ForEachApiMap(body,
										 [&](const nlohmann::json &map)
										 {
											 if (!parsed && ParseApiMap(map, found))
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
								   const nlohmann::json doc = nlohmann::json::parse(body2, nullptr, false);
								   const nlohmann::json *files = PublishedFileDetails(doc);
								   const nlohmann::json *file = files && !files->empty() ? &(*files)[0] : nullptr;
								   std::string title = file ? mmu::json::StringField(*file, "title") : std::string();
								   if (!title.empty() && mmu::json::ScalarField(*file, "consumer_app_id") != "730")
								   {
									   MMU_LOG_WARN("Workshop item %s is not a CS2 item.\n", workshopId.c_str());
									   mmu::http::QueueMainThread([callback]() { callback(MapEntry()); });
									   return;
								   }
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
													  if (p != std::string::npos && body3.find("data-appid=\"730\"") != std::string::npos)
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
																 && (title.back() == ' ' || title.back() == '\r' || title.back() == '\n'
																	 || title.back() == '\t'))
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

void MapLister::LookupByNameAsync(const std::string &name, std::function<void(MapEntry, bool ambiguous)> callback) const
{
	std::string url = "https://api.cs2kz.org/maps?name=" + UrlEncodeQuery(name) + "&state=approved&limit=100";
	mmu::http::Get(url,
				   [callback, name](bool ok, std::string body)
				   {
					   // The API matches substrings: take the exact name, or the only result.
					   MapEntry found;
					   int results = 0;
					   bool exact = false;
					   if (ok && !body.empty())
					   {
						   const std::string wanted = str::ToLower(name);
						   ForEachApiMap(body,
										 [&](const nlohmann::json &map)
										 {
											 MapEntry e;
											 if (exact || !ParseApiMap(map, e))
											 {
												 return;
											 }
											 // '_' and '%' are wildcards to the API.
											 const std::string got = str::ToLower(e.mapName);
											 if (got.find(wanted) == std::string::npos)
											 {
												 return;
											 }
											 results++;
											 exact = got == wanted;
											 if (exact || results == 1)
											 {
												 found = std::move(e);
											 }
										 });
					   }
					   const bool ambiguous = !exact && results > 1;
					   if (ambiguous)
					   {
						   found = MapEntry();
					   }
					   // Dispatch to game thread: callback touches game state.
					   MapEntry captured = std::move(found);
					   mmu::http::QueueMainThread([callback, captured, ambiguous]() mutable { callback(std::move(captured), ambiguous); });
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
								   // A half-finished sweep is not a map list,
								   // so hand back nothing rather than let a caller cache or write it.
								   MMU_LOG_WARN("CS2KZ map fetch failed at offset %d, discarding %d partial result(s).\n", st->offset,
												static_cast<int>(st->collected.size()));
								   st->collected.clear();
								   st->done(std::move(st->collected));
								   return;
							   }

							   int countBefore = static_cast<int>(st->collected.size());
							   ForEachApiMap(body,
											 [&](const nlohmann::json &map)
											 {
												 MapEntry e;
												 if (ParseApiMap(map, e))
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

			// The load that triggered this found no file, so nothing is in memory yet.
			mmu::http::QueueMainThread([out]() { g_MapLister.LoadFromFile(out.c_str()); });
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
				(*cache)[str::ToLower(e.mapName)] = {e.classicTiers, e.vanillaTiers};
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
	auto it = m_tierCache.find(str::ToLower(e.mapName));
	if (it != m_tierCache.end())
	{
		e.classicTiers = it->second.classic;
		e.vanillaTiers = it->second.vanilla;
	}
}

void SortMapsByName(std::vector<const MapEntry *> &maps)
{
	auto name = [](const MapEntry *e) -> const std::string & { return e->displayName.empty() ? e->mapName : e->displayName; };
	std::stable_sort(maps.begin(), maps.end(), [&](const MapEntry *a, const MapEntry *b) { return mmu::MapNameLess(name(a), name(b)); });
}

void MapLister::TierModes(bool &classic, bool &vanilla)
{
	classic = vanilla = false;
	std::string mode;
	if (!TierDisplayEnabled(mode))
	{
		return;
	}
	classic = (mode == "both" || mode == "classic" || mode == "ckz");
	vanilla = (mode == "both" || mode == "vanilla" || mode == "vnl");
	if (!classic && !vanilla)
	{
		// Unrecognized value - default to showing both.
		classic = vanilla = true;
	}
}

// Courses a row lists per mode.
static const size_t MAX_COURSES = 5;

std::string MapLister::GetNameAndTiers(const MapEntry &e, bool colorize, std::string &tiers) const
{
	tiers.clear();
	std::string base = e.displayName.empty() ? e.mapName : e.displayName;

	bool wantClassic = false;
	bool wantVanilla = false;
	TierModes(wantClassic, wantVanilla);
	if (!wantClassic && !wantVanilla)
	{
		return base;
	}

	// Use the clean map name as the base so we don't duplicate any baked "(Tn)".
	std::string clean = e.mapName.empty() ? base : e.mapName;

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

	// Color codes (0x01-0x10) only render in chat/menus, not the console.
	for (size_t i = 0; i < parts.size(); i++)
	{
		if (i > 0)
		{
			tiers += def;
			tiers += " | ";
		}
		tiers += colorize ? parts[i].labelColor : "";
		tiers += parts[i].label;
		tiers += def;
		tiers += ": ";
		tiers += renderValue(*parts[i].tiers);
	}
	return clean;
}

int MapLister::TierCells() const
{
	bool classic = false;
	bool vanilla = false;
	TierModes(classic, vanilla);
	size_t most = 0;
	for (const MapEntry &map : m_maps)
	{
		most = std::max({most, classic ? map.classicTiers.size() : 0, vanilla ? map.vanillaTiers.size() : 0});
	}
	// Two at least, which a mode's heading takes.
	return static_cast<int>(std::max(std::min(most, MAX_COURSES), size_t(2)));
}

std::string MapLister::GetNameAndCells(const MapEntry &e, int columns, std::vector<std::string> &cells) const
{
	cells.clear();
	std::string tiers;
	std::string name = GetNameAndTiers(e, false, tiers);
	if (tiers.empty())
	{
		return name;
	}

	auto mode = [&](const std::vector<int> &modeTiers)
	{
		for (size_t i = 0; i < static_cast<size_t>(columns); i++)
		{
			cells.push_back(i < modeTiers.size() ? TierColorCode(modeTiers[i]) + std::to_string(modeTiers[i]) : std::string());
		}
	};
	bool classic = false;
	bool vanilla = false;
	TierModes(classic, vanilla);
	if (classic)
	{
		mode(e.classicTiers);
	}
	if (vanilla)
	{
		mode(e.vanillaTiers);
	}
	return name;
}

std::vector<std::string> MapLister::GetTierDetails(const MapEntry &e) const
{
	bool classic = false;
	bool vanilla = false;
	TierModes(classic, vanilla);
	const size_t courses = std::max(classic ? e.classicTiers.size() : 0, vanilla ? e.vanillaTiers.size() : 0);
	std::vector<std::string> lines;
	for (size_t i = 0; courses > MAX_COURSES && i < courses; i++)
	{
		std::string line = "#" + std::to_string(i + 1);
		auto mode = [&](const char *label, const std::vector<int> &tiers)
		{ line += std::string("   ") + label + " " + (i < tiers.size() ? std::to_string(tiers[i]) : "-"); };
		if (classic)
		{
			mode("CKZ", e.classicTiers);
		}
		if (vanilla)
		{
			mode("VNL", e.vanillaTiers);
		}
		lines.push_back(std::move(line));
	}
	return lines;
}

std::string MapLister::GetDisplayLabel(const MapEntry &e, bool colorize, const char *resetColor) const
{
	std::string tiers;
	std::string name = GetNameAndTiers(e, colorize, tiers);
	if (tiers.empty())
	{
		return name;
	}

	// Format: " [CKZ: x | VNL: x]".
	const char *def = colorize ? CHAT_COLOR_DEFAULT : "";
	std::string suffix = std::string(" ") + def + "[" + tiers + def + "]";
	if (colorize)
	{
		suffix += resetColor; // restore the surrounding row color
	}
	return name + suffix;
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
		int end = std::min(start + BATCH, static_cast<int>(workshopMaps.size()));
		std::vector<MapEntry> batch(workshopMaps.begin() + start, workshopMaps.begin() + end);

		std::string postBody = "itemcount=" + std::to_string(batch.size());
		for (int i = 0; i < static_cast<int>(batch.size()); i++)
		{
			postBody += "&publishedfileids[" + std::to_string(i) + "]=" + batch[i].workshopId;
		}

		std::string apiKey = g_RTVConfig.general.steamApiKey;
		std::string webhook = g_RTVConfig.discord.webhookUrl;

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

								const nlohmann::json doc = nlohmann::json::parse(body, nullptr, false);
								std::unordered_map<std::string, std::string> results;
								if (const nlohmann::json *files = PublishedFileDetails(doc))
								{
									for (const nlohmann::json &file : *files)
									{
										results[mmu::json::ScalarField(file, "publishedfileid")] = mmu::json::ScalarField(file, "result");
									}
								}

								// Check each map; result != 1 means it's dead/removed
								for (const auto &e : batch)
								{
									auto found = results.find(e.workshopId);
									if (found == results.end())
									{
										continue;
									}
									const std::string &result = found->second;
									if (result != "1" && result != "")
									{
										MMU_LOG_INFO("Dead workshop map detected: %s (id=%s, "
													 "result=%s)\n",
													 e.displayName.c_str(), e.workshopId.c_str(), result.c_str());

										// Callbacks run one at a time, so no lock.
										static std::unordered_set<std::string> s_reported;
										if (!webhook.empty() && s_reported.insert(e.workshopId).second)
										{
											std::string msg = "Dead workshop map: " + e.displayName + " (ID: " + e.workshopId + ")";
											mmu::discord::SendText(webhook, msg.c_str());
										}
									}
								}
							});
	}
}
