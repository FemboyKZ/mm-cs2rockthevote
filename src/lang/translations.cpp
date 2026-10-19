#include "translations.h"

#include "game/cvarquery.h"

mmu::Translations g_RTVTranslations;

std::string RTV_Translate(int slot, const char *phrase)
{
	return mmu::cvarquery::Translate(g_RTVTranslations, slot, phrase);
}
