#include "pbc_player_scripts.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_utils.h"
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

static bool IsBlacklisted(const std::string& msg)
{
    for (const auto& prefix : g_PBC_Blacklist)
        if (!prefix.empty() && msg.rfind(prefix, 0) == 0)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// HandleChatMessage  (shared for all chat types)
// ---------------------------------------------------------------------------

static void HandleChatMessage(Player* sender, uint32 type, const std::string& rawMsg,
                               Player* whisperTarget = nullptr)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(sender)) return;
    if (type == CHAT_MSG_AFK || type == CHAT_MSG_DND || type == CHAT_MSG_SYSTEM ||
        type == CHAT_MSG_MONSTER_SAY || type == CHAT_MSG_MONSTER_YELL ||
        type == CHAT_MSG_MONSTER_WHISPER || type == CHAT_MSG_MONSTER_EMOTE ||
        type == CHAT_MSG_MONSTER_PARTY || type == CHAT_MSG_RAID_BOSS_WHISPER ||
        type == CHAT_MSG_RAID_BOSS_EMOTE)
        return;
    if (IsBlacklisted(rawMsg)) return;

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

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                                          std::string& msg, Player* receiver)
{
    HandleChatMessage(player, type, msg, receiver);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                                          std::string& msg)
{
    HandleChatMessage(player, type, msg);
    return true;
}

bool PBC_PlayerEvents::OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                                          std::string& msg, Group* /*group*/)
{
    HandleChatMessage(player, type, msg);
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

    std::string itemName = tmpl->Name1;
    std::string phrase   = PBC_BuildItemPhrase(tmpl);

    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine("The party has found " + phrase + " named " + itemName),
        PBC_MakeHistLine("The party acquired " + phrase + " named " + itemName),
        g_PBC_ReplyChanceItem);
}

void PBC_PlayerEvents::OnPlayerQuestRewardItem(Player* player, Item* item, uint32 /*count*/)
{
    if (!PBC_QuestEventGuard(player) || !PBC_PTR_VALID(item)) return;
    if (!item->IsInWorld()) return;

    ItemTemplate const* tmpl = item->GetTemplate();
    if (!tmpl || tmpl->Quality < ITEM_QUALITY_RARE) return;
    if (!(PBC_LOOT_EVENT_ITEM_CLASSES & (1u << tmpl->Class))) return;

    std::string itemName = tmpl->Name1;
    std::string phrase   = PBC_BuildItemPhrase(tmpl);

    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine("The party has been rewarded with " + phrase + " named " + itemName),
        PBC_MakeHistLine("The party was rewarded with " + phrase + " named " + itemName),
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

    std::string itemName = tmpl->Name1;
    std::string phrase   = PBC_BuildItemPhrase(tmpl);

    PBC_DispatchGroupEvent(player,
        PBC_MakeEventLine("The party has found " + phrase + " named " + itemName),
        PBC_MakeHistLine("The party acquired " + phrase + " named " + itemName),
        g_PBC_ReplyChanceItem);
}

void PBC_PlayerEvents::OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type)
{
    if (!g_PBC_Enable) return;
    if (!PBC_PTR_VALID(winner) || !PBC_PTR_VALID(loser) || type != DUEL_WON) return;
    PBC_DispatchGroupEvent(winner,
        PBC_MakeEventLine(winner->GetName() + " just won the duel against " + loser->GetName()),
        PBC_MakeHistLine(winner->GetName() + " won the duel against " + loser->GetName()),
        g_PBC_ReplyChanceDuel);
}

void PBC_PlayerEvents::OnPlayerLevelChanged(Player* player, uint8 oldLevel)
{
    if (!g_PBC_Enable || !PBC_PTR_VALID(player)) return;

    if (player->GetLevel() % 5 != 0) return;

    static const char* levelUpEventPhrases[] = {
        " can feel their abilities growing stronger",
        " grows more powerful, their skills sharpened by experience",
        " has grown stronger through their trials and hardships",
        " feels a surge of power as their abilities improve",
        " has become more capable, their experience forging them anew",
    };
    static const char* levelUpHistPhrases[] = {
        " grew stronger",
        " became more powerful through experience",
        " emerged from their trials more capable than before",
        " felt their abilities sharpen and grow",
        " gained new strength and skill",
    };

    const std::string& name = player->GetName();
    int idx = std::uniform_int_distribution<int>(0, 4)(PBC_GetRNG());
    std::string eventLine = PBC_MakeEventLine(name + levelUpEventPhrases[idx]);
    std::string histLine  = PBC_MakeHistLine(name + levelUpHistPhrases[idx]);

    PBC_DispatchGroupEvent(player, eventLine, histLine, g_PBC_ReplyChanceLevelUp);
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

    if (g_PBC_QuestCompletedSystemPrompt.empty() || g_PBC_QuestCompletedUserPrompt.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "OnPlayerCompleteQuest: prompts not configured, skipping");
        return;
    }

    std::string questTitle          = PBC_StripWowTextCodes(quest->GetTitle());
    std::string questDescription    = PBC_StripWowTextCodes(quest->GetDetails());
    std::string questLogDescription = PBC_StripWowTextCodes(quest->GetObjectives());
    std::string questCompletionLog  = PBC_StripWowTextCodes(quest->GetCompletedText());
    std::string questRewardText     = PBC_StripWowTextCodes(quest->GetOfferRewardText());
    std::string questGiver          = PBC_GetQuestStarterNames(quest->GetQuestId());
    std::string questEnder          = PBC_GetQuestEnderNames(quest->GetQuestId());
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
    ev.chatType           = CHAT_MSG_PARTY;
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
