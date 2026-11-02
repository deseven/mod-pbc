#include "pbc_event_processor.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_locales.h"
#include "pbc_event_dispatch.h"
#include "pbc_condense.h"
#include "pbc_log.h"

#include "Player.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "WorldSession.h"
#include "SharedDefines.h"
#include "GameTime.h"

#include <algorithm>
#include <memory>
#include <regex>
#include <sstream>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <queue>

// ===========================================================================
// Narrator segment parsing helpers
// ===========================================================================

std::vector<NarratorSegment> ParseNarratorSpans(const std::string& text)
{
    std::vector<NarratorSegment> segments;

    // Pre-scan: find all valid *text* narrator spans (opening '*'
    // followed by at least one character and a closing '*').
    std::vector<std::pair<size_t, size_t>> narrSpans; // [start, end] inclusive
    {
        size_t pos = 0;
        while (pos < text.size())
        {
            if (text[pos] == '*')
            {
                size_t closingPos = text.find('*', pos + 1);
                if (closingPos != std::string::npos && closingPos > pos + 1)
                {
                    narrSpans.emplace_back(pos, closingPos);
                    pos = closingPos + 1;
                    continue;
                }
            }
            pos++;
        }
    }

    if (narrSpans.empty())
    {
        segments.push_back({text, false});
        return segments;
    }

    size_t lastEnd = 0;
    for (const auto& span : narrSpans)
    {
        // Regular text before this narrator block
        if (span.first > lastEnd)
        {
            std::string reg = text.substr(lastEnd, span.first - lastEnd);
            size_t s = reg.find_first_not_of(" \t\n\r");
            size_t e = reg.find_last_not_of(" \t\n\r");
            if (s != std::string::npos && e != std::string::npos && s <= e)
                segments.push_back({reg.substr(s, e - s + 1), false});
        }

        // Narrator block
        segments.push_back({text.substr(span.first, span.second - span.first + 1), true});

        lastEnd = span.second + 1;
    }

    // Remaining regular text after the last narrator block
    if (lastEnd < text.size())
    {
        std::string reg = text.substr(lastEnd);
        size_t s = reg.find_first_not_of(" \t\n\r");
        size_t e = reg.find_last_not_of(" \t\n\r");
        if (s != std::string::npos && e != std::string::npos && s <= e)
            segments.push_back({reg.substr(s, e - s + 1), false});
    }

    return segments;
}

void PushReplySegments(const PBC_CharacterSnapshot& snap,
                       PBC_EventItem& ev,
                       const std::vector<NarratorSegment>& segments)
{
    for (const auto& seg : segments)
    {
        if (seg.isNarrator)
        {
            PBC_PendingAction narrAction;
            narrAction.charGuid          = snap.charObjGuid;
            narrAction.text              = seg.text;
            narrAction.isNarratorMessage = true;
            narrAction.fromGuildChannel  = (ev.chatType == CHAT_MSG_GUILD);

            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            g_PBC_PendingActions.push(std::move(narrAction));
        }
        else if (!seg.text.empty())
        {
            PBC_PendingAction action;
            action.charGuid    = snap.charObjGuid;
            action.targetGuid  = snap.whisperTargetGuid;
            action.chatType    = ev.chatType;
            action.text        = seg.text;

            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            g_PBC_PendingActions.push(std::move(action));
        }
    }
}

// ===========================================================================
// PBC_PushNarratorSummary
// ===========================================================================

void PBC_PushNarratorSummary(const ObjectGuid& anchorObjGuid, const std::string& eventLine,
                             bool fromGuildChannel)
{
    if (g_PBC_DisplayNarratorEvents && !anchorObjGuid.IsEmpty())
    {
        PBC_PendingAction narrAction;
        narrAction.charGuid          = anchorObjGuid;
        narrAction.text              = eventLine;
        narrAction.isNarratorMessage = true;
        narrAction.fromGuildChannel  = fromGuildChannel;

        std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
        g_PBC_PendingActions.push(std::move(narrAction));
    }
}

// ===========================================================================
// Extracted event type handlers
// ===========================================================================

namespace {

void ProcessHistoryReload()
{
    PBC_LoadHistoryFromDB();
    PBC_LoadRelationshipsFromDB();
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "HistoryReload: chat history and relationships reloaded from DB.");
    g_PBC_EventThreadDone.store(true);
}

void ProcessCondensation(PBC_EventItem& ev,
                          const std::string& condenseSysPrompt,
                          const std::string& condenseUsrTmpl)
{
    // Notify real players that a lengthy background condensation is starting
    PBC_PushNarratorSummary(ev.condensationChar.charObjGuid,
        PBC_MakeEventLine(PBC_Localize("Condensing {0}'s history...", ev.condensationChar.charName)),
        ev.chatType == CHAT_MSG_GUILD);

    // Capture the full history snapshot BEFORE condensation truncates it
    std::deque<std::string> preCondensationHistory = ev.condensationChar.history;

    bool condensed = PBC_CondenseInline(ev.condensationChar, condenseSysPrompt, condenseUsrTmpl);

    if (!condensed)
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING,
                 "Condensation event failed for character={} — history left untouched, "
                 "will retry when threshold is reached again",
                 ev.condensationChar.charName);
        g_PBC_EventThreadDone.store(true);
        return;
    }

    // Condensation succeeded — reload memories from DB
    PBC_LoadMemoriesFromDB();

    // Queue relationship updates for all party members
    QueueRelationshipUpdatesAfterCondensation(ev.condensationChar, preCondensationHistory);

    g_PBC_EventThreadDone.store(true);
}

void ProcessRelationshipUpdate(PBC_EventItem& ev)
{
    if (ev.relationshipSystemPrompt.empty() || ev.relationshipUserPromptTmpl.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "RelationshipUpdate: prompts not configured, skipping for character={}",
                 ev.relationshipChar.charName);
        g_PBC_EventThreadDone.store(true);
        return;
    }

    // Notify real players that a lengthy background relationship update is starting
    PBC_PushNarratorSummary(ev.relationshipChar.charObjGuid,
        PBC_MakeEventLine(PBC_Localize("Updating {0}'s relationship with {1}...", ev.relationshipChar.charName, ev.relationshipTargetName)),
        ev.chatType == CHAT_MSG_GUILD);

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "RelationshipUpdate: character={} target={}",
             ev.relationshipChar.charName, ev.relationshipTargetName);

    std::string userPrompt = ev.relationshipUserPromptTmpl;
    PBC_ExpandNewlineEscapes(userPrompt);

    PBC_ReplaceToken(userPrompt, "character_card",           ev.relationshipChar.characterCard);
    { std::ostringstream histOss; for (const auto& line : ev.relationshipChar.history) histOss << line << "\n"; PBC_ReplaceToken(userPrompt, "chat_history", histOss.str()); }
    PBC_ReplaceToken(userPrompt, "relationship_target",      ev.relationshipTargetInfo);
    PBC_ReplaceToken(userPrompt, "target_current_relationship", ev.relationshipCurrentText);

    const PBC_APIConfig* relCfg = PBC_GetConnection("relationship");
    PBC_LLMResult res = PBC_CallLLMWithConfig(*relCfg, ev.relationshipSystemPrompt, userPrompt);

    if (!res.success || res.text.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "RelationshipUpdate: LLM failed for character={} target={}",
                 ev.relationshipChar.charName, ev.relationshipTargetName);
        g_PBC_EventThreadDone.store(true);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
        auto& entry = g_PBC_Relationships[ev.relationshipChar.charGuidRaw][ev.relationshipTargetName];
        entry.text = res.text;
        entry.updatedAt = PBC_FormatDateTime(std::time(nullptr));
    }
    DB_UpsertRelationship(ev.relationshipChar.charGuidRaw, ev.relationshipTargetName, res.text);
    PBC_WsNotify(ev.relationshipChar.charGuidRaw, "relationship");

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "RelationshipUpdate: updated character={} target={} text=\"{}\"",
             ev.relationshipChar.charName, ev.relationshipTargetName,
             PBC_SanitizeForFmt(res.text));

    g_PBC_EventThreadDone.store(true);
}

void ProcessCardAdditionsMigration(PBC_EventItem& ev)
{
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "CardAdditionsMigration: starting...");

    QueryResult addResult = CharacterDatabase.Query(
        "SELECT bot_guid, addition FROM mod_pbc_character_card_additions ORDER BY bot_guid ASC, id ASC"
    );

    if (!addResult)
    {
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "CardAdditionsMigration: no card additions found, nothing to migrate.");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    std::unordered_map<uint64_t, std::vector<std::string>> additionsByBot;
    uint32_t totalAdditions = 0;
    do
    {
        uint64_t    botGuid  = (*addResult)[0].Get<uint64_t>();
        std::string addition = (*addResult)[1].Get<std::string>();
        additionsByBot[botGuid].push_back(std::move(addition));
        ++totalAdditions;
    } while (addResult->NextRow());

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "CardAdditionsMigration: found {} additions across {} characters",
             totalAdditions, additionsByBot.size());

    uint32_t processed = 0;
    uint32_t totalMemories = 0;

    for (auto& [botGuid, additions] : additionsByBot)
    {
        std::string charName;
        QueryResult nameResult = CharacterDatabase.Query(
            "SELECT name FROM characters WHERE guid = {}", botGuid
        );
        if (nameResult)
            charName = (*nameResult)[0].Get<std::string>();

        std::string charCard;
        if (!charName.empty())
        {
            auto cardIt = g_PBC_CharacterCards.find(charName);
            if (cardIt != g_PBC_CharacterCards.end())
                charCard = cardIt->second;
        }
        if (charCard.empty())
            charCard = g_PBC_DefaultCharacterDescription;

        PBC_CharacterSnapshot snap;
        snap.charGuidRaw   = botGuid;
        snap.charName      = charName;
        snap.characterCard = charCard;
        for (const auto& addition : additions)
            snap.history.push_back(addition);

        std::string userPrompt = PBC_BuildCondensationPromptFromSnapshot(
            snap, ev.migrationCondensationUserPromptTmpl);

        const PBC_APIConfig* condCfg = PBC_GetConnection("condensation");
        PBC_LLMResult res = PBC_CallLLMWithConfig(*condCfg, ev.migrationCondensationSystemPrompt, userPrompt, /*preserveNewlines=*/true);

        if (!res.success || res.text.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING,
                     "CardAdditionsMigration: LLM failed for character={}, skipping",
                     charName.empty() ? fmt::format("guid:{}", botGuid) : charName);
            processed += additions.size();
            continue;
        }

        int memCount = PBC_ParseMemoryLines(res.text, botGuid);
        totalMemories += memCount;
        processed += additions.size();

        PBC_Log(PBC_LogLevel::PBC_DEFAULT,
                 "CardAdditionsMigration: character={} additions={} memories_extracted={} progress={}/{}",
                 charName.empty() ? fmt::format("guid:{}", botGuid) : charName,
                 additions.size(), memCount, processed, totalAdditions);
    }

    PBC_LoadMemoriesFromDB();

    PBC_Log(PBC_LogLevel::PBC_DEFAULT,
             "CardAdditionsMigration: complete. {} additions processed, {} memories created.",
             totalAdditions, totalMemories);

    g_PBC_EventThreadDone.store(true);
}

void ProcessCombatSummarization(PBC_EventItem& ev)
{
    if (ev.combatSystemPrompt.empty() || ev.combatUserPrompt.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: CombatSummarization prompts empty, skipping");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    const PBC_APIConfig* utilCfg = PBC_GetConnection("utility");
    PBC_LLMResult summary = PBC_CallLLMWithConfig(*utilCfg, ev.combatSystemPrompt, ev.combatUserPrompt);
    if (!summary.success || summary.text.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: CombatSummarization LLM failed");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    ev.eventLine = PBC_MakeEventLine(summary.text);
    ev.source.narratorText = summary.text;

    PBC_PushNarratorSummary(ev.anchorObjGuid, ev.eventLine, ev.chatType == CHAT_MSG_GUILD);
}

void ProcessQuestSummarization(PBC_EventItem& ev)
{
    if (ev.questSystemPrompt.empty() || ev.questUserPrompt.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: QuestSummarization prompts empty, skipping");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    const PBC_APIConfig* utilCfg = PBC_GetConnection("utility");
    PBC_LLMResult summary = PBC_CallLLMWithConfig(*utilCfg, ev.questSystemPrompt, ev.questUserPrompt);
    if (!summary.success || summary.text.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: QuestSummarization LLM failed");
        g_PBC_EventThreadDone.store(true);
        return;
    }

    ev.eventLine = PBC_MakeEventLine(summary.text);
    ev.source.narratorText = summary.text;

    PBC_PushNarratorSummary(ev.anchorObjGuid, ev.eventLine, ev.chatType == CHAT_MSG_GUILD);
}

struct PendingReply {
    uint64_t    authorGuid;
    uint64_t    targetGuid;   // For whispers: the recipient. 0 otherwise.
    uint8_t     chatType;
    std::string messageText;  // Raw text (no speaker prefix)
};

// ---------------------------------------------------------------------------
// ProcessNormal
//
// Processes a Normal / QuestSummarization / CombatSummarization event.
//
// regenRecord: when non-null, the event is a regeneration of a previous
//   event.  In that mode the responder loop runs identically, but instead
//   of appending new history messages it edits the existing messages
//   (identified by regenRecord->createdHistoryIds) in place.  The
//   snapshots in ev.respondingChars must already be the pre-mutation
//   copies captured during the original event.  No new
//   PBC_LastEventRecord is saved in regen mode (the existing record is
//   reused so regen can be triggered repeatedly).
//
// outCreatedIds: when non-null, populated with the DB history IDs of
//   every message created by this event (source line + each reply), in
//   chronological order.  Used by the caller to build the
//   PBC_LastEventRecord for normal (non-regen) events.
// ---------------------------------------------------------------------------
void ProcessNormal(PBC_EventItem& ev,
                   const std::string& sysPrompt,
                   const std::string& condenseSysPrompt,
                   const std::string& condenseUsrTmpl,
                   PBC_LastEventRecord* regenRecord = nullptr,
                   std::vector<uint64_t>* outCreatedIds = nullptr)
{
    bool isRegen = (regenRecord != nullptr);

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "ProcessEvent: type={} isRegen={} respondingChars={} silentChars={} event=\"{}\"",
             static_cast<int>(ev.type), isRegen, ev.respondingChars.size(), ev.silentCharGuids.size(), ev.eventLine);

    // -------------------------------------------------------------------
    // Seed the event-local history buffer with the source event (if any).
    // This buffer accumulates every message in chronological order and is
    // rendered into each responder's snapshot before their LLM call so
    // they see the full chain of what happened before their turn.
    //
    // In regen mode the seed is already in ev.eventHistory (restored from
    // the saved record), so we only seed in normal mode.
    // -------------------------------------------------------------------
    if (!isRegen)
    {
        if (ev.source.IsChat())
        {
            PBC_HistoryEntry srcEntry;
            srcEntry.authorGuid = ev.source.senderGuid;
            srcEntry.type       = static_cast<uint8_t>(ev.chatType);
            srcEntry.message    = ev.source.message;
            ev.eventHistory.push_back(std::move(srcEntry));
        }
        else if (ev.source.IsNarrator())
        {
            PBC_HistoryEntry srcEntry;
            srcEntry.authorGuid = 0;
            srcEntry.type       = 0;
            srcEntry.message    = ev.source.narratorText;
            ev.eventHistory.push_back(std::move(srcEntry));
        }
    }

    // -------------------------------------------------------------------
    // Capture pre-mutation copies of the responding snapshots and the
    // seed eventHistory for the PBC_LastEventRecord (normal mode only).
    // The snapshots' history must be the state BEFORE any event replies
    // are appended so a future regen reproduces the same prompt context.
    // -------------------------------------------------------------------
    std::vector<PBC_CharacterSnapshot> preMutationSnapshots;
    std::vector<PBC_HistoryEntry>      seedEventHistory;
    if (!isRegen && outCreatedIds)
    {
        preMutationSnapshots = ev.respondingChars;   // deep copy
        seedEventHistory     = ev.eventHistory;      // deep copy (source only)
    }

    std::string currentEvent = ev.eventLine;

    uint64_t    lastResponderGuid = 0;
    std::string lastEventLine;

    std::vector<PendingReply> replies;

    for (PBC_CharacterSnapshot& snap : ev.respondingChars)
    {
        // Post deferred "thinks..." notification
        if (g_PBC_DisplayNarratorEvents)
        {
            PBC_PendingAction action;
            action.charGuid          = snap.charObjGuid;
            action.text              = PBC_MakeEventLine(PBC_Localize("{0} thinks...", snap.charName));
            action.isNarratorMessage = true;
            action.fromGuildChannel  = (ev.chatType == CHAT_MSG_GUILD);

            std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
            g_PBC_PendingActions.push(std::move(action));
        }
        PBC_WsNotify(snap.charGuidRaw, "thinks");

        // Condense inline if over token budget
        int histTokens = PBC_EstimateHistoryTokens(snap.charGuidRaw);
        if (histTokens > static_cast<int>(g_PBC_MaxHistoryCtx))
        {
            PBC_PushNarratorSummary(snap.charObjGuid,
                PBC_MakeEventLine(PBC_Localize("Condensing {0}'s history...", snap.charName)),
                ev.chatType == CHAT_MSG_GUILD);

            std::deque<std::string> preCondensationHistory = snap.history;

            bool condensed = PBC_CondenseInline(snap, condenseSysPrompt, condenseUsrTmpl);
            if (condensed)
            {
                PBC_LoadMemoriesFromDB();
                QueueRelationshipUpdatesAfterCondensation(snap, preCondensationHistory);
            }
        }

        // Render all eventHistory entries EXCEPT the last one into this
        // character's snapshot.  The last entry IS the currentEvent —
        // it belongs in [CURRENT EVENT], not [HISTORY].  Previous entries
        // are "the past" that this responder needs as context.
        for (size_t i = 0; i + 1 < ev.eventHistory.size(); ++i)
        {
            std::string rendered = PBC_RenderHistoryLine(ev.eventHistory[i], snap.charGuidRaw);
            snap.history.push_back(rendered);
        }

        // Build user prompt from snapshot
        std::string userPrompt = PBC_BuildUserPromptFromSnapshot(snap, currentEvent);

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "ProcessEvent: calling LLM for character={} event=\"{}\"",
                 snap.charName, currentEvent);

        PBC_LLMResult res = PBC_CallLLM(sysPrompt, userPrompt);

        if (!res.success || res.text.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessEvent: LLM failed/empty for character={}", snap.charName);
            continue;
        }

        // Collect structured reply data (no pre-rendering)
        PendingReply reply;
        reply.authorGuid  = snap.charGuidRaw;
        reply.chatType    = static_cast<uint8_t>(ev.chatType);
        reply.messageText = res.text;
        if (ev.chatType == CHAT_MSG_WHISPER && !snap.whisperTargetGuid.IsEmpty())
            reply.targetGuid = snap.whisperTargetGuid.GetCounter();
        else
            reply.targetGuid = 0;

        // Add this reply to the event-local history buffer so subsequent
        // responders see it in their chain context.
        {
            PBC_HistoryEntry replyEntry;
            replyEntry.authorGuid = snap.charGuidRaw;
            replyEntry.type       = static_cast<uint8_t>(ev.chatType);
            replyEntry.message    = res.text;
            ev.eventHistory.push_back(std::move(replyEntry));
        }

        replies.push_back(std::move(reply));

        // -----------------------------------------------------------------
        // Send immediate WS preview (id=0) — pre-render for each recipient
        // -----------------------------------------------------------------
        {
            PBC_HistoryEntry previewEntry;
            previewEntry.id         = 0;
            previewEntry.authorGuid = snap.charGuidRaw;
            previewEntry.type       = static_cast<uint8_t>(ev.chatType);
            previewEntry.message    = res.text;

            // Preview for all recipients (responding chars, silent, replyOnly, players)
            std::unordered_set<uint64_t> previewTargets;
            for (const PBC_CharacterSnapshot& rs : ev.respondingChars)
                previewTargets.insert(rs.charGuidRaw);
            if (ev.chatType != CHAT_MSG_WHISPER)
            {
                for (uint64_t g : ev.silentCharGuids)   previewTargets.insert(g);
                for (uint64_t g : ev.replyOnlyCharGuids) previewTargets.insert(g);
                for (uint64_t g : ev.playerCharGuids)    previewTargets.insert(g);
            }
            else
            {
                // Whispers: only sender + target see them
                previewTargets.clear();
                previewTargets.insert(snap.charGuidRaw);
                if (!snap.whisperTargetGuid.IsEmpty())
                    previewTargets.insert(snap.whisperTargetGuid.GetCounter());
                for (uint64_t g : ev.playerCharGuids)
                    previewTargets.insert(g);
            }

            for (uint64_t targetGuid : previewTargets)
            {
                std::string rendered = PBC_RenderHistoryLine(previewEntry, targetGuid);
                PBC_WsNotifyHistoryPreview(targetGuid, rendered);
            }
        }

        // Split reply into narrator/regular segments if narrator events enabled
        if (g_PBC_DisplayNarratorEvents)
        {
            auto segments = ParseNarratorSpans(res.text);
            PushReplySegments(snap, ev, segments);
        }
        else
        {
            if (!res.text.empty())
            {
                PBC_PendingAction action;
                action.charGuid    = snap.charObjGuid;
                action.targetGuid  = snap.whisperTargetGuid;
                action.chatType    = ev.chatType;
                action.text        = res.text;

                std::lock_guard<std::mutex> lock(g_PBC_PendingActionsMutex);
                g_PBC_PendingActions.push(std::move(action));
            }
        }

        // Advance the chain
        currentEvent = PBC_Localize("{0} says: {1}", snap.charName, res.text);

        lastResponderGuid = snap.charGuidRaw;
        lastEventLine     = currentEvent;

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "ProcessEvent: character={} replied", snap.charName);
    }

    // -----------------------------------------------------------------------
    // Flush event-local history to DB and global memory
    //
    // Normal mode: append every entry from the event-local buffer in order.
    // Regen mode: edit the existing messages in place using the saved
    //   history IDs (regenRecord->createdHistoryIds).  The message IDs —
    //   and therefore every character's ownership of them — stay stable,
    //   so we don't need to touch ownership rows at all.
    // -----------------------------------------------------------------------

    if (!isRegen)
    {
        // Collect all unique participant GUIDs
        std::vector<uint64_t> allOwners;
        for (const PBC_CharacterSnapshot& snap : ev.respondingChars)
            allOwners.push_back(snap.charGuidRaw);
        for (uint64_t g : ev.silentCharGuids)
            allOwners.push_back(g);
        for (uint64_t g : ev.replyOnlyCharGuids)
            allOwners.push_back(g);
        for (uint64_t g : ev.playerCharGuids)
            allOwners.push_back(g);
        std::sort(allOwners.begin(), allOwners.end());
        allOwners.erase(std::unique(allOwners.begin(), allOwners.end()), allOwners.end());

        // Write every entry from the event-local buffer in order.
        // For public chat all participants see everything; for whispers
        // allOwners already correctly contains only {bot, sender}.
        for (const auto& entry : ev.eventHistory)
        {
            uint64_t newId = PBC_AppendHistoryMessage(entry.authorGuid, entry.type,
                                                      entry.message, allOwners);
            if (outCreatedIds)
                outCreatedIds->push_back(newId);
        }
    }
    else
    {
        // Regen mode: edit the existing messages in place.  The number of
        // entries in ev.eventHistory must match the number of saved IDs
        // (seed + replies).  If the counts don't line up — which should
        // never happen under normal operation — we abort the regen
        // entirely and leave the original messages untouched, rather
        // than risk corrupting the history.
        const auto& savedIds = regenRecord->createdHistoryIds;
        if (ev.eventHistory.size() != savedIds.size())
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING,
                     "ProcessEvent: regen aborted — eventHistory size {} != savedIds size {} "
                     "(original messages left untouched)",
                     ev.eventHistory.size(), savedIds.size());
        }
        else
        {
            for (size_t i = 0; i < ev.eventHistory.size(); ++i)
            {
                const auto& entry = ev.eventHistory[i];
                if (savedIds[i] != 0)
                    PBC_UpdateHistoryMessage(savedIds[i], entry.message);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Save the PBC_LastEventRecord (normal mode only, when there were
    // replies and the caller requested capture via outCreatedIds).
    // The record is reused across regens — a regen does not replace it,
    // so regeneration can be triggered repeatedly.
    // -----------------------------------------------------------------------
    if (!isRegen && outCreatedIds && !replies.empty())
    {
        auto record = std::make_shared<PBC_LastEventRecord>();
        record->eventLine          = ev.eventLine;
        record->source              = ev.source;
        record->chatType            = ev.chatType;
        record->canCreateEvents     = ev.canCreateEvents;
        record->whisperSenderName   = ev.whisperSenderName;
        record->whisperTargetName   = ev.whisperTargetName;
        record->respondingChars      = std::move(preMutationSnapshots);
        record->silentCharGuids     = ev.silentCharGuids;
        record->playerCharGuids      = ev.playerCharGuids;
        record->replyOnlyCharGuids  = ev.replyOnlyCharGuids;
        record->seedEventHistory     = std::move(seedEventHistory);
        record->createdHistoryIds    = *outCreatedIds;
        record->requesterGuid        = ev.regenRequesterGuid;

        {
            std::lock_guard<std::mutex> lk(g_PBC_LastEventMutex);
            g_PBC_LastEventRecord = std::move(record);
        }
    }

    // -----------------------------------------------------------------------
    // Secondary event (normal mode only — regen never spawns secondaries)
    // -----------------------------------------------------------------------
    if (!isRegen && ev.canCreateEvents && lastResponderGuid != 0 && !replies.empty())
    {
        std::unordered_set<uint64_t> excluded;
        for (const auto& rs : ev.respondingChars)
            excluded.insert(rs.charGuidRaw);
        for (uint64_t g : ev.silentCharGuids)
            excluded.insert(g);

        PBC_PendingEventRequest req;
        req.eventLine           = lastEventLine;
        req.source              = ev.source;
        req.chatType            = ev.chatType;
        req.anchorCharGuid     = lastResponderGuid;
        req.eventHistory        = ev.eventHistory;
        for (const auto& rs : ev.respondingChars)
            req.originCharGuids.push_back(rs.charGuidRaw);
        req.playerCharGuids   = ev.playerCharGuids;
        req.excludedCharGuids = std::move(excluded);

        {
            std::lock_guard<std::mutex> lock(g_PBC_PendingEventRequestsMutex);
            g_PBC_PendingEventRequests.push(std::move(req));
        }

        PBC_Log(PBC_LogLevel::PBC_DEBUG,
                 "ProcessEvent: queued secondary event from last responder guid={} "
                 "excluded={} silent={} event=\"{}\"",
                 lastResponderGuid, req.excludedCharGuids.size(), ev.silentCharGuids.size(), lastEventLine);
    }
}

// ===========================================================================
// ProcessRegen
//
// Regenerates the responses of the last Normal event.  The saved
// PBC_LastEventRecord provides the pre-mutation snapshots and the DB
// history IDs of the original messages.  We re-run ProcessNormal in
// regen mode, which edits the existing messages in place (keeping their
// IDs and ownership stable).
//
// Before re-running, we verify that no new messages were appended to any
// affected character's history since the original event — i.e. the
// trailing history IDs of every participant still match the ones created
// by the original event.  If anything was added (e.g. narration), the
// regen is aborted.
// ===========================================================================
void ProcessRegen(PBC_EventItem& ev,
                  const std::string& sysPrompt,
                  const std::string& condenseSysPrompt,
                  const std::string& condenseUsrTmpl)
{
    auto& record = ev.regenRecord;
    if (!record)
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "ProcessRegen: no regen record attached, aborting");
        return;
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "ProcessRegen: requester={} characters={} savedIds={}",
             ev.regenRequesterGuid, record->respondingChars.size(), record->createdHistoryIds.size());

    // -------------------------------------------------------------------
    // Guardrail: verify that no new messages were appended to any
    // affected character's history since the original event.
    //
    // For each participant, the trailing history IDs must end with the
    // exact sequence of IDs created by the original event (the event's
    // createdHistoryIds, filtered to those owned by that participant).
    // If any extra messages were appended after them, the regen is
    // aborted and the record is invalidated.
    // -------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);

        // Build a per-owner list of the history IDs created by the original
        // event.  A message is "owned" by a participant if it appears in
        // their g_PBC_HistoryOwners deque.  We check ownership for every
        // participant GUID.
        std::vector<uint64_t> participants;
        for (const auto& snap : record->respondingChars)
            participants.push_back(snap.charGuidRaw);
        for (uint64_t g : record->silentCharGuids)
            participants.push_back(g);
        for (uint64_t g : record->replyOnlyCharGuids)
            participants.push_back(g);
        for (uint64_t g : record->playerCharGuids)
            participants.push_back(g);
        std::sort(participants.begin(), participants.end());
        participants.erase(std::unique(participants.begin(), participants.end()), participants.end());

        for (uint64_t guid : participants)
        {
            auto ownersIt = g_PBC_HistoryOwners.find(guid);
            if (ownersIt == g_PBC_HistoryOwners.end())
            {
                // History was cleared (e.g. condensation) — can't regen.
                PBC_Log(PBC_LogLevel::PBC_WARNING,
                         "ProcessRegen: aborted — history for guid={} was cleared since the original event",
                         guid);
                return;
            }

            const auto& ownerIds = ownersIt->second;

            // Collect the subset of createdHistoryIds owned by this guid,
            // preserving chronological order.
            std::vector<uint64_t> ownedCreated;
            for (uint64_t hid : record->createdHistoryIds)
            {
                if (std::find(ownerIds.begin(), ownerIds.end(), hid) != ownerIds.end())
                    ownedCreated.push_back(hid);
            }

            if (ownedCreated.empty())
                continue;   // this participant owns none of the event messages

            // The trailing entries of ownerIds must exactly match
            // ownedCreated.  If ownerIds is shorter, or the trailing
            // sequence differs, new messages were appended (or the
            // history was mutated) — abort.
            if (ownerIds.size() < ownedCreated.size())
            {
                PBC_Log(PBC_LogLevel::PBC_WARNING,
                         "ProcessRegen: aborted — history for guid={} has fewer entries than the original event produced",
                         guid);
                return;
            }

            size_t offset = ownerIds.size() - ownedCreated.size();
            for (size_t i = 0; i < ownedCreated.size(); ++i)
            {
                if (ownerIds[offset + i] != ownedCreated[i])
                {
                    PBC_Log(PBC_LogLevel::PBC_WARNING,
                             "ProcessRegen: aborted — new messages were appended to guid={} history since the original event",
                             guid);
                    return;
                }
            }
        }
    }

    // -------------------------------------------------------------------
    // All checks passed — rebuild the event item from the saved record
    // and re-run ProcessNormal in regen mode.
    // -------------------------------------------------------------------
    ev.eventLine         = record->eventLine;
    ev.source            = record->source;
    ev.chatType          = record->chatType;
    ev.canCreateEvents   = false;   // regen never spawns secondary events
    ev.whisperSenderName = record->whisperSenderName;
    ev.whisperTargetName = record->whisperTargetName;
    ev.respondingChars   = record->respondingChars;   // pre-mutation snapshots
    ev.silentCharGuids   = record->silentCharGuids;
    ev.playerCharGuids   = record->playerCharGuids;
    ev.replyOnlyCharGuids = record->replyOnlyCharGuids;
    ev.eventHistory      = record->seedEventHistory;  // source-only seed

    ProcessNormal(ev, sysPrompt, condenseSysPrompt, condenseUsrTmpl,
                  /*regenRecord=*/record.get(), /*outCreatedIds=*/nullptr);

    // Notify WS clients of the regen so the frontend can replace the
    // affected messages in place.  Every participant (responding chars,
    // silent chars, players) owns the regenerated messages, so we notify
    // each one with the full list of affected IDs rendered from their
    // perspective.
    {
        std::vector<uint64_t> participants;
        for (const auto& snap : record->respondingChars)
            participants.push_back(snap.charGuidRaw);
        for (uint64_t g : record->silentCharGuids)
            participants.push_back(g);
        for (uint64_t g : record->replyOnlyCharGuids)
            participants.push_back(g);
        for (uint64_t g : record->playerCharGuids)
            participants.push_back(g);
        std::sort(participants.begin(), participants.end());
        participants.erase(std::unique(participants.begin(), participants.end()), participants.end());

        for (uint64_t guid : participants)
            PBC_WsNotifyRegen(guid, record->createdHistoryIds);
    }
}

} // anonymous namespace

// ===========================================================================
// PBC_ProcessEventItem
// ===========================================================================

void PBC_ProcessEventItem(PBC_EventItem ev)
{
    // -------------------------------------------------------------------
    // Regen events skip the time-gap insertion entirely.  The saved
    // snapshots already contain whatever time-gap lines were present at
    // the time of the original event, and we don't want to insert a new
    // "some time passes" line just because the regen was triggered later.
    // -------------------------------------------------------------------
    bool isRegen = (ev.type == PBC_EventType::Regen);

    if (!isRegen)
    {
        // -------------------------------------------------------------------
        // Insert time-gap narrator lines BEFORE any event-type-specific
        // processing, so every character (responding, silent, or undergoing
        // condensation/relationship-update) knows about the time gap before
        // the LLM prompt is built.
        //
        // We collect all relevant character GUIDs first, then create ONE
        // shared "some time passes" entry owned by all of them, instead of
        // inserting one duplicate DB row per character.
        // -------------------------------------------------------------------
        bool incomingIsWhisper = (ev.chatType == CHAT_MSG_WHISPER);

        // Collect all character GUIDs that participate in this event
        std::vector<uint64_t> allGuids;
        allGuids.reserve(ev.respondingChars.size() + ev.silentCharGuids.size() + 2);

        for (const PBC_CharacterSnapshot& snap : ev.respondingChars)
            allGuids.push_back(snap.charGuidRaw);
        for (uint64_t guid : ev.silentCharGuids)
            allGuids.push_back(guid);
        if (ev.type == PBC_EventType::Condensation)
            allGuids.push_back(ev.condensationChar.charGuidRaw);
        if (ev.type == PBC_EventType::RelationshipUpdate)
            allGuids.push_back(ev.relationshipChar.charGuidRaw);

        // One shared batch call — creates a single DB row for all that need it
        std::unordered_set<uint64_t> gapInserted =
            PBC_MaybeInsertSharedTimeGap(allGuids, incomingIsWhisper);

        if (!gapInserted.empty())
        {
            std::string renderedGap = PBC_Localize("Narrator: *{0}*", PBC_Localize("some time passes"));

            // Push the rendered line into snapshots that need it for prompt building
            for (PBC_CharacterSnapshot& snap : ev.respondingChars)
            {
                if (gapInserted.count(snap.charGuidRaw))
                    snap.history.push_back(renderedGap);
            }
            if (ev.type == PBC_EventType::Condensation &&
                gapInserted.count(ev.condensationChar.charGuidRaw))
            {
                ev.condensationChar.history.push_back(renderedGap);
            }
            if (ev.type == PBC_EventType::RelationshipUpdate &&
                gapInserted.count(ev.relationshipChar.charGuidRaw))
            {
                ev.relationshipChar.history.push_back(renderedGap);
            }
        }
    }

    // Capture config strings (read-only, safe without lock)
    std::string sysPrompt         = g_PBC_SystemPrompt;
    std::string condenseSysPrompt = g_PBC_CondensationSystemPrompt;
    std::string condenseUsrTmpl   = g_PBC_CondensationUserPrompt;

    // -----------------------------------------------------------------------
    // HistoryReload
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::HistoryReload)
    {
        ProcessHistoryReload();
        return;
    }

    // -----------------------------------------------------------------------
    // Condensation event
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::Condensation)
    {
        ProcessCondensation(ev, condenseSysPrompt, condenseUsrTmpl);
        return;
    }

    // -----------------------------------------------------------------------
    // RelationshipUpdate event
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::RelationshipUpdate)
    {
        ProcessRelationshipUpdate(ev);
        return;
    }

    // -----------------------------------------------------------------------
    // CardAdditionsMigration
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::CardAdditionsMigration)
    {
        ProcessCardAdditionsMigration(ev);
        return;
    }

    // -----------------------------------------------------------------------
    // Regen: re-run the last event's responses from the saved record.
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::Regen)
    {
        ProcessRegen(ev, sysPrompt, condenseSysPrompt, condenseUsrTmpl);
        g_PBC_EventThreadDone.store(true);
        return;
    }

    // -----------------------------------------------------------------------
    // CombatSummarization: generate summary, then fall through to Normal
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::CombatSummarization)
    {
        ProcessCombatSummarization(ev);
        // Fall through to Normal processing
    }

    // -----------------------------------------------------------------------
    // QuestSummarization: generate summary, then fall through to Normal
    // -----------------------------------------------------------------------
    if (ev.type == PBC_EventType::QuestSummarization)
    {
        ProcessQuestSummarization(ev);
        // Fall through to Normal processing
    }

    // -----------------------------------------------------------------------
    // Normal event processing
    //
    // Pass outCreatedIds so ProcessNormal can capture the PBC_LastEventRecord
    // (the pre-mutation snapshots + created message IDs) for regen support.
    // -----------------------------------------------------------------------
    std::vector<uint64_t> createdIds;
    ProcessNormal(ev, sysPrompt, condenseSysPrompt, condenseUsrTmpl,
                  /*regenRecord=*/nullptr, /*outCreatedIds=*/&createdIds);

    g_PBC_EventThreadDone.store(true);
}
