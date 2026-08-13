#ifndef _INCLUDE_RTV_ENTITY_CGAMERULES_H_
#define _INCLUDE_RTV_ENTITY_CGAMERULES_H_

#include "mmu/schema.h"
#include "mmu/entity/cbaseentity.h"
#include <entity2/entitysystem.h> // GameTime_t

// Minimal gamerules wrappers for the map and round clocks.
// Only the fields we read are declared.
// Layout/field names from CS2Fixes (src/cs2_sdk/entity/cgamerules.h).
class CGameRules
{
public:
	DECLARE_SCHEMA_CLASS(CGameRules)
};

class CCSGameRules : public CGameRules
{
public:
	DECLARE_SCHEMA_CLASS(CCSGameRules)

	SCHEMA_FIELD(GameTime_t, m_flGameStartTime)
	SCHEMA_FIELD(GameTime_t, m_fRoundStartTime)

	// The getters report resolution separately,
	// because an unresolved field reads as 0 and cs2kz legitimately pins both of these to 0.

	// Game time mp_timelimit counts from. Excludes warmup, and does not care when this plugin loaded.
	bool GetGameStartTime(float &out)
	{
		if (m_flGameStartTime_Offset() <= 0)
		{
			return false;
		}
		out = m_flGameStartTime().GetTime();
		return true;
	}

	// Game time mp_roundtime counts from.
	bool GetRoundStartTime(float &out)
	{
		if (m_fRoundStartTime_Offset() <= 0)
		{
			return false;
		}
		out = m_fRoundStartTime().GetTime();
		return true;
	}
};

// The networked entity (classname "cs_gamerules") that owns the CCSGameRules object.
class CCSGameRulesProxy : public CBaseEntity
{
public:
	DECLARE_SCHEMA_CLASS(CCSGameRulesProxy)

	SCHEMA_FIELD(CCSGameRules *, m_pGameRules)
};

// Cached per map, since the proxy entity is recreated on each level load.
CCSGameRules *RTV_FindGameRules();
void RTV_ResetGameRulesCache();

#endif // _INCLUDE_RTV_ENTITY_CGAMERULES_H_
