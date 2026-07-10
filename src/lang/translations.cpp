#include "translations.h"

mmu::Translations g_RTVTranslations;

std::string RTV_Translate(int slot, const char *phrase)
{
	return g_RTVTranslations.Translate(RTV_SlotLanguage(slot), phrase ? phrase : "");
}
