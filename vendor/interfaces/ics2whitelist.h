#ifndef _INCLUDE_ICS2WHITELIST_H_
#define _INCLUDE_ICS2WHITELIST_H_

#include <cstdint>

#define CS2WHITELIST_INTERFACE "ICS2Whitelist002"

// Kick-forward listener interface.
// Other mm:s plugins implement this and register via ICS2Whitelist::AddListener.
enum class WLKickResult
{
	Allow, // proceed with kick (default)
	Block, // suppress the kick - another plugin is handling access
};

class ICS2WhitelistListener
{
public:
	// Called just before a player is kicked by the whitelist (first rejection only,
	// not repeat reconnects that are handled by the rejection cache).
	// Return Block to cancel the kick.
	virtual WLKickResult OnWhitelistKickPre(int slot)
	{
		return WLKickResult::Allow;
	}

	virtual ~ICS2WhitelistListener() = default;
};

// Public whitelist interface for other Metamod plugins.
//
// Obtain via:
//   ICS2Whitelist *pWL = static_cast<ICS2Whitelist *>(
//       g_SMAPI->MetaFactory(CS2WHITELIST_INTERFACE, nullptr, nullptr));
class ICS2Whitelist
{
public:
	virtual ~ICS2Whitelist() = default;

	// Whitelist queries

	// Returns true if the player in the given slot passes the whitelist check.
	// Same check as on connect; returns true if whitelist is disabled or slot invalid.
	virtual bool IsPlayerWhitelisted(int slot) const = 0;

	// Returns true if the given raw entry string (SteamID or IP) is present in
	// the in-memory whitelist.  The entry is normalised before the lookup.
	virtual bool IsEntryWhitelisted(const char *entry) const = 0;

	// Returns the number of entries currently in the in-memory whitelist.
	virtual int GetEntryCount() const = 0;

	// Whitelist cache (confirmed-allowed players this map)

	// Returns true if the player's xuid is in the per-map whitelist-confirm cache.
	// Cached players skip the full whitelist lookup on reconnect.
	virtual bool IsPlayerWhitelistCached(int slot) const = 0;

	// Returns the number of xuids in the whitelist-confirm cache.
	virtual int GetWhitelistCacheCount() const = 0;

	// Rejection cache (confirmed-rejected players this map)

	// Returns true if the player's xuid is in the rejection (blacklist) cache.
	virtual bool IsPlayerBlacklisted(int slot) const = 0;

	// Returns the number of xuids in the rejection cache.
	virtual int GetBlacklistCacheCount() const = 0;

	// Mutation

	// Reload the whitelist file from disk (also clears both caches).
	// Returns true on success.
	virtual bool ReloadFile() = 0;

	// Add or remove an entry from the in-memory whitelist and persist to file/DB.
	// Returns true if the set was changed.
	virtual bool AddEntry(const char *entry) = 0;
	virtual bool RemoveEntry(const char *entry) = 0;

	// Kick-forward listener registration

	virtual void AddListener(ICS2WhitelistListener *listener) = 0;
	virtual void RemoveListener(ICS2WhitelistListener *listener) = 0;
};

#endif // _INCLUDE_ICS2WHITELIST_H_
