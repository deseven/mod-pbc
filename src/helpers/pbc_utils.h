#ifndef MOD_PBC_UTILS_H
#define MOD_PBC_UTILS_H

#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <random>

// ---------------------------------------------------------------------------
// mod-pbc shared utility functions
//
// Pure string/template helpers and RNG — no game-object dependencies.
// ---------------------------------------------------------------------------

// On 64-bit, valid pointers are above first 64 KiB. Values in that range
// are garbage from playerbots bot packet processing.
#define PBC_PTR_VALID(p) (reinterpret_cast<uintptr_t>(p) > 0xFFFFu)

// ---------------------------------------------------------------------------
// Template substitution helpers
// ---------------------------------------------------------------------------

// Normalize all line endings to LF (\n). Converts \r\n → \n and standalone \r → \n.
// Call this once when loading prompt/card text from disk so all downstream code
// can assume consistent \n line endings.
void PBC_NormalizeNewlines(std::string& s);

// Expand literal "\n" escape sequences in-place (config prompts use \n for newlines).
void PBC_ExpandNewlineEscapes(std::string& s);

// Replace all occurrences of {key} with value in s, in-place.
void PBC_ReplaceToken(std::string& s, const std::string& key, const std::string& value);

// Remove any remaining {token} patterns in s, replacing them with "" and
// logging a warning for each unique unknown token found.
void PBC_CleanUnknownTokens(std::string& s);

// Strip lines from an annotated string that contain only annotation markers
// with no visible content (e.g. "[pet_info]\n").  Used for API responses
// to avoid sending empty template slots to the frontend.
void PBC_StripEmptyAnnotatedLines(std::string& s);

// ---------------------------------------------------------------------------
// Debug output helpers
// ---------------------------------------------------------------------------

// Truncate a long string for debug logging: if > maxLen symbols, return
// first headLen ... last tailLen.  Otherwise return the string unchanged.
std::string PBC_TruncateForDebug(const std::string& s, size_t maxLen = 2000, size_t headLen = 500, size_t tailLen = 1500);

// Replace curly braces with parentheses so the string is safe to pass
// through AzerothCore's fmt-based LOG macros (which double-format).
std::string PBC_SanitizeForFmt(const std::string& s);

// ---------------------------------------------------------------------------
// Chat / message helpers
// ---------------------------------------------------------------------------

// Strip WoW item/object link markup, keeping only the display name.
std::string PBC_SanitizeChatMessage(const std::string& msg);

// Case-insensitive check: does msg contain charName?
bool PBC_MentionsCharacter(const std::string& msg, const std::string& charName);

// ---------------------------------------------------------------------------
// String formatting helpers
// ---------------------------------------------------------------------------

// Build a natural-language list: "You see X, Y and Z nearby." or "" if empty.
std::string PBC_NaturalList(const std::vector<std::string>& items);

// Default relationship text for an unknown target.
std::string PBC_DefaultRelationshipText(const std::string& name);

// ---------------------------------------------------------------------------
// Locale helpers
// ---------------------------------------------------------------------------

// Returns the configured DBC locale constant from worldserver.conf's DBC.Locale
// (e.g. LOCALE_enUS=0, LOCALE_deDE=3).  Safe to call from any thread.
uint8_t PBC_GetDbcLocale();

// Reads a DBC string array at the configured locale index, falling back to
// index 0 (enUS) if the configured locale's string is empty.  For use with
// DBC fields like AreaTableEntry::area_name, CreatureFamilyEntry::Name, etc.
// Returns empty string_view if all locale entries are empty.
std::string_view PBC_DbcString(char const* const* localeStrArray);

// Get a localized creature name.  Checks creature_template_locale for the
// configured locale, falls back to CreatureTemplate::Name (enUS).
std::string PBC_GetCreatureName(uint32_t entry);

// Get a localized gameobject name.  Checks gameobject_template_locale for the
// configured locale, falls back to GameObjectTemplate::name (enUS).
std::string PBC_GetGameObjectName(uint32_t entry);

// Get a localized item name.  Checks item_template_locale for the configured
// locale, falls back to ItemTemplate::Name1 (enUS).
std::string PBC_GetItemName(uint32_t entry);

// Get localized quest text.  Checks quest_template_locale for the configured
// locale, falls back to the Quest object's built-in (enUS) text.
// Returns empty string if both locale and fallback are empty.
std::string PBC_GetQuestTitle(uint32_t questId);
std::string PBC_GetQuestDetails(uint32_t questId);
std::string PBC_GetQuestObjectives(uint32_t questId);
std::string PBC_GetQuestOfferRewardText(uint32_t questId);
std::string PBC_GetQuestCompletedText(uint32_t questId);

// ---------------------------------------------------------------------------
// Enum-to-string helpers (require SharedDefines.h in implementation)
// ---------------------------------------------------------------------------

// Convert class ID to human-readable name (e.g. CLASS_WARRIOR → "Warrior").
std::string PBC_ClassStr(uint8_t cls);

// Convert race ID to human-readable name (e.g. RACE_HUMAN → "Human").
std::string PBC_RaceStr(uint8_t race);

// Convert gender ID to human-readable name (GENDER_FEMALE → "female", else "male").
std::string PBC_GenderStr(uint8_t gender);



// ---------------------------------------------------------------------------
// RNG
// ---------------------------------------------------------------------------

// Thread-local Mersenne Twister — seeded from multiple entropy sources.
std::mt19937& PBC_GetRNG();

// Roll 0-99 and return true if result < chance.
bool PBC_RollChance(uint32_t chance);

// ---------------------------------------------------------------------------
// Time formatting helpers
// ---------------------------------------------------------------------------

// Format a time_t as "YYYY-MM-DD hh:ii:ss" (local time).
std::string PBC_FormatDateTime(time_t t);

// Format a time_t as "YYYY-MM-DD" (local time).
std::string PBC_FormatDate(time_t t);

#endif // MOD_PBC_UTILS_H
