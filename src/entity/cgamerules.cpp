#include "cgamerules.h"
#include "src/common.h"

static CCSGameRules *s_pGameRules = nullptr;

CCSGameRules *RTV_FindGameRules()
{
	if (s_pGameRules || !g_pEntitySystem)
	{
		return s_pGameRules;
	}

	EntityInstanceByClassIter_t iter("cs_gamerules");
	if (CEntityInstance *inst = iter.First())
	{
		s_pGameRules = static_cast<CCSGameRulesProxy *>(inst)->m_pGameRules();
	}
	return s_pGameRules;
}

void RTV_ResetGameRulesCache()
{
	s_pGameRules = nullptr;
}
