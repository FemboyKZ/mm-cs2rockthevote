#ifndef _INCLUDE_RTV_TIMER_SYSTEM_H_
#define _INCLUDE_RTV_TIMER_SYSTEM_H_

#include <functional>
#include <vector>

using TimerCallback = std::function<void()>;

// A timer ID of -1 means invalid/not running.
static constexpr int INVALID_TIMER = -1;

struct TimerEntry
{
	int id = INVALID_TIMER;
	float executeTime = 0.0f; // game time when next fire should occur
	float interval = 0.0f;    // 0 = single-fire. >0 = repeating
	TimerCallback callback;
	bool killed = false;
};

class RTVTimerSystem
{
public:
	// Schedule callback to run after 'delay' seconds (game time).
	// If interval > 0 the timer repeats every 'interval' seconds after first fire.
	// Returns a timer ID that can be used with KillTimer().
	int CreateTimer(float delay, TimerCallback callback, float interval = 0.0f);

	// Cancel and remove a timer. Safe to call with INVALID_TIMER.
	void KillTimer(int id);

	// Run a pending timer's callback now and end it, repeating or not. Not from inside a callback.
	void FireNow(int id);

	// Called from Hook_GameFrame every tick.
	void Process(float curtime);

	// Kill all timers (e.g. on map end).
	void KillAll();

private:
	std::vector<TimerEntry> m_timers;
	int m_nextId = 1;
};

extern RTVTimerSystem g_Timers;

#endif // _INCLUDE_RTV_TIMER_SYSTEM_H_
