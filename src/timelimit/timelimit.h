#ifndef _INCLUDE_RTV_TIMELIMIT_H_
#define _INCLUDE_RTV_TIMELIMIT_H_

// Reads and extends whichever convar family actually ends the current map.
// Plain CS2 uses mp_timelimit. cs2kz-metamod runs the map as one long round,
// so it mirrors mp_timelimit and the mp_roundtime to one value and caps them at 1440.
// Nothing here links against cs2kz. The mode comes from the convar bounds instead.

enum class LimitSource
{
	None,
	TimeLimit, // mp_timelimit
	RoundTime, // mp_roundtime family
};

struct ExtendResult
{
	bool applied = false; // the limit actually moved
	bool partial = false; // granted less than requested because the cap was hit
	int granted = 0;
	float capMinutes = 0.0f; // 0 when the convar has no max
};

class RTVTimeLimit
{
public:
	void OnMapStart(float curtime);

	LimitSource GetSource() const;

	// Returns false when no time limit applies.
	// `seconds` goes negative once the limit has already passed, which callers still need to act on.
	bool GetTimeLeftSeconds(float &seconds) const;

	// Minutes that still fit under the convar max, or -1 when it has no max.
	int GetHeadroomMinutes() const;

	// True when an extend would move the limit and the per-map budget allows it.
	bool IsExtendAvailable() const;

	// Adds `minutes`, trimmed to whatever fits under the convar max.
	ExtendResult Extend(int minutes);

	// Logs the non-time conditions that can end the map before any extension pays off.
	void LogCompetingEndConditions() const;

private:
	float m_mapStartTime = 0.0f;
	int m_extendsUsed = 0;
};

extern RTVTimeLimit g_RTVTimeLimit;

// Shared handler behind !extend and mm_extend. minutes <= 0 uses the configured default.
void RTV_CommandExtend(int slot, int minutes);

void RTV_AnnounceExtend(const ExtendResult &res);

#endif // _INCLUDE_RTV_TIMELIMIT_H_
