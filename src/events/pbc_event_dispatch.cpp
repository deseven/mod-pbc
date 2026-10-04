#include "pbc_event_dispatch.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_utils.h"
#include "pbc_locales.h"
#include "pbc_group_helpers.h"
#include "pbc_bot_helpers.h"
#include "pbc_log.h"

#include "Player.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "WorldSession.h"
#include "SharedDefines.h"

#include <fmt/core.h>
#include <algorithm>
#include <random>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Narrator formatting helpers
// ---------------------------------------------------------------------------
std::string PBC_MakeEventLine(const std::string& text) { return "*" + text + "*"; }
std::string PBC_MakeHistLine(const std::string& text)  { return PBC_Localize("Narrator: *{0}*", text); }

std::string PBC_MakeHistLineFromSource(const PBC_EventSource& source)
{
    if (source.IsNarrator())
        return PBC_MakeHistLine(source.narratorText);
    if (source.IsChat())
        return PBC_Localize("{0}: {1}", source.senderName, source.message);
    return "";
}

// ---------------------------------------------------------------------------
// PBC_NotifyRealPlayersInGroup
// ---------------------------------------------------------------------------
void PBC_NotifyRealPlayersInGroup(Player* anchor, const std::string& eventLine)
{
    if (!g_PBC_DisplayNarratorEvents) return;
    if (!PBC_PTR_VALID(anchor) || eventLine.empty()) return;

    auto sendTo = [&](Player* p)
    {
        if (!PBC_PTR_VALID(p) || !p->IsInWorld()) return;
        WorldSession* sess = p->GetSession();
        if (!PBC_IsRealPlayerSession(sess)) return;
        ChatHandler(sess).SendSysMessage(eventLine);
    };

    Group* grp = anchor->GetGroup();
    if (grp)
    {
        for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
            sendTo(ref->GetSource());
    }
    else
    {
        sendTo(anchor);
    }
}

// ---------------------------------------------------------------------------
// PBC_PushEvent
// ---------------------------------------------------------------------------
void PBC_PushEvent(PBC_EventItem item)
{
    std::lock_guard<std::mutex> lock(g_PBC_EventQueueMutex);
    g_PBC_EventQueue.push(std::move(item));
}

// ---------------------------------------------------------------------------
// AddTrackedPlayersToEvent
// ---------------------------------------------------------------------------
void AddTrackedPlayersToEvent(PBC_EventItem& ev, Player* anchor, bool subGroupOnly)
{
    if (!PBC_PTR_VALID(anchor)) return;

    auto realPlayers = subGroupOnly
        ? PBC_FindRealPlayersInSubGroup(anchor)
        : PBC_FindRealPlayersInGroup(anchor);
    for (Player* rp : realPlayers)
    {
        uint64_t guid = rp->GetGUID().GetCounter();
        ev.silentCharGuids.push_back(guid);
        ev.playerCharGuids.push_back(guid);
    }

    WorldSession* anchorSess = anchor->GetSession();
    if (PBC_IsRealPlayerSession(anchorSess))
    {
        uint64_t guid = anchor->GetGUID().GetCounter();
        if (std::find(ev.playerCharGuids.begin(), ev.playerCharGuids.end(), guid) == ev.playerCharGuids.end())
        {
            ev.silentCharGuids.push_back(guid);
            ev.playerCharGuids.push_back(guid);
        }
    }
}

// ---------------------------------------------------------------------------
// PBC_DispatchGroupEvent
// ---------------------------------------------------------------------------
void PBC_DispatchGroupEvent(Player* anchor, const std::string& eventLine,
                             const std::string& narratorText, uint32_t chance,
                             bool notifyRealPlayers)
{
    if (!PBC_PTR_VALID(anchor)) return;

    WorldSession* anchorSess = anchor->GetSession();
    bool anchorIsReal = PBC_IsRealPlayerSession(anchorSess);
    bool anchorIsBot  = PBC_IsBotSession(anchorSess);

    if (!anchorIsReal && !PBC_BotIsGroupedWithRealPlayer(anchor))
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "DispatchGroupEvent: skipped — no real player in group (anchor={})", anchor->GetName());
        return;
    }

    if (notifyRealPlayers)
        PBC_NotifyRealPlayersInGroup(anchor, eventLine);

    auto bots = PBC_FindGroupBots(anchor);
    if (bots.empty() && !anchorIsBot) return;

    std::shuffle(bots.begin(), bots.end(), PBC_GetRNG());

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "DispatchGroupEvent: anchor={} bots={} chance={}% event=\"{}\"",
             anchor->GetName(), bots.size(), chance, eventLine);

    PBC_EventItem ev;
    ev.type            = PBC_EventType::Normal;
    ev.eventLine       = eventLine;
    ev.source.narratorText = narratorText;
    // Use raid chat when the anchor is in a raid group so that members in
    // other sub-groups can see the responses; party chat otherwise.
    ev.chatType        = PBC_GetGroupChatType(anchor);
    ev.canCreateEvents = true;

    // Record the real player who triggered this event (for regen logging).
    if (anchorIsReal)
        ev.regenRequesterGuid = anchor->GetGUID().GetCounter();

    PBC_RollBotsWithPenalty(ev, bots, chance, "event");

    if (anchorIsBot)
        ev.silentCharGuids.push_back(anchor->GetGUID().GetCounter());

    AddTrackedPlayersToEvent(ev, anchor);

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_RollGroupBotsIntoEvent
// ---------------------------------------------------------------------------
bool PBC_RollGroupBotsIntoEvent(PBC_EventItem& ev, Player* player,
                                 uint32_t chance, const char* debugLabel)
{
    auto bots = PBC_FindGroupBots(player);
    if (bots.empty()) return false;

    std::shuffle(bots.begin(), bots.end(), PBC_GetRNG());
    PBC_RollBotsWithPenalty(ev, bots, chance, debugLabel);
    return true;
}

// ---------------------------------------------------------------------------
// PBC_RollBotsWithPenalty
// ---------------------------------------------------------------------------
void PBC_RollBotsWithPenalty(PBC_EventItem& ev,
                              const std::vector<Player*>& bots,
                              uint32_t baseChance,
                              const char* debugLabel)
{
    uint32_t currentChance = baseChance;
    for (Player* bot : bots)
    {
        if (currentChance == 0)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Roll {} character={} chance=0% -> silent (no chance left)",
                     debugLabel, bot->GetName());
            ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
            continue;
        }

        uint32_t effectiveChance = PBC_GetEffectiveChance(bot->GetGUID().GetCounter(), currentChance);
        bool rolled = PBC_RollChance(effectiveChance);
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "Roll {} character={} chance={}% (base={}% mod={}) -> {}",
                 debugLabel, bot->GetName(), effectiveChance, currentChance,
                 static_cast<int32_t>(effectiveChance) - static_cast<int32_t>(currentChance),
                 rolled ? "RESPOND" : "silent");
        if (rolled)
        {
            ev.respondingChars.push_back(PBC_SnapshotCharacter(bot));
            currentChance = currentChance > g_PBC_RollPenaltyOnAnswer
                ? currentChance - g_PBC_RollPenaltyOnAnswer : 0;
        }
        else
        {
            ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
        }
    }
}

// ---------------------------------------------------------------------------
// PBC_RollBotsForMessage
// ---------------------------------------------------------------------------
void PBC_RollBotsForMessage(PBC_EventItem& ev,
                             const std::vector<Player*>& bots,
                             const std::string& message)
{
    bool anyMention = false;
    for (Player* bot : bots)
        if (PBC_MentionsCharacter(message, bot->GetName())) { anyMention = true; break; }

    if (anyMention)
    {
        std::string lowerMsg = message;
        std::transform(lowerMsg.begin(), lowerMsg.end(), lowerMsg.begin(), ::tolower);

        std::vector<std::pair<size_t, Player*>> positions;
        for (Player* bot : bots)
        {
            std::string lname = bot->GetName();
            std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
            size_t pos = lowerMsg.find(lname);
            if (pos != std::string::npos)
                positions.emplace_back(pos, bot);
        }
        std::sort(positions.begin(), positions.end(),
                  [](const auto& a, const auto& b){ return a.first < b.first; });

        std::unordered_set<uint64_t> mentionedGuids;
        for (auto& [pos, bot] : positions)
        {
            mentionedGuids.insert(bot->GetGUID().GetCounter());
            uint32_t effectiveChance = PBC_GetEffectiveChance(bot->GetGUID().GetCounter(), g_PBC_ReplyChanceMention);
            bool rolled = PBC_RollChance(effectiveChance);
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Roll mention character={} chance={}% -> {}",
                     bot->GetName(), effectiveChance, rolled ? "RESPOND" : "silent");
            if (rolled)
                ev.respondingChars.push_back(PBC_SnapshotCharacter(bot));
            else
                ev.silentCharGuids.push_back(bot->GetGUID().GetCounter());
        }

        uint32_t mentionPenalty = g_PBC_RollPenaltyOnAnswer * mentionedGuids.size();
        uint32_t baseChance = g_PBC_ReplyChanceMention > mentionPenalty
            ? g_PBC_ReplyChanceMention - mentionPenalty : 0;

        std::vector<Player*> nonMentionedBots;
        for (Player* bot : bots)
            if (!mentionedGuids.count(bot->GetGUID().GetCounter()))
                nonMentionedBots.push_back(bot);

        std::shuffle(nonMentionedBots.begin(), nonMentionedBots.end(), PBC_GetRNG());
        PBC_RollBotsWithPenalty(ev, nonMentionedBots, baseChance, "mention-bystander");
    }
    else
    {
        std::vector<Player*> shuffledBots = bots;
        std::shuffle(shuffledBots.begin(), shuffledBots.end(), PBC_GetRNG());
        PBC_RollBotsWithPenalty(ev, shuffledBots, g_PBC_ReplyChanceMessage, "message");
    }
}

// ---------------------------------------------------------------------------
// PBC_DispatchWhisperEvent
// ---------------------------------------------------------------------------
void PBC_DispatchWhisperEvent(Player* sender, Player* target, const std::string& msg)
{
    if (!PBC_PTR_VALID(sender) || !PBC_PTR_VALID(target)) return;

    std::string senderName = sender->GetName();
    std::string targetName = target->GetName();
    std::string eventLine   = PBC_Localize("{0} tells you privately: {1}", senderName, msg);
    std::string historyLine = PBC_Localize("{0} (privately to you): {1}", senderName, msg);

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "Whisper event: {} -> {}: \"{}\" (chance={}%)",
             senderName, targetName, msg, g_PBC_ReplyChanceWhisper);

    PBC_EventItem ev;
    ev.type               = PBC_EventType::Normal;
    ev.eventLine          = eventLine;
    ev.source.senderGuid  = sender->GetGUID().GetCounter();
    ev.source.senderName  = senderName;
    ev.source.message     = msg;
    ev.chatType           = CHAT_MSG_WHISPER;

    ev.whisperSenderName = senderName;
    ev.whisperTargetName = targetName;

    // Record the real player who triggered this event (for regen logging).
    {
        WorldSession* senderSess = sender->GetSession();
        if (PBC_IsRealPlayerSession(senderSess))
            ev.regenRequesterGuid = sender->GetGUID().GetCounter();
    }

    if (PBC_RollChance(g_PBC_ReplyChanceWhisper))
    {
        PBC_CharacterSnapshot snap = PBC_SnapshotCharacter(target);
        snap.whisperTargetGuid = sender->GetGUID();
        snap.whisperTargetName = senderName;
        ev.respondingChars.push_back(std::move(snap));
    }
    else
    {
        ev.silentCharGuids.push_back(target->GetGUID().GetCounter());
    }

    {
        WorldSession* senderSess = sender->GetSession();
        if (PBC_IsRealPlayerSession(senderSess))
        {
            uint64_t senderGuid = sender->GetGUID().GetCounter();
            ev.playerCharGuids.push_back(senderGuid);
        }
    }

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_PickTriggerEventLine
// ---------------------------------------------------------------------------
std::string PBC_PickTriggerEventLine(uint64_t botGuid, const std::string& charName)
{
    bool hasLastEntry     = false;
    bool lastIsNarrator   = false;
    bool lastIsWhisper    = false;
    bool lastIsOwn        = false;
    bool lastIsTimePasses = false;

    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto ownersIt = g_PBC_HistoryOwners.find(botGuid);
        if (ownersIt != g_PBC_HistoryOwners.end() && !ownersIt->second.empty())
        {
            uint64_t lastId = ownersIt->second.back();
            auto entryIt = g_PBC_History.find(lastId);
            if (entryIt != g_PBC_History.end())
            {
                hasLastEntry     = true;
                const auto& entry = entryIt->second;
                lastIsNarrator   = (entry.type == 0);
                lastIsWhisper    = (entry.type == CHAT_MSG_WHISPER);
                lastIsOwn        = (entry.authorGuid == botGuid);
                lastIsTimePasses = (entry.type == 0 && entry.message == PBC_Localize("some time passes"));
            }
        }
    }

    // -------------------------------------------------------------------
    // No history → default
    // -------------------------------------------------------------------
    if (!hasLastEntry)
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "PickTriggerEventLine: guid={} char='{}' — no history, using default",
                botGuid, charName);
        return PBC_Localize("you feel the urge to say something");
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG,
            "PickTriggerEventLine: guid={} char='{}' "
            "isNarrator={} isWhisper={} isOwn={} isTimePasses={}",
            botGuid, charName,
            lastIsNarrator, lastIsWhisper, lastIsOwn, lastIsTimePasses);

    // -------------------------------------------------------------------
    // Case 1: "Narrator: *some time passes*"
    // -------------------------------------------------------------------
    if (lastIsTimePasses)
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG,
                "PickTriggerEventLine: guid={} char='{}' → case 1 (time passes) — random you_want_to variant",
                botGuid, charName);

        static const std::vector<std::string> timePassesTriggers = {
            PBC_Localize("you want to comment on your surroundings"),
            PBC_Localize("you want to ask a question"),
            PBC_Localize("you want to share something"),
            PBC_Localize("you want to comment on how you feel")
        };
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, timePassesTriggers.size() - 1);
        return timePassesTriggers[dist(rng)];
    }

    // -------------------------------------------------------------------
    // Case 2: Narrator, but not "some time passes"
    // -------------------------------------------------------------------
    if (lastIsNarrator)
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG,
                "PickTriggerEventLine: guid={} char='{}' → case 2 (narrator, not time passes) — urge_to_comment",
                botGuid, charName);
        return PBC_Localize("you feel the urge to comment on the last thing that happened");
    }

    // -------------------------------------------------------------------
    // Case 3: Not from Narrator and not a whisper
    // -------------------------------------------------------------------
    if (!lastIsWhisper)
    {
        if (lastIsOwn)
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG,
                    "PickTriggerEventLine: guid={} char='{}' → case 3a (own reply) — saying_more",
                    botGuid, charName);
            return PBC_Localize("you feel like saying more");
        }
        else
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG,
                    "PickTriggerEventLine: guid={} char='{}' → case 3b (other reply) — answering_that",
                    botGuid, charName);
            return PBC_Localize("you feel like answering that");
        }
    }

    // -------------------------------------------------------------------
    // Case 4: Whisper or other → default
    // -------------------------------------------------------------------
    PBC_Log(PBC_LogLevel::PBC_DEBUG,
            "PickTriggerEventLine: guid={} char='{}' → case 4 (whisper/fallthrough) — default",
            botGuid, charName);
    return PBC_Localize("you feel the urge to say something");
}

// ---------------------------------------------------------------------------
// PBC_DispatchTriggerEvent
// ---------------------------------------------------------------------------
void PBC_DispatchTriggerEvent(Player* bot)
{
    if (!PBC_PTR_VALID(bot)) return;

    // Raid chat when in a raid group, party chat for a regular party, say
    // otherwise — so triggered characters in different raid sub-groups are
    // still heard by everyone.
    uint32_t chatType = PBC_GetGroupChatType(bot);

    uint64_t botGuid = bot->GetGUID().GetCounter();

    // Insert time-gap narrator line BEFORE picking the trigger event text,
    // so PBC_PickTriggerEventLine sees it and can choose an appropriate
    // trigger variant (e.g. "you want to comment on your surroundings"
    // instead of "you feel the urge to comment on the last thing that
    // happened").
    PBC_MaybeInsertTimeGap(botGuid);

    std::string eventText = PBC_PickTriggerEventLine(botGuid, bot->GetName());

    PBC_EventItem ev;
    ev.type             = PBC_EventType::Normal;
    ev.eventLine        = PBC_MakeEventLine(eventText);
    ev.chatType         = chatType;
    ev.canCreateEvents  = false;

    PBC_CharacterSnapshot snap = PBC_SnapshotCharacter(bot);
    ev.respondingChars.push_back(std::move(snap));

    if (chatType == CHAT_MSG_PARTY || chatType == CHAT_MSG_RAID)
    {
        auto groupBots = PBC_FindGroupBots(bot);
        for (Player* groupBot : groupBots)
            ev.silentCharGuids.push_back(groupBot->GetGUID().GetCounter());
    }

    AddTrackedPlayersToEvent(ev, bot);

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "Trigger event: character={} chatType={} silentChars={} event=\"{}\"",
             bot->GetName(), chatType, ev.silentCharGuids.size(), eventText);

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// PBC_DispatchPartyMessageEvent
// ---------------------------------------------------------------------------
void PBC_DispatchPartyMessageEvent(Player* sender, const std::string& msg,
                                    const std::string& senderNameOverride,
                                    uint32_t chatType,
                                    bool canCreateEvents)
{
    if (!PBC_PTR_VALID(sender)) return;

    std::string senderName = senderNameOverride.empty() ? sender->GetName() : senderNameOverride;
    std::string historyLine = PBC_Localize("{0}: {1}", senderName, msg);
    std::string eventLine   = PBC_Localize("{0} says: {1}", senderName, msg);

    bool isPartyChat = (chatType == CHAT_MSG_PARTY || chatType == CHAT_MSG_PARTY_LEADER);
    bool isRaidChat  = (chatType == CHAT_MSG_RAID  || chatType == CHAT_MSG_RAID_LEADER  ||
                        chatType == CHAT_MSG_RAID_WARNING);
    bool isGroupChat = isPartyChat || isRaidChat;

    // Raid chat reaches the whole raid, so all group bots are eligible.
    // Party chat inside a raid only reaches the sender's own sub-group, so
    // only those bots can hear — and therefore answer — the message.  For a
    // regular (non-raid) party PBC_FindSubGroupBots is equivalent to
    // PBC_FindGroupBots.
    std::vector<Player*> bots;
    if (isRaidChat)
        bots = PBC_FindGroupBots(sender);
    else if (isPartyChat)
        bots = PBC_FindSubGroupBots(sender);
    else
        bots = PBC_FindNearbyBots(sender);

    if (bots.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "Chat message event from {} type={} discarded — no bots found ({})",
                 senderName, chatType, isGroupChat ? "group empty" : "none nearby");
        return;
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "Chat message event from {} type={} ({} bots): \"{}\"",
             senderName, chatType, bots.size(), msg);

    PBC_EventItem ev;
    ev.type               = PBC_EventType::Normal;
    ev.eventLine          = eventLine;
    ev.source.senderGuid  = sender->GetGUID().GetCounter();
    ev.source.senderName  = senderName;
    ev.source.message     = msg;
    ev.chatType           = chatType ? chatType : CHAT_MSG_PARTY;
    ev.canCreateEvents    = canCreateEvents;

    // Record the real player who triggered this event (for regen logging).
    {
        WorldSession* senderSess = sender->GetSession();
        if (PBC_IsRealPlayerSession(senderSess))
            ev.regenRequesterGuid = sender->GetGUID().GetCounter();
    }

    PBC_RollBotsForMessage(ev, bots, msg);

    // Party chat inside a raid only reaches the sender's sub-group, so only
    // the real players in that sub-group should receive the history.  Raid
    // chat and regular-party chat reach the whole group.
    AddTrackedPlayersToEvent(ev, sender, /*subGroupOnly=*/isPartyChat && PBC_IsInRaidGroup(sender));

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "Chat from {} type={} -> {}/{} bots will respond",
             senderName, chatType, ev.respondingChars.size(), bots.size());

    PBC_PushEvent(std::move(ev));
}

// ---------------------------------------------------------------------------
// Regeneration of the last event's responses
// ---------------------------------------------------------------------------

bool PBC_CanRegenLastEvent()
{
    std::lock_guard<std::mutex> lock(g_PBC_LastEventMutex);
    return g_PBC_LastEventRecord != nullptr;
}

bool PBC_IsPlayerInLastEventGroup(Player* player)
{
    if (!PBC_PTR_VALID(player)) return false;

    std::shared_ptr<PBC_LastEventRecord> record;
    {
        std::lock_guard<std::mutex> lock(g_PBC_LastEventMutex);
        record = g_PBC_LastEventRecord;
    }
    if (!record) return false;

    // Collect all character GUIDs that participated in the last event.
    std::unordered_set<uint64_t> eventGuids;
    for (const auto& snap : record->respondingChars)
        eventGuids.insert(snap.charGuidRaw);
    for (uint64_t g : record->silentCharGuids)
        eventGuids.insert(g);

    if (eventGuids.empty()) return false;

    // Check if the player is in the same group as any of the event characters.
    Group* playerGroup = player->GetGroup();
    if (!playerGroup) return false;

    for (GroupReference* ref = playerGroup->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld()) continue;

        if (eventGuids.count(member->GetGUID().GetCounter()))
            return true;
    }

    return false;
}

bool PBC_DispatchRegenEvent(uint64_t requesterGuid)
{
    std::shared_ptr<PBC_LastEventRecord> record;
    {
        std::lock_guard<std::mutex> lock(g_PBC_LastEventMutex);
        record = g_PBC_LastEventRecord;
    }

    if (!record)
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "DispatchRegenEvent: no regen-eligible last event record");
        return false;
    }

    // The event queue must be empty — otherwise pending events would execute
    // before our regen and change what we consider "the last event", making
    // the guardrail checks (no new messages since the event) unreliable.
    {
        std::lock_guard<std::mutex> lock(g_PBC_EventQueueMutex);
        if (!g_PBC_EventQueue.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING, "DispatchRegenEvent: event queue is not empty, aborting regen");
            return false;
        }
    }

    PBC_EventItem ev;
    ev.type              = PBC_EventType::Regen;
    ev.regenRecord       = record;          // shared_ptr copy — keeps the record alive
    ev.regenRequesterGuid = requesterGuid;

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "DispatchRegenEvent: requester={} characters={} savedIds={}",
             requesterGuid, record->respondingChars.size(), record->createdHistoryIds.size());

    PBC_PushEvent(std::move(ev));
    return true;
}
