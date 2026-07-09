#ifndef _INCLUDE_ADMIN_FORWARDS_H_
#define _INCLUDE_ADMIN_FORWARDS_H_

#include <functional>
#include <vector>
#include <algorithm>
#include <string>
#include <cstdint>

// Other Metamod plugins can acquire this interface via:
//   ICS2AdminForwards *fwd = (ICS2AdminForwards *)g_SMAPI->MetaFactory(
//       CS2ADMIN_FORWARDS_INTERFACE, nullptr, nullptr);
// Version bumped to 003: Register* now returns ForwardHandle; Unregister* added.
#define CS2ADMIN_FORWARDS_INTERFACE "ICS2AdminForwards003"

// Opaque handle returned by Register* calls. Pass to the matching Unregister*
// method to remove a callback. 0 is the invalid/sentinel value.
using ForwardHandle = uint32_t;
static constexpr ForwardHandle kInvalidForwardHandle = 0;

// Internal helper: per-forward-type list with stable numeric IDs.
template<typename Fn>
struct ForwardList
{
	struct Entry
	{
		ForwardHandle id;
		Fn fn;
	};

	ForwardHandle Add(Fn fn)
	{
		uint32_t id = m_nextId++;
		m_entries.push_back({id, std::move(fn)});
		return id;
	}

	void Remove(ForwardHandle id)
	{
		m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [id](const Entry &e) { return e.id == id; }), m_entries.end());
	}

	void Clear()
	{
		m_entries.clear();
	}

	std::vector<Entry> m_entries;
	uint32_t m_nextId = 1;
};

// Forward callback types.
// Returning true from blockable callbacks prevents the action.

// Ban/Unban
using OnBanPlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnUnbanPlayerFn = std::function<void(const char *authid, int adminSlot)>;

// Mute/Gag/Silence
using OnMutePlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnGagPlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnSilencePlayerFn = std::function<bool(int targetSlot, int adminSlot, int timeMinutes, const char *reason)>;
using OnUnmutePlayerFn = std::function<void(int targetSlot, int adminSlot)>;
using OnUngagPlayerFn = std::function<void(int targetSlot, int adminSlot)>;
using OnUnsilencePlayerFn = std::function<void(int targetSlot, int adminSlot)>;

// Kick/Slay (blockable)
using OnKickPlayerFn = std::function<bool(int targetSlot, int adminSlot, const char *reason)>;
using OnSlayPlayerFn = std::function<bool(int targetSlot, int adminSlot)>;

// Map change (blockable)
using OnMapChangeFn = std::function<bool(const char *mapName, int adminSlot)>;

// Player lifecycle
using OnClientConnectedFn = std::function<void(int slot, const char *name, uint64_t steamid64, const char *ip)>;
using OnClientDisconnectFn = std::function<void(int slot)>;
using OnClientAuthorizedFn = std::function<void(int slot, const char *authid, uint64_t steamid64)>;

// Report
using OnReportPlayerFn = std::function<void(int reporterSlot, int targetSlot, const char *reason)>;

// Admin check
using OnClientPreAdminCheckFn = std::function<void(int slot)>;

class ICS2AdminForwards
{
public:
	// Register a callback. Returns a handle that can be passed to the matching
	// Unregister* method. Call Unregister* from your plugin's Unload() to
	// prevent cs2admin from invoking callbacks into an unloaded DLL.
	virtual ForwardHandle RegisterOnBanPlayer(OnBanPlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnUnbanPlayer(OnUnbanPlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnMutePlayer(OnMutePlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnGagPlayer(OnGagPlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnUnmutePlayer(OnUnmutePlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnUngagPlayer(OnUngagPlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnReportPlayer(OnReportPlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnClientPreAdminCheck(OnClientPreAdminCheckFn callback) = 0;
	virtual ForwardHandle RegisterOnKickPlayer(OnKickPlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnSlayPlayer(OnSlayPlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnSilencePlayer(OnSilencePlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnUnsilencePlayer(OnUnsilencePlayerFn callback) = 0;
	virtual ForwardHandle RegisterOnMapChange(OnMapChangeFn callback) = 0;
	virtual ForwardHandle RegisterOnClientConnected(OnClientConnectedFn callback) = 0;
	virtual ForwardHandle RegisterOnClientDisconnect(OnClientDisconnectFn callback) = 0;
	virtual ForwardHandle RegisterOnClientAuthorized(OnClientAuthorizedFn callback) = 0;

	virtual void UnregisterOnBanPlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnUnbanPlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnMutePlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnGagPlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnUnmutePlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnUngagPlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnReportPlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnClientPreAdminCheck(ForwardHandle handle) = 0;

	virtual void UnregisterOnKickPlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnSlayPlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnSilencePlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnUnsilencePlayer(ForwardHandle handle) = 0;
	virtual void UnregisterOnMapChange(ForwardHandle handle) = 0;
	virtual void UnregisterOnClientConnected(ForwardHandle handle) = 0;
	virtual void UnregisterOnClientDisconnect(ForwardHandle handle) = 0;
	virtual void UnregisterOnClientAuthorized(ForwardHandle handle) = 0;
};

class CS2AForwards : public ICS2AdminForwards
{
public:
	// Called from CS2APlugin::Unload() to drop all registered callbacks so that
	// no lambda from another (possibly already-unloaded) plugin can be invoked.
	void Shutdown()
	{
		m_onBanPlayer.Clear();
		m_onUnbanPlayer.Clear();
		m_onMutePlayer.Clear();
		m_onGagPlayer.Clear();
		m_onUnmutePlayer.Clear();
		m_onUngagPlayer.Clear();
		m_onUnsilencePlayer.Clear();
		m_onReportPlayer.Clear();
		m_onClientPreAdminCheck.Clear();
		m_onKickPlayer.Clear();
		m_onSlayPlayer.Clear();
		m_onSilencePlayer.Clear();
		m_onMapChange.Clear();
		m_onClientConnected.Clear();
		m_onClientDisconnect.Clear();
		m_onClientAuthorized.Clear();
	}

	ForwardHandle RegisterOnBanPlayer(OnBanPlayerFn callback) override
	{
		return m_onBanPlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnUnbanPlayer(OnUnbanPlayerFn callback) override
	{
		return m_onUnbanPlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnMutePlayer(OnMutePlayerFn callback) override
	{
		return m_onMutePlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnGagPlayer(OnGagPlayerFn callback) override
	{
		return m_onGagPlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnUnmutePlayer(OnUnmutePlayerFn callback) override
	{
		return m_onUnmutePlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnUngagPlayer(OnUngagPlayerFn callback) override
	{
		return m_onUngagPlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnReportPlayer(OnReportPlayerFn callback) override
	{
		return m_onReportPlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnClientPreAdminCheck(OnClientPreAdminCheckFn callback) override
	{
		return m_onClientPreAdminCheck.Add(std::move(callback));
	}

	ForwardHandle RegisterOnKickPlayer(OnKickPlayerFn callback) override
	{
		return m_onKickPlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnSlayPlayer(OnSlayPlayerFn callback) override
	{
		return m_onSlayPlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnSilencePlayer(OnSilencePlayerFn callback) override
	{
		return m_onSilencePlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnUnsilencePlayer(OnUnsilencePlayerFn callback) override
	{
		return m_onUnsilencePlayer.Add(std::move(callback));
	}

	ForwardHandle RegisterOnMapChange(OnMapChangeFn callback) override
	{
		return m_onMapChange.Add(std::move(callback));
	}

	ForwardHandle RegisterOnClientConnected(OnClientConnectedFn callback) override
	{
		return m_onClientConnected.Add(std::move(callback));
	}

	ForwardHandle RegisterOnClientDisconnect(OnClientDisconnectFn callback) override
	{
		return m_onClientDisconnect.Add(std::move(callback));
	}

	ForwardHandle RegisterOnClientAuthorized(OnClientAuthorizedFn callback) override
	{
		return m_onClientAuthorized.Add(std::move(callback));
	}

	void UnregisterOnBanPlayer(ForwardHandle h) override
	{
		m_onBanPlayer.Remove(h);
	}

	void UnregisterOnUnbanPlayer(ForwardHandle h) override
	{
		m_onUnbanPlayer.Remove(h);
	}

	void UnregisterOnMutePlayer(ForwardHandle h) override
	{
		m_onMutePlayer.Remove(h);
	}

	void UnregisterOnGagPlayer(ForwardHandle h) override
	{
		m_onGagPlayer.Remove(h);
	}

	void UnregisterOnUnmutePlayer(ForwardHandle h) override
	{
		m_onUnmutePlayer.Remove(h);
	}

	void UnregisterOnUngagPlayer(ForwardHandle h) override
	{
		m_onUngagPlayer.Remove(h);
	}

	void UnregisterOnReportPlayer(ForwardHandle h) override
	{
		m_onReportPlayer.Remove(h);
	}

	void UnregisterOnClientPreAdminCheck(ForwardHandle h) override
	{
		m_onClientPreAdminCheck.Remove(h);
	}

	void UnregisterOnKickPlayer(ForwardHandle h) override
	{
		m_onKickPlayer.Remove(h);
	}

	void UnregisterOnSlayPlayer(ForwardHandle h) override
	{
		m_onSlayPlayer.Remove(h);
	}

	void UnregisterOnSilencePlayer(ForwardHandle h) override
	{
		m_onSilencePlayer.Remove(h);
	}

	void UnregisterOnUnsilencePlayer(ForwardHandle h) override
	{
		m_onUnsilencePlayer.Remove(h);
	}

	void UnregisterOnMapChange(ForwardHandle h) override
	{
		m_onMapChange.Remove(h);
	}

	void UnregisterOnClientConnected(ForwardHandle h) override
	{
		m_onClientConnected.Remove(h);
	}

	void UnregisterOnClientDisconnect(ForwardHandle h) override
	{
		m_onClientDisconnect.Remove(h);
	}

	void UnregisterOnClientAuthorized(ForwardHandle h) override
	{
		m_onClientAuthorized.Remove(h);
	}

	// Fire forwards. Blockable ones return true if any callback blocked.

	bool FireOnBanPlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason)
	{
		for (auto &e : m_onBanPlayer.m_entries)
		{
			if (e.fn(targetSlot, adminSlot, timeMinutes, reason))
			{
				return true;
			}
		}
		return false;
	}

	bool FireOnMutePlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason)
	{
		for (auto &e : m_onMutePlayer.m_entries)
		{
			if (e.fn(targetSlot, adminSlot, timeMinutes, reason))
			{
				return true;
			}
		}
		return false;
	}

	bool FireOnGagPlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason)
	{
		for (auto &e : m_onGagPlayer.m_entries)
		{
			if (e.fn(targetSlot, adminSlot, timeMinutes, reason))
			{
				return true;
			}
		}
		return false;
	}

	bool FireOnKickPlayer(int targetSlot, int adminSlot, const char *reason)
	{
		for (auto &e : m_onKickPlayer.m_entries)
		{
			if (e.fn(targetSlot, adminSlot, reason))
			{
				return true;
			}
		}
		return false;
	}

	bool FireOnSlayPlayer(int targetSlot, int adminSlot)
	{
		for (auto &e : m_onSlayPlayer.m_entries)
		{
			if (e.fn(targetSlot, adminSlot))
			{
				return true;
			}
		}
		return false;
	}

	bool FireOnSilencePlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason)
	{
		for (auto &e : m_onSilencePlayer.m_entries)
		{
			if (e.fn(targetSlot, adminSlot, timeMinutes, reason))
			{
				return true;
			}
		}
		return false;
	}

	bool FireOnMapChange(const char *mapName, int adminSlot)
	{
		for (auto &e : m_onMapChange.m_entries)
		{
			if (e.fn(mapName, adminSlot))
			{
				return true;
			}
		}
		return false;
	}

	void FireOnUnbanPlayer(const char *authid, int adminSlot)
	{
		for (auto &e : m_onUnbanPlayer.m_entries)
		{
			e.fn(authid, adminSlot);
		}
	}

	void FireOnUnmutePlayer(int targetSlot, int adminSlot)
	{
		for (auto &e : m_onUnmutePlayer.m_entries)
		{
			e.fn(targetSlot, adminSlot);
		}
	}

	void FireOnUngagPlayer(int targetSlot, int adminSlot)
	{
		for (auto &e : m_onUngagPlayer.m_entries)
		{
			e.fn(targetSlot, adminSlot);
		}
	}

	void FireOnUnsilencePlayer(int targetSlot, int adminSlot)
	{
		for (auto &e : m_onUnsilencePlayer.m_entries)
		{
			e.fn(targetSlot, adminSlot);
		}
	}

	void FireOnReportPlayer(int reporterSlot, int targetSlot, const char *reason)
	{
		for (auto &e : m_onReportPlayer.m_entries)
		{
			e.fn(reporterSlot, targetSlot, reason);
		}
	}

	void FireOnClientPreAdminCheck(int slot)
	{
		for (auto &e : m_onClientPreAdminCheck.m_entries)
		{
			e.fn(slot);
		}
	}

	void FireOnClientConnected(int slot, const char *name, uint64_t steamid64, const char *ip)
	{
		for (auto &e : m_onClientConnected.m_entries)
		{
			e.fn(slot, name, steamid64, ip);
		}
	}

	void FireOnClientDisconnect(int slot)
	{
		for (auto &e : m_onClientDisconnect.m_entries)
		{
			e.fn(slot);
		}
	}

	void FireOnClientAuthorized(int slot, const char *authid, uint64_t steamid64)
	{
		for (auto &e : m_onClientAuthorized.m_entries)
		{
			e.fn(slot, authid, steamid64);
		}
	}

private:
	ForwardList<OnBanPlayerFn> m_onBanPlayer;
	ForwardList<OnUnbanPlayerFn> m_onUnbanPlayer;
	ForwardList<OnMutePlayerFn> m_onMutePlayer;
	ForwardList<OnGagPlayerFn> m_onGagPlayer;
	ForwardList<OnUnmutePlayerFn> m_onUnmutePlayer;
	ForwardList<OnUngagPlayerFn> m_onUngagPlayer;
	ForwardList<OnReportPlayerFn> m_onReportPlayer;
	ForwardList<OnClientPreAdminCheckFn> m_onClientPreAdminCheck;
	ForwardList<OnKickPlayerFn> m_onKickPlayer;
	ForwardList<OnSlayPlayerFn> m_onSlayPlayer;
	ForwardList<OnSilencePlayerFn> m_onSilencePlayer;
	ForwardList<OnUnsilencePlayerFn> m_onUnsilencePlayer;
	ForwardList<OnMapChangeFn> m_onMapChange;
	ForwardList<OnClientConnectedFn> m_onClientConnected;
	ForwardList<OnClientDisconnectFn> m_onClientDisconnect;
	ForwardList<OnClientAuthorizedFn> m_onClientAuthorized;
};

extern CS2AForwards g_CS2AForwards;

#endif // _INCLUDE_ADMIN_FORWARDS_H_
