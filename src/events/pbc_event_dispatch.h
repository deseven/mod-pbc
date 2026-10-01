#ifndef MOD_PBC_EVENT_DISPATCH_H
#define MOD_PBC_EVENT_DISPATCH_H

#include <cstdint>
#include <string>
#include <vector>

class Player;
struct PBC_EventItem;

// ---------------------------------------------------------------------------
// Narrator event formatting helpers
// ---------------------------------------------------------------------------
std::string PBC_MakeEventLine(const std::string& text);
std::string PBC_MakeHistLine(const std::string& text);

// Derive a histLine from a PBC_EventSource.  For narrator events produces
// "Narrator: *text*", for chat events produces "Name: message", and
// returns "" when the source is empty (e.g. trigger events).
struct PBC_EventSource;
std::string PBC_MakeHistLineFromSource(const PBC_EventSource& source);

// ---------------------------------------------------------------------------
// Send a narrator system message to all real (non-bot) players in the
// same group as 'anchor' (or to anchor alone if ungrouped).
// No-op when PBC.DisplayNarratorEvents is disabled.
// ---------------------------------------------------------------------------
void PBC_NotifyRealPlayersInGroup(Player* anchor, const std::string& eventLine);

// ---------------------------------------------------------------------------
// Dispatch a group event (from any translation unit).
// Builds a PBC_EventItem with snapshots for all bots in anchor's group,
// rolls each bot's chance, and pushes the item onto the global event queue.
// narratorText: raw narrator text (no prefix/asterisk wrapper) — stored in
//   ev.source.narratorText; histLine is derived automatically.
// notifyRealPlayers=false: suppress the narrator system message sent to real
// players in the group (used for combat events, which can be very frequent).
// ---------------------------------------------------------------------------
void PBC_DispatchGroupEvent(Player* anchor, const std::string& eventLine,
                             const std::string& narratorText, uint32_t chance,
                             bool notifyRealPlayers = true);

// ---------------------------------------------------------------------------
// Roll bots with decaying penalty.  Does NOT shuffle — caller should shuffle
// the bot list first if randomised order is desired.
// Fills ev.respondingChars and ev.silentCharGuids.
// debugLabel is used in log messages to identify the roll context.
// ---------------------------------------------------------------------------
void PBC_RollBotsWithPenalty(PBC_EventItem& ev,
                              const std::vector<Player*>& bots,
                              uint32_t baseChance,
                              const char* debugLabel = "Roll");

// ---------------------------------------------------------------------------
// Convenience wrapper: finds group bots for 'player', shuffles them, and
// rolls with penalty into the provided event item.  Returns true if any bots
// were found (regardless of roll outcomes), false if the player has no group
// bots — in which case the caller should abort the event.
// ---------------------------------------------------------------------------
bool PBC_RollGroupBotsIntoEvent(PBC_EventItem& ev, Player* player,
                                 uint32_t chance, const char* debugLabel = "event");

// ---------------------------------------------------------------------------
// Full message roll logic: checks for mentions, sorts mentioned bots by
// position in the message, rolls mentioned bots at mention chance, then
// non-mentioned bots at a reduced chance with decaying penalty.
// Handles shuffling internally.
// ---------------------------------------------------------------------------
void PBC_RollBotsForMessage(PBC_EventItem& ev,
                             const std::vector<Player*>& bots,
                             const std::string& message);

// ---------------------------------------------------------------------------
// Dispatch a whisper event from sender to target bot.
// Rolls chance, takes snapshot with whisper target info, pushes PBC_EventItem.
// Used by both in-game whisper hook and HTTP API whisper endpoint.
// ---------------------------------------------------------------------------
void PBC_DispatchWhisperEvent(Player* sender, Player* target, const std::string& msg);

// ---------------------------------------------------------------------------
// Pick the trigger event line based on the character's last history message.
// charName is the name of the character being triggered, used to detect
// whether the last message was spoken by the same character (case 3 fork).
// Returns the event text (without surrounding asterisks — caller wraps with
// PBC_MakeEventLine).  Thread-safe.
//
// Logic:
//   0. No history → "you feel the urge to say something".
//   1. Last line is "Narrator: *some time passes*" → random pick from four
//      "you want to …" variants.
//   2. Last line is any other Narrator line →
//      "you feel the urge to comment on the last thing that happened".
//   3. Last line is not Narrator and not a whisper:
//      a. Speaker is charName → "you feel like saying more".
//      b. Speaker is someone else → "you feel like answering that".
//   4. Whisper or fallthrough →
//      "you feel the urge to say something".
// ---------------------------------------------------------------------------
std::string PBC_PickTriggerEventLine(uint64_t botGuid, const std::string& charName);

// ---------------------------------------------------------------------------
// Dispatch a trigger event for a single character.
// The character always responds (no roll). The event line is picked
// dynamically based on the last history message (see PBC_PickTriggerEventLine)
// and is NOT written to history.
// Chat type is PARTY if the character is in a group, SAY otherwise.
// ---------------------------------------------------------------------------
void PBC_DispatchTriggerEvent(Player* bot);

// ---------------------------------------------------------------------------
// Dispatch a party/raid chat message event from sender to their group bots.
// Finds group bots, rolls chances (mention-aware), pushes PBC_EventItem.
// senderNameOverride: when non-empty, replaces sender->GetName() (used by API
// where the "sender" may be a name string rather than the actual Player* name).
// chatType: defaults to CHAT_MSG_PARTY; pass CHAT_MSG_RAID etc. for raid chat.
// canCreateEvents: defaults to true; set false to prevent secondary events.
// ---------------------------------------------------------------------------
void PBC_DispatchPartyMessageEvent(Player* sender, const std::string& msg,
                                    const std::string& senderNameOverride = "",
                                    uint32_t chatType = 0,
                                    bool canCreateEvents = true);

// ---------------------------------------------------------------------------
// Dispatch a guild chat message event from a real player to same-guild bots.
// Finds all online bot guildmates, rolls chances (mention-aware), pushes
// PBC_EventItem with chatType = CHAT_MSG_GUILD.
// ---------------------------------------------------------------------------
void PBC_DispatchGuildMessageEvent(Player* sender, const std::string& msg);

// ---------------------------------------------------------------------------
// Adds all real (non-bot) players in the anchor's group (including the anchor
// itself if it's a real player) to the event's silentCharGuids and
// playerCharGuids.  This ensures player characters receive history passively
// during play sessions.
//
// When subGroupOnly is true, only the real players in the anchor's own
// sub-group (party) within a raid are tracked.  This is used for party-chat
// messages sent inside a raid, which only reach the sender's sub-group.
// ---------------------------------------------------------------------------
void AddTrackedPlayersToEvent(PBC_EventItem& ev, Player* anchor,
                              bool subGroupOnly = false);

// ---------------------------------------------------------------------------
// Regeneration of the last event's responses.
//
// PBC_CanRegenLastEvent returns true if there is a regen-eligible last event
// record (i.e. the last Normal event produced at least one character
// response and no messages have been appended to any affected character's
// history since).  Thread-safe.
//
// PBC_IsPlayerInLastEventGroup returns true if 'player' is in the same group
// as at least one of the characters that participated in the last event
// (responding or silent).  This is the authorization check used by the
// .chars regen-last command and the /api/regen-last endpoint.  Main-thread
// only (walks group membership).
//
// PBC_DispatchRegenEvent builds a Regen PBC_EventItem from the saved
// PBC_LastEventRecord and pushes it onto the global event queue.  The
// 'requesterGuid' is stored in the event for logging/WS.  Returns true if
// a regen event was queued, false if no regen-eligible record exists.
// Safe to call from the main thread.
// ---------------------------------------------------------------------------
bool PBC_CanRegenLastEvent();
bool PBC_IsPlayerInLastEventGroup(Player* player);
bool PBC_DispatchRegenEvent(uint64_t requesterGuid);

#endif // MOD_PBC_EVENT_DISPATCH_H
