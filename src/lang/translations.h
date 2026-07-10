#ifndef _INCLUDE_RTV_TRANSLATIONS_H_
#define _INCLUDE_RTV_TRANSLATIONS_H_

#include "mmu/translations.h"

#include <string>

// SourceMod-style phrase tables for RTV's chat and menu text, resolved per client language.
// Phrase values may embed {color} tags.
extern mmu::Translations g_RTVTranslations;

// Defined in cs2rockthevote.cpp.
// Returns the mapped phrase-file language key for the client in `slot`, or the default language when unavailable.
std::string RTV_SlotLanguage(int slot);

// Translate `phrase` into the language of the client in `slot`.
// Convenience wrapper over g_RTVTranslations for building menu titles and labels.
std::string RTV_Translate(int slot, const char *phrase);

#endif // _INCLUDE_RTV_TRANSLATIONS_H_
