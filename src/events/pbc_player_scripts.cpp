#include "pbc_player_scripts.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_utils.h"
#include "pbc_locales.h"
#include "pbc_item_helpers.h"
#include "pbc_quest_helpers.h"
#include "pbc_group_helpers.h"
#include "pbc_event_dispatch.h"
#include "pbc_poll.h"
#include "pbc_http.h"
#include "pbc_log.h"

#include "CharacterCache.h"
#include "Player.h"
#include "Creature.h"
#include "Item.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "SharedDefines.h"
#include "QuestDef.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "GameTime.h"

#include <algorithm>
#include <ctime>
#include <random>

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

static bool IsBlacklisted(uint32 lang, const std::string& msg)
{
    // When IgnoreAllAddonMessages is enabled, drop ALL addon traffic silently.
    if (lang == LANG_ADDON && g_PBC_IgnoreAllAddonMessages)
        return true;

    for (const auto& prefix : g_PBC_Blacklist)
        if (!prefix.empty() && msg.rfind(prefix, 0) == 0)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// HandleChatMessage  (shared for all chat types)
// ---------------------------------------------------------------------------

static void HandleChatMessage(Player* sender, uint32 type, uint32 lang,
                               const std::string& rawMsg,
                               Player* whisperTarget = nullptr)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(sender)) return;
    if (type == CHAT_MSG_AFK || type == CHAT_MSG_DND) return;
    if (IsBlacklisted(lang, rawMsg)) return;

    const std::string msg = PBC_SanitizeChatMessage(rawMsg);

    // --- Whisper path ---
    if (type == CHAT_MSG_WHISPER)
    {
        if (!PBC_PTR_VALID(whisperTarget)
            || !whisperTarget->GetSession()
            || !whisperTarget->GetSession()->IsBot())
            return;

        PBC_DispatchWhisperEvent(sender, whisperTarget, msg);
        return;
    }

    // --- Say / Yell / Group / Raid ---
    bool senderIsBot = sender->GetSession() && sender->GetSession()->IsBot();

    if (senderIsBot)
        return;

    bool isGroupChat = (type == CHAT_MSG_PARTY || type == CHAT_MSG_PARTY_LEADER ||
                        type == CHAT_MSG_RAID  || type == CHAT_MSG_RAID_LEADER  ||
                        type == CHAT_MSG_RAID_WARNING);

    // --- Guild chat ---
    if (type == CHAT_MSG_GUILD && g_PBC_EnableGuildChat)
    {
        PBC_DispatchGuildMessageEvent(sender, msg);
        return;
    }

    PBC_DispatchPartyMessageEvent(sender, msg, "", type, isGroupChat);
}

// ---------------------------------------------------------------------------
// PBC_PlayerEvents
// ---------------------------------------------------------------------------

PBC_PlayerEvents::PBC_PlayerEvents() : PlayerScript("PBC_PlayerEvents",
{
    PLAYERHOOK_CAN_PLAYER_USE_CHAT,
    PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT,
    PLAYERHOOK_CAN_PLAYER_USE_GROUP_CHAT,
    PLAYERHOOK_CAN_PLAYER_USE_GUILD_CHAT,
    PLAYERHOOK_ON_LOGIN,
    PLAYERHOOK_ON_LOGOUT,
    PLAYERHOOK_ON_LOOT_ITEM,
    PLAYERHOOK_ON_QUEST_REWARD_ITEM,
    PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM,
    PLAYERHOOK_ON_DUEL_END,
    PLAYERHOOK_ON_LEVEL_CHANGED,
    PLAYERHOOK_ON_CREATURE_KILL,
    PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
    PLAYERHOOK_ON_PLAYER_JUST_DIED,
}) {}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                                          std::string& msg, Player* receiver)
{
    HandleChatMessage(player, type, lang, msg, receiver);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                                          std::string& msg)
{
    HandleChatMessage(player, type, lang, msg);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                                          std::string& msg, Group* /*group*/)
{
    HandleChatMessage(player, type, lang, msg);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang,
                                          std::string& msg, Guild* /*guild*/)
{
    HandleChatMessage(player, type, lang, msg);
    return true;
}

void PBC_PlayerEvents::OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player) || !PBC_PTR_VALID(item)) return;
    if (!item->IsInWorld()) return;

    ItemTemplate const* tmpl = item->GetTemplate();
    if (!tmpl || tmpl->Quality < ITEM_QUALITY_RARE) return;
    if (!(PBC_LOOT_EVENT_ITEM_CLASSES & (1u << tmpl->Class))) return;

    std::string itemName = PBC_GetItemName(item->GetEntry());
    if (itemName.empty())
        itemName = tmpl->Name1;
    std::string phrase   = PBC_BuildItemPhrase(tmpl);

    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine(PBC_Localize("The party has found {0} named {1}", phrase, itemName)),
        PBC_Localize("The party acquired {0} named {1}", phrase, itemName),
        g_PBC_ReplyChanceItem);
}

void PBC_PlayerEvents::OnPlayerQuestRewardItem(Player* player, Item* item, uint32 /*count*/)
{
    if (!PBC_QuestEventGuard(player) || !PBC_PTR_VALID(item)) return;
    if (!item->IsInWorld()) return;

    ItemTemplate const* tmpl = item->GetTemplate();
    if (!tmpl || tmpl->Quality < ITEM_QUALITY_RARE) return;
    if (!(PBC_LOOT_EVENT_ITEM_CLASSES & (1u << tmpl->Class))) return;

    std::string itemName = PBC_GetItemName(item->GetEntry());
    if (itemName.empty())
        itemName = tmpl->Name1;
    std::string phrase   = PBC_BuildItemPhrase(tmpl);

    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine(PBC_Localize("The party has been rewarded with {0} named {1}", phrase, itemName)),
        PBC_Localize("The party was rewarded with {0} named {1}", phrase, itemName),
        g_PBC_ReplyChanceItem);
}

void PBC_PlayerEvents::OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 /*count*/, RollVote /*voteType*/, Roll* /*roll*/)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player) || !PBC_PTR_VALID(item)) return;
    if (!item->IsInWorld()) return;

    ItemTemplate const* tmpl = item->GetTemplate();
    if (!tmpl || tmpl->Quality < ITEM_QUALITY_RARE) return;
    if (!(PBC_LOOT_EVENT_ITEM_CLASSES & (1u << tmpl->Class))) return;

    std::string itemName = PBC_GetItemName(item->GetEntry());
    if (itemName.empty())
        itemName = tmpl->Name1;
    std::string phrase   = PBC_BuildItemPhrase(tmpl);

    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine(PBC_Localize("The party has found {0} named {1}", phrase, itemName)),
        PBC_Localize("The party acquired {0} named {1}", phrase, itemName),
        g_PBC_ReplyChanceItem);
}

void PBC_PlayerEvents::OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(winner) || !PBC_PTR_VALID(loser) || type != DUEL_WON) return;
    uint8_t winnerGender = winner->getGender();
    PBC_DispatchGroupEvent(winner,
        PBC_MakeEventLine(PBC_Localize("{0} just won the duel against {1}", winner->GetName(), loser->GetName(), winnerGender)),
        PBC_Localize("{0} won the duel against {1}", winner->GetName(), loser->GetName(), winnerGender),
        g_PBC_ReplyChanceDuel);
}

void PBC_PlayerEvents::OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/)
{
    if (!g_PBC_Enable || !PBC_PTR_VALID(player)) return;

    if (player->GetLevel() % 5 != 0) return;

    // Event phrases use present tense ("happening now" for in-game chat).
    // History phrases use past tense ("happened earlier" for DB storage).
    // Each pair shares the same wording, differing only in tense.
    static const char* levelUpEventPhrases[] = {
        " grows stronger",
        " becomes more powerful through experience",
        " emerges from their trials more capable than before",
        " feels their abilities sharpen and grow",
        " gains new strength and skill",
    };
    static const char* levelUpHistPhrases[] = {
        " grew stronger",
        " became more powerful through experience",
        " emerged from their trials more capable than before",
        " felt their abilities sharpen and grow",
        " gained new strength and skill",
    };

    const std::string& name = player->GetName();
    uint8_t gender = player->getGender();
    int idx = std::uniform_int_distribution<int>(0, 4)(PBC_GetRNG());
    std::string eventLine = PBC_MakeEventLine(name + PBC_Localize(levelUpEventPhrases[idx], gender));
    std::string narratorText = name + PBC_Localize(levelUpHistPhrases[idx], gender);

    PBC_DispatchGroupEvent(player, eventLine, narratorText, g_PBC_ReplyChanceLevelUp);
}

void PBC_PlayerEvents::OnPlayerCreatureKill(Player* killer, Creature* killed)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(killer) || !PBC_PTR_VALID(killed)) return;

    PBC_TrackGroupKill(killer, killed);
}

void PBC_PlayerEvents::OnPlayerJustDied(Player* player)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player)) return;

    Group* grp = player->GetGroup();
    if (!grp) return;

    uint32_t grpGuid = grp->GetGUID().GetCounter();

    {
        std::lock_guard<std::mutex> lock(g_PBC_PartyStateMutex);
        PBC_GroupCombatTracker& tracker = g_PBC_GroupCombatTrackers[grpGuid];

        if (!tracker.wasInCombat)
        {
            tracker.wasInCombat = true;
            tracker.combatStartTime = GameTime::GetGameTime().count();
            tracker.partySize = grp->GetMembersCount();
        }

        ++tracker.deadCount;
    }
}

void PBC_PlayerEvents::OnPlayerCompleteQuest(Player* player, Quest const* quest)
{
    if (!PBC_QuestEventGuard(player) || !quest) return;

    if (!PBC_IsQuestValidForEvent(quest))
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "OnPlayerCompleteQuest: quest '{}' (id={}) has no meaningful data, skipping",
                 PBC_StripWowTextCodes(quest->GetTitle()), quest->GetQuestId());
        return;
    }

    if (g_PBC_QuestCompletedSystemPrompt.empty() || g_PBC_QuestCompletedUserPrompt.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "OnPlayerCompleteQuest: prompts not configured, skipping");
        return;
    }

    std::string questTitle          = PBC_StripWowTextCodes(PBC_GetQuestTitle(quest->GetQuestId()));
    std::string questDescription    = PBC_StripWowTextCodes(PBC_GetQuestDetails(quest->GetQuestId()));
    std::string questLogDescription = PBC_StripWowTextCodes(PBC_GetQuestObjectives(quest->GetQuestId()));
    std::string questCompletionLog  = PBC_StripWowTextCodes(PBC_GetQuestCompletedText(quest->GetQuestId()));
    std::string questRewardText     = PBC_StripWowTextCodes(PBC_GetQuestOfferRewardText(quest->GetQuestId()));
    std::string questGiver          = PBC_GetQuestStarterNamesWithGender(quest->GetQuestId());
    std::string questEnder          = PBC_GetQuestEnderNamesWithGender(quest->GetQuestId());
    std::string questGiverType      = PBC_GetQuestStarterType(quest->GetQuestId());
    std::string questEnderType      = PBC_GetQuestEnderType(quest->GetQuestId());

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "OnPlayerCompleteQuest: leader={} quest='{}' (id={})",
             player->GetName(), questTitle, quest->GetQuestId());

    std::string userPrompt = PBC_SubstituteQuestVars(
        g_PBC_QuestCompletedUserPrompt,
        questTitle, questDescription, questLogDescription, questCompletionLog,
        questRewardText, questGiver, questEnder, questGiverType, questEnderType);

    PBC_EventItem ev;
    ev.type               = PBC_EventType::QuestSummarization;
    ev.chatType           = PBC_GetGroupChatType(player);
    ev.canCreateEvents    = true;
    ev.questSystemPrompt  = g_PBC_QuestCompletedSystemPrompt;
    ev.questUserPrompt    = userPrompt;
    ev.anchorObjGuid      = player->GetGUID();

    if (!PBC_RollGroupBotsIntoEvent(ev, player, g_PBC_ReplyChanceQuestCompleted, "quest-completed"))
        return;

    AddTrackedPlayersToEvent(ev, player);

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Login / Logout — WS "online" and "offline" events
// ---------------------------------------------------------------------------

void PBC_PlayerEvents::OnPlayerLogin(Player* player)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player)) return;

    uint64_t guid = player->GetGUID().GetCounter();
    uint32_t accountId = sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(guid));
    if (accountId == 0) return;

    PBC_WsNotifyAccount(accountId, "online", guid);
}

void PBC_PlayerEvents::OnPlayerLogout(Player* player)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(player)) return;

    uint64_t guid = player->GetGUID().GetCounter();
    uint32_t accountId = sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(guid));
    if (accountId == 0) return;

    PBC_WsNotifyAccount(accountId, "offline", guid);
}
