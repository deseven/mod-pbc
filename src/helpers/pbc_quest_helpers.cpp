#include "pbc_quest_helpers.h"
#include "pbc_utils.h"
#include "pbc_bot_helpers.h"
#include "pbc_locales.h"
#include "pbc_group_helpers.h"
#include "pbc_config.h"
#include "ObjectMgr.h"
#include "Creature.h"
#include "CreatureData.h"
#include "GameObject.h"
#include "Player.h"
#include "Group.h"
#include "WorldSession.h"
#include "Log.h"

// ---------------------------------------------------------------------------
// Strip WoW quest text formatting codes.
// $b / $B  = line break
// $N / $n  = player name placeholder
// $R / $r  = player race placeholder
// $C / $c  = player class placeholder
// $G x:y;  = gender-conditional text
// ---------------------------------------------------------------------------
std::string PBC_StripWowTextCodes(const std::string& text)
{
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '$' && i + 1 < text.size())
        {
            char next = text[i + 1];
            // $b / $B -> newline
            if (next == 'b' || next == 'B')
            {
                result += '\n';
                ++i;
                continue;
            }
            // $N / $n / $R / $r / $C / $c -> skip placeholder
            if (next == 'N' || next == 'n' || next == 'R' || next == 'r' ||
                next == 'C' || next == 'c')
            {
                ++i;
                continue;
            }
            // $G x:y; -> skip entire conditional
            if (next == 'G' || next == 'g')
            {
                i += 2; // skip '$G'
                while (i < text.size() && text[i] != ';')
                    ++i;
                continue;
            }
        }
        result += text[i];
    }
    return result;
}

// ---------------------------------------------------------------------------
// Look up quest starter/ender NPC names from ObjectMgr relations.
// Returns a comma-separated list of creature/GO names for the given quest ID.
// ---------------------------------------------------------------------------

std::string PBC_GetQuestStarterNames(uint32 questId)
{
    std::string result;
    auto* relMap = sObjectMgr->GetCreatureQuestRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId)
        {
            std::string name = PBC_GetCreatureName(it->first);
            if (!name.empty())
            {
                if (!result.empty()) result += ", ";
                result += name;
            }
        }
    }
    // Also check gameobject starters
    auto* goRelMap = sObjectMgr->GetGOQuestRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId)
        {
            std::string name = PBC_GetGameObjectName(it->first);
            if (!name.empty())
            {
                if (!result.empty()) result += ", ";
                result += name;
            }
        }
    }
    return result;
}

std::string PBC_GetQuestEnderNames(uint32 questId)
{
    std::string result;
    auto* relMap = sObjectMgr->GetCreatureQuestInvolvedRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId)
        {
            std::string name = PBC_GetCreatureName(it->first);
            if (!name.empty())
            {
                if (!result.empty()) result += ", ";
                result += name;
            }
        }
    }
    // Also check gameobject enders
    auto* goRelMap = sObjectMgr->GetGOQuestInvolvedRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId)
        {
            std::string name = PBC_GetGameObjectName(it->first);
            if (!name.empty())
            {
                if (!result.empty()) result += ", ";
                result += name;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Determine the source type of quest starters/enders from ObjectMgr relations.
// Returns "person" if only creatures, "object" if only gameobjects,
// "person or object" if both, or "" if neither.
// ---------------------------------------------------------------------------

std::string PBC_GetQuestStarterType(uint32 questId)
{
    bool hasCreature = false, hasGO = false;
    auto* relMap = sObjectMgr->GetCreatureQuestRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId) { hasCreature = true; break; }
    }
    auto* goRelMap = sObjectMgr->GetGOQuestRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId) { hasGO = true; break; }
    }
    if (hasCreature && hasGO) return PBC_Localize("person or object");
    if (hasCreature) return PBC_Localize("person");
    if (hasGO) return PBC_Localize("object");
    return "";
}

std::string PBC_GetQuestEnderType(uint32 questId)
{
    bool hasCreature = false, hasGO = false;
    auto* relMap = sObjectMgr->GetCreatureQuestInvolvedRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId) { hasCreature = true; break; }
    }
    auto* goRelMap = sObjectMgr->GetGOQuestInvolvedRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId) { hasGO = true; break; }
    }
    if (hasCreature && hasGO) return PBC_Localize("person or object");
    if (hasCreature) return PBC_Localize("person");
    if (hasGO) return PBC_Localize("object");
    return "";
}

// ---------------------------------------------------------------------------
// Look up a single creature's gender from creature_template → model info.
// Returns PBC_Localize("male"), PBC_Localize("female"), or empty string.
// ---------------------------------------------------------------------------
std::string PBC_GetCreatureGender(uint32 entry)
{
    CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(entry);
    if (!ct)
        return "";

    CreatureModel const* model = ct->GetFirstValidModel();
    if (!model || model->CreatureDisplayID == 0)
        return "";

    CreatureModelInfo const* modelInfo = sObjectMgr->GetCreatureModelInfo(model->CreatureDisplayID);
    if (!modelInfo)
        return "";

    switch (modelInfo->gender)
    {
        case GENDER_MALE:   return PBC_Localize("male");
        case GENDER_FEMALE: return PBC_Localize("female");
        default:            return "";
    }
}

// ---------------------------------------------------------------------------
// Look up quest starter/ender names with gender appended for creatures.
// Returns e.g. "Gryan Stoutmantle (male), Wanted Poster" — gameobjects are
// included as plain names (no gender).
// ---------------------------------------------------------------------------
std::string PBC_GetQuestStarterNamesWithGender(uint32 questId)
{
    std::string result;

    // Creature starters
    auto* relMap = sObjectMgr->GetCreatureQuestRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId)
        {
            std::string name = PBC_GetCreatureName(it->first);
            if (!name.empty())
            {
                if (!result.empty()) result += ", ";
                result += name;
                std::string gender = PBC_GetCreatureGender(it->first);
                if (!gender.empty())
                {
                    result += " (";
                    result += gender;
                    result += ")";
                }
            }
        }
    }
    // Gameobject starters (no gender)
    auto* goRelMap = sObjectMgr->GetGOQuestRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId)
        {
            std::string name = PBC_GetGameObjectName(it->first);
            if (!name.empty())
            {
                if (!result.empty()) result += ", ";
                result += name;
            }
        }
    }
    return result;
}

std::string PBC_GetQuestEnderNamesWithGender(uint32 questId)
{
    std::string result;

    // Creature enders
    auto* relMap = sObjectMgr->GetCreatureQuestInvolvedRelationMap();
    for (auto it = relMap->begin(); it != relMap->end(); ++it)
    {
        if (it->second == questId)
        {
            std::string name = PBC_GetCreatureName(it->first);
            if (!name.empty())
            {
                if (!result.empty()) result += ", ";
                result += name;
                std::string gender = PBC_GetCreatureGender(it->first);
                if (!gender.empty())
                {
                    result += " (";
                    result += gender;
                    result += ")";
                }
            }
        }
    }
    // Gameobject enders (no gender)
    auto* goRelMap = sObjectMgr->GetGOQuestInvolvedRelationMap();
    for (auto it = goRelMap->begin(); it != goRelMap->end(); ++it)
    {
        if (it->second == questId)
        {
            std::string name = PBC_GetGameObjectName(it->first);
            if (!name.empty())
            {
                if (!result.empty()) result += ", ";
                result += name;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// String substitution for quest prompt placeholders.
// ---------------------------------------------------------------------------
std::string PBC_SubstituteQuestVars(const std::string& tmpl,
                                     const std::string& title,
                                     const std::string& description,
                                     const std::string& logDescription,
                                     const std::string& completionLog,
                                     const std::string& rewardText,
                                     const std::string& questGiver,
                                     const std::string& questEnder,
                                     const std::string& questGiverType,
                                     const std::string& questEnderType)
{
    std::string result = tmpl;
    PBC_ReplaceToken(result, "quest_title",          title);
    PBC_ReplaceToken(result, "quest_description",    description);
    PBC_ReplaceToken(result, "quest_log_description", logDescription);
    PBC_ReplaceToken(result, "quest_completion_log",  completionLog);
    PBC_ReplaceToken(result, "quest_reward_text",    rewardText);
    PBC_ReplaceToken(result, "quest_giver",          questGiver);
    PBC_ReplaceToken(result, "quest_ender",          questEnder);
    PBC_ReplaceToken(result, "quest_giver_type",     questGiverType);
    PBC_ReplaceToken(result, "quest_ender_type",     questEnderType);
    return result;
}

// ---------------------------------------------------------------------------
// Validate that a quest has enough meaningful data to warrant an LLM event.
// PvP rank quests and other placeholder quests with no real title, no
// description, and no giver/ender are filtered out.
// ---------------------------------------------------------------------------
bool PBC_IsQuestValidForEvent(Quest const* quest)
{
    if (!quest) return false;

    std::string title = PBC_StripWowTextCodes(quest->GetTitle());
    if (title.empty()) return false;

    std::string description = PBC_StripWowTextCodes(quest->GetDetails());
    if (description.empty()) return false;

    std::string giverNames = PBC_GetQuestStarterNames(quest->GetQuestId());
    std::string enderNames = PBC_GetQuestEnderNames(quest->GetQuestId());
    if (giverNames.empty() && enderNames.empty()) return false;

    return true;
}

// ---------------------------------------------------------------------------
// Common guard checks for quest events.
// Returns true if the event should proceed.
// ---------------------------------------------------------------------------
bool PBC_QuestEventGuard(Player* player)
{
    if (!g_PBC_Enable) return false;
    if (!PBC_PTR_VALID(player)) return false;

    Group* grp = player->GetGroup();
    if (!grp) return false;
    if (grp->GetLeaderGUID() != player->GetGUID()) return false;

    WorldSession* sess = player->GetSession();
    bool leaderIsReal = PBC_IsRealPlayerSession(sess);
    if (!leaderIsReal && !PBC_BotIsGroupedWithRealPlayer(player)) return false;

    return true;
}
