#include "timelimit.h"
#include "mmu/log.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/entity/cgamerules.h"
#include "src/rtv/rtv_manager.h"
#include "src/utils/print_utils.h"

#include <tier1/convar.h>

#include <algorithm>
#include <cmath>

RTVTimeLimit g_RTVTimeLimit;

// Vanilla mp_roundtime max. Above it, a plugin is running the map as one round.
static constexpr float kVanillaRoundTimeCap = 60.0f;

// Function-local so the refs bind whenever the game registers the convar.
static CConVarRef<float> &TimeLimitRef()
{
	static CConVarRef<float> ref("mp_timelimit");
	return ref;
}

static CConVarRef<float> &RoundTimeRef()
{
	static CConVarRef<float> ref("mp_roundtime");
	return ref;
}

static CConVarRef<float> &RoundTimeDefuseRef()
{
	static CConVarRef<float> ref("mp_roundtime_defuse");
	return ref;
}

static CConVarRef<float> &RoundTimeHostageRef()
{
	static CConVarRef<float> ref("mp_roundtime_hostage");
	return ref;
}

static bool Usable(const CConVarRef<float> &ref)
{
	return ref.IsValidRef() && ref.IsConVarDataAvailable();
}

static bool ReadCap(const CConVarRef<float> &ref, float &cap)
{
	if (!Usable(ref) || !ref.HasMax())
	{
		return false;
	}
	cap = ref.GetMax();
	return true;
}

using RoundTimeRefFn = CConVarRef<float> &(*)();
static const RoundTimeRefFn kRoundTimeRefs[] = {RoundTimeRef, RoundTimeDefuseRef, RoundTimeHostageRef};
static constexpr int kRoundTimeRefCount = static_cast<int>(sizeof(kRoundTimeRefs) / sizeof(kRoundTimeRefs[0]));

// Leaked on purpose, and allocated through memoverride so it lives in tier0's heap rather than ours.
// cs2kz-metamod saves whatever max it finds installed and restores it on its own unload,
// so this pointer has to stay readable after we are gone.
static CVValue_t *s_capValue = nullptr;
static CVValue_t *s_savedMax[kRoundTimeRefCount] = {};
static bool s_capInstalled[kRoundTimeRefCount] = {};

static bool OwnsRoundTimeCap()
{
	CConVarRef<float> &ref = RoundTimeRef();
	return s_capValue && Usable(ref) && ref.GetConVarData()->MaxValue() == s_capValue;
}

// Only an external cap raise implies a plugin pinning the round start,
// which is what makes the round clock readable here. Ours implies nothing.
static bool RoundStartIsPinned()
{
	float cap = 0.0f;
	return !OwnsRoundTimeCap() && ReadCap(RoundTimeRef(), cap) && cap > kVanillaRoundTimeCap;
}

static LimitSource ResolveSource()
{
	const std::string &mode = g_RTVConfig.extend.mode;

	if (mode == "roundtime")
	{
		return Usable(RoundTimeRef()) ? LimitSource::RoundTime : LimitSource::None;
	}
	if (mode == "timelimit")
	{
		return Usable(TimeLimitRef()) ? LimitSource::TimeLimit : LimitSource::None;
	}

	if (RoundStartIsPinned() && RoundTimeRef().Get() > 0.0f)
	{
		return LimitSource::RoundTime;
	}
	if (Usable(TimeLimitRef()) && TimeLimitRef().Get() > 0.0f)
	{
		return LimitSource::TimeLimit;
	}
	// A server wanting the round to bound the map without cs2kz says so with Mode "roundtime".
	return LimitSource::None;
}

static CConVarRef<float> &SourceRef(LimitSource src)
{
	return src == LimitSource::RoundTime ? RoundTimeRef() : TimeLimitRef();
}

// Convars already at `minutes` are skipped, so with cs2kz loaded the first write mirrors the rest for us.
static void WriteLimit(LimitSource src, float minutes)
{
	auto assign = [minutes](CConVarRef<float> &ref)
	{
		if (Usable(ref) && std::fabs(ref.Get() - minutes) > 0.001f)
		{
			ref.Set(minutes);
		}
	};

	if (src == LimitSource::RoundTime)
	{
		assign(RoundTimeRef());
		assign(RoundTimeDefuseRef());
		assign(RoundTimeHostageRef());

		// Without a mirroring plugin the map would still expire on the old mp_timelimit.
		CConVarRef<float> &tl = TimeLimitRef();
		if (Usable(tl) && tl.Get() > 0.0f && tl.Get() < minutes)
		{
			tl.Set(minutes);
		}
		return;
	}

	assign(TimeLimitRef());
}

void RTVTimeLimit::OnMapStart(float curtime)
{
	m_mapStartTime = curtime;
	m_extendsUsed = 0;
	ApplyRoundTimeCap();
	LogCompetingEndConditions();
}

void RTVTimeLimit::ApplyRoundTimeCap()
{
	int minutes = g_RTVConfig.extend.roundTimeCap;

	if (OwnsRoundTimeCap())
	{
		// Installed already, so a config reload only changes the value we point at.
		if (minutes > 0)
		{
			s_capValue->m_fl32Value = static_cast<float>(minutes);
		}
		else
		{
			RestoreRoundTimeCap();
		}
		return;
	}

	if (minutes <= kVanillaRoundTimeCap)
	{
		return;
	}

	float existing = 0.0f;
	if (ReadCap(RoundTimeRef(), existing) && existing > kVanillaRoundTimeCap)
	{
		return;
	}

	if (!s_capValue)
	{
		s_capValue = new CVValue_t(static_cast<float>(minutes));
	}
	s_capValue->m_fl32Value = static_cast<float>(minutes);

	for (int i = 0; i < kRoundTimeRefCount; i++)
	{
		CConVarRef<float> &ref = kRoundTimeRefs[i]();
		if (!Usable(ref))
		{
			continue;
		}
		ConVarData *data = ref.GetConVarData();
		s_savedMax[i] = data->HasMaxValue() ? data->MaxValue() : nullptr;
		data->SetMaxValue(s_capValue);
		s_capInstalled[i] = true;
	}

	if (s_capInstalled[0])
	{
		MMU_LOG_INFO("Raised the mp_roundtime maximum to %d minutes.\n", minutes);
	}
}

void RTVTimeLimit::RestoreRoundTimeCap()
{
	for (int i = 0; i < kRoundTimeRefCount; i++)
	{
		if (!s_capInstalled[i])
		{
			continue;
		}
		s_capInstalled[i] = false;

		CConVarRef<float> &ref = kRoundTimeRefs[i]();
		if (!Usable(ref))
		{
			continue;
		}

		// Reclaim only the pointer still ours.
		// Another plugin may have layered its own cap on top since, and taking that back would clobber theirs.
		ConVarData *data = ref.GetConVarData();
		if (data->MaxValue() != s_capValue)
		{
			continue;
		}

		if (s_savedMax[i])
		{
			data->SetMaxValue(s_savedMax[i]);
		}
		else
		{
			data->RemoveMaxValue();
		}
	}
}

LimitSource RTVTimeLimit::GetSource() const
{
	return ResolveSource();
}

bool RTVTimeLimit::GetTimeLeftSeconds(float &seconds) const
{
	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
	{
		return false;
	}
	float curtime = globals->curtime;
	LimitSource src = ResolveSource();
	if (src == LimitSource::None)
	{
		return false;
	}

	CConVarRef<float> &ref = SourceRef(src);
	float limit = ref.Get();
	if (limit <= 0.0f)
	{
		return false;
	}

	// The clock each limit actually counts from.
	float anchor = 0.0f;
	if (CCSGameRules *rules = RTV_FindGameRules())
	{
		bool ok = src == LimitSource::RoundTime ? rules->GetRoundStartTime(anchor) : rules->GetGameStartTime(anchor);
		if (ok)
		{
			seconds = anchor + limit * 60.0f - curtime;
			return true;
		}
	}

	// No gamerules or unresolved schema. cs2kz pins the round start to 0,
	// so the round clock still lands exactly, and the map clock falls back to an estimate.
	float elapsed = src == LimitSource::RoundTime && RoundStartIsPinned() ? curtime : curtime - m_mapStartTime;
	seconds = limit * 60.0f - elapsed;
	return true;
}

int RTVTimeLimit::GetHeadroomMinutes() const
{
	LimitSource src = ResolveSource();
	if (src == LimitSource::None)
	{
		return 0;
	}

	CConVarRef<float> &ref = SourceRef(src);
	float cap = 0.0f;
	if (!ReadCap(ref, cap))
	{
		return -1;
	}

	return (std::max)(0, static_cast<int>(std::floor(cap - ref.Get())));
}

bool RTVTimeLimit::IsExtendAvailable() const
{
	const ExtendCfg &cfg = g_RTVConfig.extend;
	if (!cfg.enabled || cfg.minutes <= 0)
	{
		return false;
	}
	if (cfg.maxExtends > 0 && m_extendsUsed >= cfg.maxExtends)
	{
		return false;
	}
	return GetHeadroomMinutes() != 0;
}

ExtendResult RTVTimeLimit::Extend(int minutes)
{
	ExtendResult res;

	if (minutes <= 0)
	{
		return res;
	}

	LimitSource src = ResolveSource();
	if (src == LimitSource::None)
	{
		return res;
	}

	float timeLeftBefore = 0.0f;
	GetTimeLeftSeconds(timeLeftBefore);

	CConVarRef<float> &ref = SourceRef(src);
	float before = ref.Get();
	float target = before + static_cast<float>(minutes);

	// An overdue map would swallow the extension, so rebase onto the time already spent.
	// Otherwise a 60 minute grant buys less than 60 minutes of play.
	if (timeLeftBefore < 0.0f)
	{
		target = before - timeLeftBefore / 60.0f + static_cast<float>(minutes);
	}

	float cap = 0.0f;
	if (ReadCap(ref, cap))
	{
		res.capMinutes = cap;
		if (before >= cap - 0.001f)
		{
			return res;
		}
		if (target > cap)
		{
			target = cap;
			res.partial = true;
		}
	}

	WriteLimit(src, target);

	// Read back rather than trusting the write.
	// The engine clamps on set, and a mirroring plugin may land on a different value than the one we asked for.
	float after = ref.Get();

	// Report the playable time gained from now, not the raw convar delta,
	// so the rebase above does not announce minutes that were already spent.
	float timeLeftAfter = 0.0f;
	GetTimeLeftSeconds(timeLeftAfter);
	float gained = timeLeftAfter - (std::max)(timeLeftBefore, 0.0f);

	res.granted = (std::max)(0, static_cast<int>(std::lround(gained / 60.0f)));
	res.applied = res.granted > 0;

	if (!res.applied)
	{
		res.partial = false;
		return res;
	}

	m_extendsUsed++;
	MMU_LOG_INFO("Map extended by %d minute(s), %s is now %.0f.\n", res.granted, ref.GetName(), after);
	return res;
}

void RTVTimeLimit::LogCompetingEndConditions() const
{
	ConVarRefAbstract ignoreWin("mp_ignore_round_win_conditions");
	bool winConditionsLive = !ignoreWin.IsValidRef() || ignoreWin.GetInt() == 0;

	if (winConditionsLive)
	{
		static const char *const roundLimits[] = {"mp_maxrounds", "mp_winlimit", "mp_fraglimit"};

		for (const char *name : roundLimits)
		{
			ConVarRefAbstract ref(name);
			if (ref.IsValidRef() && ref.GetInt() > 0)
			{
				MMU_LOG_WARN("%s is %d - the map can end on rounds before its time limit, so extending will not always keep it running.\n", name,
							 ref.GetInt());
			}
		}
	}

	ConVarRefAbstract builtinVote("mp_endmatch_votenextmap");
	if (builtinVote.IsValidRef() && builtinVote.GetInt() != 0)
	{
		MMU_LOG_WARN("mp_endmatch_votenextmap is enabled - the game's own next-map vote will compete with RTV's. Set it to 0.\n");
	}
}

void RTV_AnnounceExtend(const ExtendResult &res)
{
	if (!res.applied)
	{
		if (res.capMinutes > 0.0f)
		{
			RTV_ChatToAllT("The map cannot be extended - the server's %d minute time cap is already reached.", static_cast<int>(res.capMinutes));
		}
		else
		{
			RTV_ChatToAllT("The map cannot be extended.");
		}
		return;
	}

	if (res.partial)
	{
		RTV_ChatToAllT("Map extended by %d minute(s) - the server's %d minute time cap is now reached, so this is the last extension.", res.granted,
					   static_cast<int>(res.capMinutes));
		return;
	}

	float timeLeft = 0.0f;
	g_RTVTimeLimit.GetTimeLeftSeconds(timeLeft);
	RTV_ChatToAllT("Map extended by %d minute(s). About %d minute(s) left.", res.granted, (std::max)(0, static_cast<int>(timeLeft / 60.0f)));
}

void RTV_CommandExtend(int slot, int minutes)
{
	const ExtendCfg &cfg = g_RTVConfig.extend;

	if (!cfg.enabled)
	{
		RTV_PrintToChatT(slot, "Map extending is disabled.");
		return;
	}

	if (minutes <= 0)
	{
		minutes = cfg.minutes;
	}

	if (g_RTVManager.IsMapChangeScheduled())
	{
		RTV_PrintToChatT(slot, "A map change is already scheduled.");
		return;
	}

	if (g_RTVTimeLimit.GetSource() == LimitSource::None)
	{
		RTV_PrintToChatT(slot, "This map has no time limit to extend.");
		return;
	}

	ExtendResult res = g_RTVTimeLimit.Extend(minutes);
	RTV_AnnounceExtend(res);

	if (res.applied)
	{
		g_RTVManager.OnMapExtended();
	}
}
