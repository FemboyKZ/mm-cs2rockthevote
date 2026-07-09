#ifndef _INCLUDE_RTV_TRANSLATIONS_H_
#define _INCLUDE_RTV_TRANSLATIONS_H_

#include <string>
#include <unordered_map>

// SourceMod-style phrase tables for RTV's chat and menu text, resolved per client language.
// Phrase values may embed {color} tags.
// Values keep any printf-style format specifiers, the caller formats after translating.
class RTVTranslations
{
public:
	// Load config.txt (cl_language -> short key map)
	// and every *.phrases.txt under <baseDir>/addons/cs2rockthevote/translations.
	void Load(const char *baseDir);

	// Language key used when a client's language is unknown or a phrase lacks it.
	void SetDefaultLanguage(const std::string &lang);

	// Map a raw cl_language value ("english") to a phrase-file key ("en").
	// Unmapped values are returned as-is (lowercased), so Translate can fall back.
	std::string MapClientLanguage(const char *clLanguage) const;

	// Translate a phrase for a language. Falls back to the default language,
	// then to the phrase key itself, so unknown phrases pass through unchanged
	// (format specifiers intact, colors absent).
	std::string Translate(const std::string &lang, const std::string &phrase) const;

private:
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_phrases; // phrase -> lang -> text
	std::unordered_map<std::string, std::string> m_languageMap;                              // cl_language -> short key
	std::string m_defaultLang = "en";
};

extern RTVTranslations g_RTVTranslations;

// Defined in cs2rockthevote.cpp.
// Returns the mapped phrase-file language key for the client in `slot`, or the default language when unavailable.
std::string RTV_SlotLanguage(int slot);

// Translate `phrase` into the language of the client in `slot`.
std::string RTV_Translate(int slot, const char *phrase);

#endif // _INCLUDE_RTV_TRANSLATIONS_H_
