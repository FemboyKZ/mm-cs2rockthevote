#include "map_lister.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/utils/http_client.h"

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
		META_CONPRINTF("[CS2RTV] maplist.txt not found at '%s' - attempting "
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

// CS2KZ API parsing
bool MapLister::ParseCS2KZMapJson(const std::string &jsonObj, MapEntry &out)
{
	// Expected fields: "name" (map name), "workshop_id" (string)
	// We also look into courses[0].filters for tier name
	std::string name = JsonGetString(jsonObj, "name");
	std::string wsId = JsonGetString(jsonObj, "workshop_id");

	if (name.empty())
	{
		return false;
	}

	out.mapName = name;
	out.workshopId = wsId;
	out.isWorkshop = !wsId.empty();

	// Build display name with tier if present.
	// API structure: courses[0].filters.{vanilla|classic}.nub_tier  (string)
	// Tier strings: very-easy=1, easy=2, medium=3, advanced=4, hard=5,
	//               very-hard=6, extreme=7, death=8
	const std::string &mode = g_RTVConfig.general.kzTierMode;
	// Pick "vanilla" or "classic" sub-object under "filters"
	std::string modeKey = (mode == "vanilla") ? "\"vanilla\"" : "\"classic\"";

	int tier = 0;
	size_t filtersPos = jsonObj.find("\"filters\"");
	if (filtersPos != std::string::npos)
	{
		size_t modePos = jsonObj.find(modeKey, filtersPos);
		if (modePos != std::string::npos)
		{
			size_t nubPos = jsonObj.find("\"nub_tier\"", modePos);
			if (nubPos != std::string::npos)
			{
				// Find the string value after "nub_tier":
				size_t colon = jsonObj.find(':', nubPos + 10);
				if (colon != std::string::npos)
				{
					size_t q1 = jsonObj.find('"', colon + 1);
					if (q1 != std::string::npos)
					{
						size_t q2 = jsonObj.find('"', q1 + 1);
						if (q2 != std::string::npos)
						{
							std::string tierStr = jsonObj.substr(q1 + 1, q2 - q1 - 1);
							if (tierStr == "very-easy")
							{
								tier = 1;
							}
							else if (tierStr == "easy")
							{
								tier = 2;
							}
							else if (tierStr == "medium")
							{
								tier = 3;
							}
							else if (tierStr == "advanced")
							{
								tier = 4;
							}
							else if (tierStr == "hard")
							{
								tier = 5;
							}
							else if (tierStr == "very-hard")
							{
								tier = 6;
							}
							else if (tierStr == "extreme")
							{
								tier = 7;
							}
							else if (tierStr == "death")
							{
								tier = 8;
							}
							else if (tierStr == "unfeasible")
							{
								tier = 9;
							}
							else if (tierStr == "impossible")
							{
								tier = 10;
							}
						}
					}
				}
			}
		}
	}

	if (tier > 0)
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "%s (T%d)", name.c_str(), tier);
		out.displayName = buf;
	}
	else
	{
		out.displayName = name;
	}

	return true;
}

void MapLister::LookupByWorkshopIdAsync(const std::string &workshopId, std::function<void(MapEntry)> callback) const
{
	// 1) Try CS2KZ API
	std::string cs2kzUrl = "https://api.cs2kz.org/maps?workshop_id=" + workshopId + "&state=approved";
	RTV_HttpGet(cs2kzUrl,
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
							RTV_QueueMainThread([callback, captured]() mutable { callback(std::move(captured)); });
							return;
						}
					}

					META_CONPRINTF("[CS2RTV] CS2KZ API miss for %s (ok=%d body_len=%zu), trying Steam.\n",
								   workshopId.c_str(), (int)ok, body.size());

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
					RTV_HttpPostForm(steamUrl, postBody,
								 [workshopId, callback](bool ok2, std::string body2)
								 {
									 META_CONPRINTF("[CS2RTV] Steam API response for %s: ok=%d body_len=%zu.\n",
												workshopId.c_str(), (int)ok2, body2.size());
									 MapEntry fallback;
									 if (ok2 && !body2.empty())
									 {
										 // Response:
										 // {"response":{"publishedfiledetails":[{"publishedfileid":"...","title":"...","result":1}]}}
										 std::string title = JsonGetString(body2, "title");
										 if (!title.empty())
										 {
											 fallback.mapName = title;
											 fallback.displayName = title;
											 fallback.workshopId = workshopId;
											 fallback.isWorkshop = true;
										 }
										 else
										 {
											 META_CONPRINTF("[CS2RTV] Steam API returned no title for %s. Body: %.256s\n",
														workshopId.c_str(), body2.c_str());
										 }
									 }
									 // Dispatch to game thread: callback touches game state.
									 MapEntry captured = std::move(fallback);
									 RTV_QueueMainThread([callback, captured]() mutable { callback(std::move(captured)); });
								 });
				});
}

void MapLister::LookupByNameAsync(const std::string &name, std::function<void(MapEntry)> callback) const
{
	std::string url = "https://api.cs2kz.org/maps?name=" + name + "&state=approved&limit=5";
	RTV_HttpGet(url,
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
					RTV_QueueMainThread([callback, captured]() mutable { callback(std::move(captured)); });
				});
}

void MapLister::GenerateMaplistAsync(const std::string &outputPath) const
{
	// Paginate CS2KZ API to get all approved maps
	// We fetch page 0 first, then continue until we get an empty result.
	const int PAGE_SIZE = 500;

	struct State
	{
		std::string outputPath;
		std::vector<MapEntry> collected;
		int offset = 0;
	};

	auto state = std::make_shared<State>();
	state->outputPath = outputPath;

	// Recursive lambda via shared_ptr to allow self-reference
	struct Fetcher
	{
		std::shared_ptr<State> st;

		void Fetch(std::shared_ptr<Fetcher> self)
		{
			std::string url = "https://api.cs2kz.org/maps?state=approved&limit=500&offset=" + std::to_string(st->offset);

			RTV_HttpGet(url,
						[this, self](bool ok, std::string body) mutable
						{
							if (!ok || body.empty())
							{
								Write();
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
								Write();
							}
						});
		}

		void Write()
		{
			if (st->collected.empty())
			{
				META_CONPRINTF("[CS2RTV] No maps returned from CS2KZ API.\n");
				return;
			}

			FILE *fp = fopen(st->outputPath.c_str(), "w");
			if (!fp)
			{
				META_CONPRINTF("[CS2RTV] Cannot write '%s'.\n", st->outputPath.c_str());
				return;
			}

			for (const auto &e : st->collected)
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

			META_CONPRINTF("[CS2RTV] Wrote %d maps to '%s'.\n", static_cast<int>(st->collected.size()), st->outputPath.c_str());
		}
	};

	auto fetcher = std::make_shared<Fetcher>();
	fetcher->st = state;
	fetcher->Fetch(fetcher);
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

		RTV_HttpPostForm("https://api.steampowered.com/ISteamRemoteStorage/"
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
								 META_CONPRINTF("[CS2RTV] Dead workshop map detected: %s (id=%s, "
												"result=%s)\n",
												e.displayName.c_str(), e.workshopId.c_str(), result.c_str());

								 if (!webhook.empty())
								 {
									 std::string msg = "Dead workshop map: " + e.displayName + " (ID: " + e.workshopId + ")";
									 std::string json = "{\"content\":\"" + msg + "\"}";
									 RTV_HttpPost(webhook, json, nullptr);
								 }
							 }
						 }
					 });
	}
}
