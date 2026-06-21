#ifndef _INCLUDE_RTV_FORWARDS_H_
#define _INCLUDE_RTV_FORWARDS_H_

#include <functional>
#include <vector>
#include <algorithm>
#include <cstdint>

// Other Metamod plugins can acquire this interface via:
//   ICS2RTVForwards *fwd = (ICS2RTVForwards *)g_SMAPI->MetaFactory(
//       CS2RTV_FORWARDS_INTERFACE, nullptr, nullptr);
#define CS2RTV_FORWARDS_INTERFACE "ICS2RTVForwards001"

// Opaque handle returned by Register* calls.
// Pass to the matching Unregister* method to remove a callback.
// 0 is the invalid/sentinel value.
using RTVForwardHandle = uint32_t;
static constexpr RTVForwardHandle kInvalidRTVForwardHandle = 0;

// Per-forward-type list with stable numeric IDs.
template<typename Fn>
struct RTVForwardList
{
	struct Entry
	{
		RTVForwardHandle id;
		Fn fn;
	};

	RTVForwardHandle Add(Fn fn)
	{
		uint32_t id = m_nextId++;
		m_entries.push_back({id, std::move(fn)});
		return id;
	}

	void Remove(RTVForwardHandle id)
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

// Fired when a map vote is about to start.
// isRTV is true for !rtv-triggered votes,
// false for end-of-map / nextmap votes.
// Blockable: return true to prevent the vote from starting.
using OnMapVoteStartFn = std::function<bool(bool isRTV)>;

// Fired when a map vote finishes with a winning map.
// winnerMap is the clean changelevel name.
// Not fired when a vote ends with no result (tie/no-change).
using OnMapVoteEndFn = std::function<void(const char *winnerMap, bool isRTV)>;

// Fired when a winning map has been chosen and the changelevel is scheduled.
// delaySecs is how long until the change executes.
using OnMapChangeScheduledFn = std::function<void(const char *mapName, int delaySecs)>;

class ICS2RTVForwards
{
public:
	// Register a callback.
	// Returns a handle that can be passed to the matching Unregister* method.
	// Call Unregister* from your plugin's Unload() to prevent cs2rockthevote
	// from invoking callbacks into an unloaded DLL.
	virtual RTVForwardHandle RegisterOnMapVoteStart(OnMapVoteStartFn callback) = 0;
	virtual RTVForwardHandle RegisterOnMapVoteEnd(OnMapVoteEndFn callback) = 0;
	virtual RTVForwardHandle RegisterOnMapChangeScheduled(OnMapChangeScheduledFn callback) = 0;

	virtual void UnregisterOnMapVoteStart(RTVForwardHandle handle) = 0;
	virtual void UnregisterOnMapVoteEnd(RTVForwardHandle handle) = 0;
	virtual void UnregisterOnMapChangeScheduled(RTVForwardHandle handle) = 0;
};

class CS2RTVForwards : public ICS2RTVForwards
{
public:
	// Called from CS2RTVPlugin::Unload() to drop all registered callbacks so that
	// no lambda from another (possibly already-unloaded) plugin can be invoked.
	void Shutdown()
	{
		m_onMapVoteStart.Clear();
		m_onMapVoteEnd.Clear();
		m_onMapChangeScheduled.Clear();
	}

	RTVForwardHandle RegisterOnMapVoteStart(OnMapVoteStartFn callback) override
	{
		return m_onMapVoteStart.Add(std::move(callback));
	}

	RTVForwardHandle RegisterOnMapVoteEnd(OnMapVoteEndFn callback) override
	{
		return m_onMapVoteEnd.Add(std::move(callback));
	}

	RTVForwardHandle RegisterOnMapChangeScheduled(OnMapChangeScheduledFn callback) override
	{
		return m_onMapChangeScheduled.Add(std::move(callback));
	}

	void UnregisterOnMapVoteStart(RTVForwardHandle h) override
	{
		m_onMapVoteStart.Remove(h);
	}

	void UnregisterOnMapVoteEnd(RTVForwardHandle h) override
	{
		m_onMapVoteEnd.Remove(h);
	}

	void UnregisterOnMapChangeScheduled(RTVForwardHandle h) override
	{
		m_onMapChangeScheduled.Remove(h);
	}

	// Fire forwards.

	// Returns true if any callback blocked the vote.
	bool FireOnMapVoteStart(bool isRTV)
	{
		for (auto &e : m_onMapVoteStart.m_entries)
		{
			if (e.fn(isRTV))
			{
				return true;
			}
		}
		return false;
	}

	void FireOnMapVoteEnd(const char *winnerMap, bool isRTV)
	{
		for (auto &e : m_onMapVoteEnd.m_entries)
		{
			e.fn(winnerMap, isRTV);
		}
	}

	void FireOnMapChangeScheduled(const char *mapName, int delaySecs)
	{
		for (auto &e : m_onMapChangeScheduled.m_entries)
		{
			e.fn(mapName, delaySecs);
		}
	}

private:
	RTVForwardList<OnMapVoteStartFn> m_onMapVoteStart;
	RTVForwardList<OnMapVoteEndFn> m_onMapVoteEnd;
	RTVForwardList<OnMapChangeScheduledFn> m_onMapChangeScheduled;
};

extern CS2RTVForwards g_CS2RTVForwards;

#endif // _INCLUDE_RTV_FORWARDS_H_
