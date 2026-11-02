#ifndef MOD_PBC_CONFIG_H
#define MOD_PBC_CONFIG_H

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>
#include <memory>
#include "ObjectGuid.h"
#include "ScriptMgr.h"
#include "pbc_llm.h"

// Module enable / debug
extern bool     g_PBC_Enable;
extern bool     g_PBC_DebugEnabled;
extern bool     g_PBC_DebugShowFullRequest;
extern bool     g_PBC_DisplayNarratorEvents;
extern bool     g_PBC_CardAdditionsMigrationNeeded;

// ---------------------------------------------------------------------------
// LLM API connection registry
//
// Connections are loaded from JSONC files at config time and stored in a map
// keyed by task name: "default", "utility", "condensation", "relationship".
// Task-specific slots fall back to "default" when empty.
// ---------------------------------------------------------------------------
extern std::unordered_map<std::string, PBC_APIConfig> g_PBC_Connections;
extern std::mutex g_PBC_ConnectionsMutex;

// Returns a pointer to the connection for the given task name, or nullptr if
// the task has no connection and no "default" fallback exists. Thread-safe.
// The returned pointer is valid until the next config reload.
const PBC_APIConfig* PBC_GetConnection(const std::string& name);

// Context / condensation
extern uint32_t    g_PBC_MaxHistoryCtx;
extern uint32_t    g_PBC_MaxMemoriesCtx;

// Prompt templates
extern std::string g_PBC_SystemPrompt;
extern std::string g_PBC_UserPrompt;
extern std::string g_PBC_CondensationSystemPrompt;
extern std::string g_PBC_CondensationUserPrompt;
extern std::string g_PBC_DefaultCharacterDescription;
extern std::string g_PBC_CharacterContext;

// Relationship update prompts
extern std::string g_PBC_RelationshipUpdateSystemPrompt;
extern std::string g_PBC_RelationshipUpdateUserPrompt;

// Paths
extern std::string g_PBC_PromptsPath;
extern std::string g_PBC_CharacterCardsPath;

// Reply chances (0-100)
extern uint32_t g_PBC_ReplyChanceWhisper;
extern uint32_t g_PBC_ReplyChanceMention;
extern uint32_t g_PBC_ReplyChanceMessage;
extern uint32_t g_PBC_RollPenaltyOnAnswer;
extern uint32_t g_PBC_ReplyChanceItem;
extern uint32_t g_PBC_ReplyChanceDuel;
extern uint32_t g_PBC_ReplyChanceLevelUp;
extern uint32_t g_PBC_ReplyChanceHardCombat;
extern uint32_t g_PBC_ReplyChanceQuestCompleted;
extern uint32_t g_PBC_ReplyChanceQuestTaken;
extern uint32_t g_PBC_ReplyChanceLocationChanged;

extern uint32_t g_PBC_LocationChangeDebounceCycles;
extern uint32_t g_PBC_CombatEndDebounceCycles;

// Quest LLM prompts
extern std::string g_PBC_QuestCompletedSystemPrompt;
extern std::string g_PBC_QuestCompletedUserPrompt;
extern std::string g_PBC_QuestTakenSystemPrompt;
extern std::string g_PBC_QuestTakenUserPrompt;

// Combat LLM prompts
extern std::string g_PBC_CombatEndedSystemPrompt;
extern std::string g_PBC_CombatEndedUserPrompt;

// HTTP server
extern int         g_PBC_HttpServerPort;
extern std::string g_PBC_HttpServerBind;
extern int         g_PBC_HttpServerTimeout;
extern std::string g_PBC_HttpServerBaseUrl;
extern std::string g_PBC_HttpServerPrivateKey;
extern std::string g_PBC_HttpServerFrontendPath;

// Blacklist prefixes
extern std::vector<std::string> g_PBC_Blacklist;

// When enabled (1), all addon messages (LANG_ADDON) are silently ignored.
// The PBC.Blacklist is bypassed entirely in this mode.
// When disabled (0), the blacklist logic applies as normal.
extern bool g_PBC_IgnoreAllAddonMessages;

// Per-character roll chance modifiers (bot_guid -> modifier, -100..100)
extern std::unordered_map<uint64_t, int32_t> g_PBC_RollChanceModifiers;
extern std::mutex g_PBC_DataMutex;

// Snapshot of a single character's state, captured on the main thread.
// The event thread works exclusively with these — never touches live Player*.
struct PBC_CharacterSnapshot
{
    ObjectGuid  charObjGuid;
    uint64_t    charGuidRaw = 0;
    std::string charName;

    // Pre-rendered prompt fragments (captured on main thread)
    std::string characterCard;
    std::string context;

    // Raw template variables for re-rendering per-event user prompts
    std::string charGender;
    std::string charRace;
    std::string charClass;
    std::string charRole;
    std::string charLevel;
    std::string charGold;
    std::string scene;
    std::string petInfo;
    std::string charGroup;
    std::string charLos;
    std::string combatStatus;
    std::string equipment;

    // The character's history at the moment of snapshotting.
    // The event thread appends replies locally so subsequent characters
    // see an up-to-date history within the same event.
    std::deque<std::string> history;

    // For whisper responses
    ObjectGuid  whisperTargetGuid;
    std::string whisperTargetName;

    // Names of all other party members (excluding this character).
    std::vector<std::string> partyMemberNames;

    // True if at least one group member is a real (non-bot) player.
    bool hasRealPlayerInGroup = false;
};

// Event types
enum class PBC_EventType : uint8_t
{
    Normal,                 // One or more characters may respond
    QuestSummarization,     // Summarize via LLM, then process responders
    CombatSummarization,    // Summarize via LLM, then process responders
    Condensation,           // Extract memories from history, clear it
    HistoryReload,          // Reload all histories from DB
    RelationshipUpdate,     // Update one character's relationship with a target
    CardAdditionsMigration, // Convert legacy card additions into memories
    Regen,                  // Regenerate the last event's responses
};

// ---------------------------------------------------------------------------
// Central chat history store (mirrors mod_pbc_history)
// ---------------------------------------------------------------------------
struct PBC_HistoryEntry
{
    uint64_t    id         = 0;
    time_t      timestamp  = 0;       // Unix timestamp
    uint64_t    authorGuid = 0;       // 0 = narrator
    uint8_t     type       = 2;       // ChatMsg enum: 0=narrator, 2=PARTY, 7=WHISPER, etc.
    std::string message;              // Raw text, no speaker prefix
};

// Raw source data for an event — the single source of truth.
// All rendered views (histLine, eventLine) are derived from this.
// Every event carries the full struct; unused fields stay default-empty.
struct PBC_EventSource
{
    // Narrator events (item found, duel, level up, combat/quest summary, etc.)
    std::string narratorText;       // Raw narrator text, no wrapper

    // Chat events (say, yell, party, whisper)
    uint64_t    senderGuid = 0;     // 0 for narrator / trigger events
    std::string senderName;         // Empty for narrator / trigger events
    std::string message;           // Empty for narrator / trigger events

    bool IsNarrator() const { return !narratorText.empty(); }
    bool IsChat() const     { return senderGuid != 0 && !message.empty(); }
    bool HasSource() const  { return IsNarrator() || IsChat(); }
};

// ---------------------------------------------------------------------------
// PBC_LastEventRecord
//
// Snapshot of the last Normal event that produced at least one character
// response.  Captured at the end of ProcessNormal so the responses can be
// regenerated later (see the Regen event type).
//
// The record stores:
//   - The pre-mutation copies of every responding character's snapshot
//     (history captured BEFORE the event's replies were appended).
//   - The event metadata needed to re-run ProcessNormal identically
//     (eventLine, source, chatType, canCreateEvents, whisper info, the
//     full participant GUID lists, and the event-local history buffer
//     as it was BEFORE any replies were added — i.e. just the source).
//   - The DB history IDs of every message created by the event (source
//     line + each reply), in chronological order.  These are used to
//     edit the existing messages in place during regeneration so the
//     message IDs (and therefore every character's ownership of them)
//     remain stable.
//   - The GUID of the real player who triggered the original event
//     (used for the regen authorization check).
// ---------------------------------------------------------------------------
struct PBC_LastEventRecord
{
    // Event metadata (copied from the original PBC_EventItem)
    std::string eventLine;
    PBC_EventSource source;
    uint32_t chatType = 0;
    bool canCreateEvents = false;
    std::string whisperSenderName;
    std::string whisperTargetName;

    // Pre-mutation snapshots of the responding characters.
    // Each snapshot's history is the state BEFORE the event's replies
    // were appended, so re-running ProcessNormal reproduces the exact
    // same prompt context.
    std::vector<PBC_CharacterSnapshot> respondingChars;

    // Participant GUID lists (copied from the original event).
    std::vector<uint64_t> silentCharGuids;
    std::vector<uint64_t> playerCharGuids;
    std::vector<uint64_t> replyOnlyCharGuids;

    // The event-local history buffer as it was BEFORE any replies were
    // added — i.e. just the source entry (if any).  Re-running
    // ProcessNormal re-seeds eventHistory from this.
    std::vector<PBC_HistoryEntry> seedEventHistory;

    // DB history IDs of every message created by the original event,
    // in chronological order: [source line (if any), reply 1, reply 2, ...].
    // During regeneration these messages are edited in place.
    std::vector<uint64_t> createdHistoryIds;

    // The real player GUID that triggered the original event (0 if none).
    // Used by the regen authorization check.
    uint64_t requesterGuid = 0;
};

// A single unit of work for the event queue.
struct PBC_EventItem
{
    PBC_EventType type = PBC_EventType::Normal;

    // Normal / QuestSummarization / CombatSummarization fields
    std::string eventLine;          // Present-tense for [CURRENT EVENT]
    PBC_EventSource source;        // Raw event data — single source of truth
    uint32_t    chatType = 0;       // Chat channel for bot replies
    std::vector<PBC_CharacterSnapshot> respondingChars;  // Rolled to respond
    std::vector<uint64_t> silentCharGuids;               // Receive source-derived histLine only
    std::vector<uint64_t> playerCharGuids;               // Real players receiving history

    // For whisper events
    std::string whisperSenderName;
    std::string whisperTargetName;

    // GUIDs that already have source-derived histLine but still need new replies
    std::vector<uint64_t> replyOnlyCharGuids;
    bool canCreateEvents = false;   // If true, triggers secondary events

    // Event-local history accumulator.  Populated during ProcessNormal with
    // structured entries in chronological order (source first, then each
    // reply).  Before each responder's LLM call the current contents are
    // rendered into that character's snapshot so they see the full chain.
    // At the end of processing the buffer is flushed to DB / global memory.
    std::vector<PBC_HistoryEntry> eventHistory;

    // QuestSummarization extra fields
    std::string questSystemPrompt;
    std::string questUserPrompt;

    // CombatSummarization extra fields
    std::string combatSystemPrompt;
    std::string combatUserPrompt;

    // Player who triggered the quest event
    ObjectGuid  anchorObjGuid;

    // Condensation fields
    PBC_CharacterSnapshot condensationChar;
    std::string condensationSystemPrompt;
    std::string condensationUserPrompt;

    // RelationshipUpdate fields
    PBC_CharacterSnapshot relationshipChar;
    std::string     relationshipTargetName;
    std::string     relationshipTargetInfo;
    std::string     relationshipCurrentText;
    std::string     relationshipSystemPrompt;
    std::string     relationshipUserPromptTmpl;

    // CardAdditionsMigration fields
    std::string     migrationCondensationSystemPrompt;
    std::string     migrationCondensationUserPromptTmpl;

    // -----------------------------------------------------------------------
    // Regen fields
    //
    // A Regen event re-runs the last Normal event's responses.  It carries:
    //   - regenRecord: the saved PBC_LastEventRecord (snapshots + metadata)
    //   - regenRequesterGuid: the real player who requested the regen
    //     (used only for logging / WS notifications)
    // -----------------------------------------------------------------------
    std::shared_ptr<PBC_LastEventRecord> regenRecord;
    uint64_t regenRequesterGuid = 0;
};

// Chat-send action posted from event thread to main thread.
struct PBC_PendingAction
{
    ObjectGuid  charGuid;
    ObjectGuid  targetGuid;     // Non-empty = whisper target
    uint32_t    chatType = 0;
    std::string text;           // Empty = no-op
    bool        isNarratorMessage = false;  // Send as narrator system message
};

extern std::queue<PBC_PendingAction> g_PBC_PendingActions;
extern std::mutex                    g_PBC_PendingActionsMutex;

// Secondary event request from event thread to main thread.
struct PBC_PendingEventRequest
{
    std::string eventLine;
    PBC_EventSource source;         // Raw event data from the original event
    uint32_t chatType = 0;
    uint64_t anchorCharGuid = 0;
    std::unordered_set<uint64_t> excludedCharGuids;
    std::vector<uint64_t> originCharGuids;
    std::vector<uint64_t> playerCharGuids;

    // Full accumulated history from the primary event (source + every reply
    // in order).  Written to new targets' DB history before the secondary
    // event is processed so their snapshots capture the complete chain.
    // The secondary event goes through the same ProcessNormal logic as a
    // regular event — no special handling needed.
    std::vector<PBC_HistoryEntry> eventHistory;
};

extern std::queue<PBC_PendingEventRequest> g_PBC_PendingEventRequests;
extern std::mutex                          g_PBC_PendingEventRequestsMutex;

// Whisper request from HTTP API thread to main thread.
struct PBC_PendingWhisperRequest
{
    std::string message;
    uint64_t    senderGuid = 0;
    uint64_t    targetGuid = 0;
};

extern std::queue<PBC_PendingWhisperRequest> g_PBC_PendingWhisperRequests;
extern std::mutex                            g_PBC_PendingWhisperRequestsMutex;

// Party message request from HTTP API thread to main thread.
struct PBC_PendingPartyMessageRequest
{
    std::string senderName;
    uint64_t    senderGuid = 0;
    std::string message;
};

extern std::queue<PBC_PendingPartyMessageRequest> g_PBC_PendingPartyMessageRequests;
extern std::mutex                                  g_PBC_PendingPartyMessageRequestsMutex;

// Trigger request from HTTP API thread to main thread.
struct PBC_PendingTriggerRequest
{
    uint64_t targetGuid = 0;
};

extern std::queue<PBC_PendingTriggerRequest> g_PBC_PendingTriggerRequests;
extern std::mutex                            g_PBC_PendingTriggerRequestsMutex;

// Universal event queue. Drained by OnUpdate — one event at a time.
extern std::queue<PBC_EventItem>  g_PBC_EventQueue;
extern std::mutex                 g_PBC_EventQueueMutex;

// True when no event thread is running.
extern std::atomic<bool> g_PBC_EventThreadDone;

// ---------------------------------------------------------------------------
// Last event record — snapshot of the most recent Normal event that produced
// at least one character response.  Used by the Regen event type to
// regenerate the responses.  Guarded by g_PBC_LastEventMutex.
//
// g_PBC_LastEventRecord is nullptr when no regen-eligible event has been
// processed yet (or when the record has been invalidated — e.g. because new
// messages were appended to an affected character's history after the event).
// ---------------------------------------------------------------------------
extern std::shared_ptr<PBC_LastEventRecord> g_PBC_LastEventRecord;
extern std::mutex                           g_PBC_LastEventMutex;

// All messages, keyed by mod_pbc_history.id
extern std::unordered_map<uint64_t, PBC_HistoryEntry> g_PBC_History;

// Per-character ordered list of history IDs (the character's chronological "view").
// Order = insertion order = history_id ASC = chronological.
extern std::unordered_map<uint64_t, std::deque<uint64_t>> g_PBC_HistoryOwners;

// Single mutex for all three maps below.
extern std::mutex g_PBC_HistoryMutex;

// Last message timestamp per character for time-gap detection
extern std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;

// In-memory character memories
struct PBC_MemoryEntry
{
    uint64_t    dbId = 0;
    std::string text;
    uint8_t     importance = 5;
    std::string createdAt;       // YYYY-MM-DD
};

extern std::unordered_map<uint64_t, std::vector<PBC_MemoryEntry>> g_PBC_Memories;
extern std::mutex g_PBC_MemoriesMutex;

// One relationship entry
struct PBC_RelationshipEntry
{
    std::string text;
    std::string updatedAt;      // YYYY-MM-DD hh:ii:ss
};

// In-memory relationships: bot_guid -> (target_name -> entry)
extern std::unordered_map<uint64_t, std::unordered_map<std::string, PBC_RelationshipEntry>> g_PBC_Relationships;
extern std::mutex g_PBC_RelationshipsMutex;

// Character cards loaded from disk: name -> card text
extern std::unordered_map<std::string, std::string> g_PBC_CharacterCards;

void PBC_PushEvent(PBC_EventItem item);

// Per-character roll chance helper (main-thread only)
uint32_t PBC_GetEffectiveChance(uint64_t botGuid, uint32_t baseChance);

// Loader / WorldScript
void PBC_LoadConfig(bool isStartup = false);
bool PBC_LoadPrompts();
void PBC_LoadCharacterCards();

#endif // MOD_PBC_CONFIG_H
