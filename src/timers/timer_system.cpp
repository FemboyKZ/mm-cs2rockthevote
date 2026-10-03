#include "timer_system.h"

#include <algorithm>

RTVTimerSystem g_Timers;

int RTVTimerSystem::CreateTimer(float delay, TimerCallback callback, float interval)
{
	TimerEntry entry;
	entry.id = m_nextId++;
	entry.executeTime = 0.0f; // will be resolved on first Process() call
	entry.interval = interval;
	entry.callback = std::move(callback);
	entry.killed = false;

	entry.executeTime = -(delay); // negative  -> "not yet initialised"
	if (delay == 0.0f)
	{
		entry.executeTime = -0.001f;
	}

	m_timers.push_back(std::move(entry));
	return m_timers.back().id;
}

void RTVTimerSystem::KillTimer(int id)
{
	if (id == INVALID_TIMER)
	{
		return;
	}
	for (auto &e : m_timers)
	{
		if (e.id == id)
		{
			e.killed = true;
			return;
		}
	}
}

void RTVTimerSystem::Process(float curtime)
{
	const int count = static_cast<int>(m_timers.size());
	for (int i = 0; i < count; i++)
	{
		TimerEntry &entry = m_timers[i];

		if (entry.killed)
		{
			continue;
		}

		// If the entry's executeTime is negative it hasn't been initialised yet;
		// set it relative to the current game time.
		if (entry.executeTime <= 0.0f)
		{
			float delay = -entry.executeTime; // recover the original delay
			entry.executeTime = curtime + delay;
			continue; // fire no earlier than one tick from now
		}

		if (curtime < entry.executeTime)
		{
			continue;
		}

		// Fire
		if (entry.callback)
		{
			entry.callback();
		}

		// Re-fetch: callback may have pushed new entries, invalidating a cached ref.
		TimerEntry &fired = m_timers[i];
		if (fired.interval > 0.0f)
		{
			fired.executeTime = curtime + fired.interval;
		}
		else
		{
			fired.killed = true;
		}
	}

	m_timers.erase(std::remove_if(m_timers.begin(), m_timers.end(), [](const TimerEntry &e) { return e.killed; }), m_timers.end());
}

void RTVTimerSystem::KillAll()
{
	m_timers.clear();
}
