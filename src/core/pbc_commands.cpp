#include "pbc_commands.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_bot_helpers.h"
#include "pbc_event_dispatch.h"
#include "Chat.h"
#include "Config.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "ChatCommand.h"
#include "Log.h"
#include "WorldSession.h"
#include "Group.h"

#include <algorithm>
#include <mutex>
#include <sstream>

using namespace Acore::ChatCommands;

static Player* FindTarget(ChatHandler* handler, std::optional<std::string_view> nameArg)
{
    if (!nameArg || nameArg->empty())
    {
        // Console has no session/player — caller must supply a name
        if (!handler->GetSession())
            return nullptr;
        return handler->GetSession()->GetPlayer();
    }
    return ObjectAccessor::FindPlayerByName(std::string(*nameArg));
}

// ---------------------------------------------------------------------------
// .chars reload
// ---------------------------------------------------------------------------
static bool HandleCharsReload(ChatHandler* handler, Optional<std::string_view>)
{
    sConfigMgr->Reload();
    PBC_LoadConfig();
    PBC_LoadPrompts();
    PBC_LoadCharacterCards();
    PBC_LoadMemoriesFromDB();
    PBC_LoadCharacterDataFromDB();

    // Reload history (and relationships) from DB safely by posting a
    // HistoryReload event onto the queue.  It will be processed after all
    // currently queued events finish, so no in-flight history writes are lost.
    // PBC_ProcessEventItem handles the relationship reload inside HistoryReload.
    PBC_EventItem ev;
    ev.type = PBC_EventType::HistoryReload;
    PBC_PushEvent(std::move(ev));

    handler->PSendSysMessage("[PBC] Config, prompts, character cards, memories and character data reloaded. History/relationship reload queued (runs after pending events).");
    return true;
}

// ---------------------------------------------------------------------------
// .chars condense [char_name]
// ---------------------------------------------------------------------------
static bool HandleCharsCondense(ChatHandler* handler, Optional<std::string_view> nameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars condense <char_name>");
        return false;
    }
    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }

    WorldSession* targetSess = target->GetSession();
    if (!targetSess)
    {
        handler->PSendSysMessage("[PBC] '{}' has no session.", target->GetName());
        return false;
    }

    bool isBot = PBC_IsBotSession(targetSess);
    bool isOwnCharacter = false;

    if (!isBot && handler->GetSession())
    {
        Player* callingPlayer = handler->GetSession()->GetPlayer();
        if (callingPlayer && callingPlayer->GetGUID() == target->GetGUID())
            isOwnCharacter = true;
    }

    if (!isBot && !isOwnCharacter)
    {
        handler->PSendSysMessage("[PBC] '{}' is not a playerbot.", target->GetName());
        return false;
    }

    PBC_TriggerCondensation(target);
    handler->PSendSysMessage("[PBC] Condensation triggered for '{}'.", target->GetName());
    return true;
}

// ---------------------------------------------------------------------------
// .chars info [char_name]
// ---------------------------------------------------------------------------
static bool HandleCharsInfo(ChatHandler* handler, Optional<std::string_view> nameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars info <char_name>");
        return false;
    }
    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }

    uint64_t botGuid = target->GetGUID().GetCounter();
    std::string card = PBC_GetCharacterCard(target);

    int memCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
        auto it = g_PBC_Memories.find(botGuid);
        if (it != g_PBC_Memories.end())
            memCount = static_cast<int>(it->second.size());
    }

    int histCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto it = g_PBC_HistoryOwners.find(botGuid);
        if (it != g_PBC_HistoryOwners.end())
            histCount = static_cast<int>(it->second.size());
    }

    int estimatedTokens = PBC_EstimateHistoryTokens(botGuid);

    int32_t rollMod = 0;
    {
        std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
        auto it = g_PBC_RollChanceModifiers.find(botGuid);
        if (it != g_PBC_RollChanceModifiers.end())
            rollMod = it->second;
    }

    handler->PSendSysMessage("[PBC] === {} ===", target->GetName());
    handler->PSendSysMessage("[PBC] Memories: {}  |  History lines: {}  |  Est. tokens: {}/{}  |  Roll modifier: {:+d}",
        memCount, histCount, estimatedTokens, g_PBC_MaxHistoryCtx, rollMod);
    handler->PSendSysMessage("[PBC] Card:\n{}", card);
    return true;
}

// ---------------------------------------------------------------------------
// .chars reset [char_name]  /  .chars reset @ALL
// ---------------------------------------------------------------------------
static bool HandleCharsReset(ChatHandler* handler, Optional<std::string_view> nameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars reset <char_name>  or  chars reset @ALL");
        return false;
    }

    // Handle the @ALL special token: wipe every bot's data.
    if (nameArg && *nameArg == "@ALL")
    {
        CharacterDatabase.Execute("DELETE FROM mod_pbc_history");
        CharacterDatabase.Execute("DELETE FROM mod_pbc_history_owners");
        DB_DeleteAllMemories();
        DB_DeleteAllRelationships();

        {
            std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
            g_PBC_History.clear();
            g_PBC_HistoryOwners.clear();
            g_PBC_LastHistoryTime.clear();
        }
        {
            std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
            g_PBC_Memories.clear();
        }
        {
            std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
            g_PBC_Relationships.clear();
        }

        handler->PSendSysMessage("[PBC] History, memories and relationships cleared for ALL characters.");
        return true;
    }

    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }

    uint64_t botGuid = target->GetGUID().GetCounter();

    DB_RemoveAllHistoryOwnership(botGuid);
    DB_DeleteMemoriesForCharacter(botGuid);
    DB_DeleteRelationshipsForCharacter(botGuid);

    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        g_PBC_HistoryOwners.erase(botGuid);
        g_PBC_LastHistoryTime.erase(botGuid);
    }
    {
        std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
        g_PBC_Memories.erase(botGuid);
    }
    {
        std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
        g_PBC_Relationships.erase(botGuid);
    }

    handler->PSendSysMessage("[PBC] History, memories and relationships cleared for '{}'.", target->GetName());
    return true;
}

// ---------------------------------------------------------------------------
// .chars history [char_name] [num=5]
// ---------------------------------------------------------------------------
static bool HandleCharsHistory(ChatHandler* handler, Optional<std::string_view> nameArg,
                                Optional<int32> numArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars history <char_name> [num]");
        return false;
    }

    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }

    int num = numArg ? static_cast<int>(*numArg) : 5;
    num = std::max(1, std::min(num, 20));

    uint64_t botGuid = target->GetGUID().GetCounter();

    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    auto it = g_PBC_HistoryOwners.find(botGuid);
    if (it == g_PBC_HistoryOwners.end() || it->second.empty())
    {
        handler->PSendSysMessage("[PBC] No history found for '{}'.", target->GetName());
        return true;
    }

    const auto& idList = it->second;
    int total = static_cast<int>(idList.size());
    int start = std::max(0, total - num);

    handler->PSendSysMessage("[PBC] Last {} history entries for '{}' ({} total):",
        total - start, target->GetName(), total);
    for (int i = start; i < total; ++i)
    {
        auto entryIt = g_PBC_History.find(idList[i]);
        if (entryIt == g_PBC_History.end()) continue;
        std::string line = PBC_RenderHistoryLine(entryIt->second, botGuid);
        handler->PSendSysMessage("{}", line);
    }

    return true;
}

// ---------------------------------------------------------------------------
// .chars relationship <char_name> <target_char_name>
// Outputs char_name's relationship towards target_char_name.
// ---------------------------------------------------------------------------
static bool HandleCharsRelationship(ChatHandler* handler,
                                    std::string_view charNameArg,
                                    std::string_view targetNameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if (charNameArg.empty() || targetNameArg.empty())
    {
        handler->PSendSysMessage("[PBC] Usage: chars relationship <char_name> <target_char_name>");
        return false;
    }

    Player* bot = ObjectAccessor::FindPlayerByName(std::string(charNameArg));
    if (!bot)
    {
        handler->PSendSysMessage("[PBC] Character '{}' not found or not online.", charNameArg);
        return false;
    }

    uint64_t botGuid = bot->GetGUID().GetCounter();
    std::string targetName(targetNameArg);

    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    auto botIt = g_PBC_Relationships.find(botGuid);
    if (botIt == g_PBC_Relationships.end())
    {
        handler->PSendSysMessage("[PBC] No relationship data found for '{}'.", bot->GetName());
        return true;
    }

    auto tgtIt = botIt->second.find(targetName);
    if (tgtIt == botIt->second.end())
    {
        handler->PSendSysMessage("[PBC] No relationship entry for '{}' -> '{}'.",
            bot->GetName(), targetName);
        return true;
    }

    handler->PSendSysMessage("[PBC] {}'s relationship with {}:\n{}",
        bot->GetName(), targetName,
        tgtIt->second.text);
    return true;
}

// ---------------------------------------------------------------------------
// .chars relationship-update <char_name> <target_char_name>
// Forces an immediate relationship update LLM call for char_name -> target.
// ---------------------------------------------------------------------------
static bool HandleCharsRelationshipUpdate(ChatHandler* handler,
                                          std::string_view charNameArg,
                                          std::string_view targetNameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if (charNameArg.empty() || targetNameArg.empty())
    {
        handler->PSendSysMessage("[PBC] Usage: chars relationship-update <char_name> <target_char_name>");
        return false;
    }

    Player* bot = ObjectAccessor::FindPlayerByName(std::string(charNameArg));
    if (!bot)
    {
        handler->PSendSysMessage("[PBC] Character '{}' not found or not online.", charNameArg);
        return false;
    }
    if (!PBC_IsBot(bot))
    {
        handler->PSendSysMessage("[PBC] '{}' is not a playerbot.", bot->GetName());
        return false;
    }

    if (g_PBC_RelationshipUpdateSystemPrompt.empty() || g_PBC_RelationshipUpdateUserPrompt.empty())
    {
        handler->PSendSysMessage("[PBC] Relationship update prompts are not configured.");
        return false;
    }

    std::string targetName(targetNameArg);
    uint64_t botGuid = bot->GetGUID().GetCounter();

    // Read current relationship text
    std::string currentRel;
    {
        std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
        auto botIt = g_PBC_Relationships.find(botGuid);
        if (botIt != g_PBC_Relationships.end())
        {
            auto tgtIt = botIt->second.find(targetName);
            if (tgtIt != botIt->second.end())
                currentRel = tgtIt->second.text;
        }
    }
    if (currentRel.empty())
        currentRel = PBC_DefaultRelationshipText(targetName);

    PBC_CharacterSnapshot snap = PBC_SnapshotCharacter(bot);

    PBC_EventItem relEv;
    relEv.type                       = PBC_EventType::RelationshipUpdate;
    relEv.relationshipChar            = std::move(snap);
    relEv.relationshipTargetName     = targetName;
    relEv.relationshipTargetInfo     = targetName;
    relEv.relationshipCurrentText    = currentRel;
    relEv.relationshipSystemPrompt   = g_PBC_RelationshipUpdateSystemPrompt;
    relEv.relationshipUserPromptTmpl = g_PBC_RelationshipUpdateUserPrompt;

    PBC_PushEvent(std::move(relEv));

    handler->PSendSysMessage("[PBC] Relationship update queued for '{}' -> '{}'.",
        bot->GetName(), targetName);
    return true;
}

// ---------------------------------------------------------------------------
// .chars roll_modifier [char_name] [roll_modifier]
// Sets or displays the per-character roll chance modifier (-100 to 100).
// ---------------------------------------------------------------------------
static bool HandleCharsRollModifier(ChatHandler* handler,
                                    Optional<std::string_view> nameArg,
                                    Optional<int32> modifierArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    // Need at least a character name
    if (!nameArg || nameArg->empty())
    {
        handler->PSendSysMessage("[PBC] Usage: chars roll-modifier <char_name> [roll_modifier]");
        handler->PSendSysMessage("[PBC]   roll_modifier: integer from -100 to 100. Positive = more talkative, negative = less talkative.");
        handler->PSendSysMessage("[PBC]   Omit roll_modifier to display the current value.");
        return false;
    }

    Player* target = ObjectAccessor::FindPlayerByName(std::string(*nameArg));
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Character '{}' not found or not online.", *nameArg);
        return false;
    }

    uint64_t botGuid = target->GetGUID().GetCounter();

    // No modifier argument: display current value
    if (!modifierArg)
    {
        int32_t currentMod = 0;
        {
            std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
            auto it = g_PBC_RollChanceModifiers.find(botGuid);
            if (it != g_PBC_RollChanceModifiers.end())
                currentMod = it->second;
        }
        handler->PSendSysMessage("[PBC] {}'s roll chance modifier: {:+d}", target->GetName(), currentMod);
        return true;
    }

    int32_t modifier = *modifierArg;
    if (modifier < -100 || modifier > 100)
    {
        handler->PSendSysMessage("[PBC] Roll modifier must be between -100 and 100 (got {}).", modifier);
        return false;
    }

    // Update in-memory map
    {
        std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
        if (modifier == 0)
            g_PBC_RollChanceModifiers.erase(botGuid);
        else
            g_PBC_RollChanceModifiers[botGuid] = modifier;
    }

    // Persist to database
    DB_UpsertRollChanceModifier(botGuid, modifier);

    handler->PSendSysMessage("[PBC] {}'s roll chance modifier set to {:+d}.", target->GetName(), modifier);
    return true;
}

// ---------------------------------------------------------------------------
// .chars connection-test [connection_name]
// Sends a test request to the specified connection (default if omitted).
// ---------------------------------------------------------------------------
static bool HandleCharsConnectionTest(ChatHandler* handler,
                                       Optional<std::string_view> nameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    std::string connName = (nameArg && !nameArg->empty()) ? std::string(*nameArg) : "default";

    // PBC_GetConnection falls back to the default connection when the requested
    // slot is not specifically configured. Detect that so we can inform the user.
    bool fellBack = false;
    const PBC_APIConfig* cfg = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_PBC_ConnectionsMutex);
        auto it = g_PBC_Connections.find(connName);
        if (it != g_PBC_Connections.end())
        {
            cfg = &it->second;
        }
        else
        {
            it = g_PBC_Connections.find("default");
            if (it != g_PBC_Connections.end())
            {
                cfg = &it->second;
                fellBack = true;
            }
        }
    }

    if (!cfg)
    {
        handler->PSendSysMessage("[PBC] No connections configured.");
        return false;
    }

    if (fellBack)
        handler->PSendSysMessage("[PBC] Connection '{}' is not configured, using default.", connName);

    handler->PSendSysMessage("[PBC] Connection '{}' test (model='{}'): querying with 'hi'...",
        fellBack ? "default" : connName, cfg->model);

    PBC_LLMResult result = PBC_CallLLMWithConfig(*cfg, "Answer in one single short sentence.", "hi");

    if (result.success)
    {
        handler->PSendSysMessage("[PBC] Connection '{}' test OK ({} tokens): {}", connName, result.tokensUsed, result.text);
    }
    else
    {
        handler->PSendSysMessage("[PBC] Connection '{}' test FAILED — no valid response received. Check server logs for details.", connName);
    }

    return result.success;
}

// ---------------------------------------------------------------------------
// .chars context [char_name]
// ---------------------------------------------------------------------------
static bool HandleCharsContext(ChatHandler* handler, Optional<std::string_view> nameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }
    if ((!nameArg || nameArg->empty()) && !handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] Usage from console: chars context <char_name>");
        return false;
    }
    Player* target = FindTarget(handler, nameArg);
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Player not found or not online.");
        return false;
    }

    std::string context = PBC_GetCharacterContext(target);
    handler->PSendSysMessage("[PBC] === {} context ===\n{}", target->GetName(), context);
    return true;
}

// ---------------------------------------------------------------------------
// .chars web
// ---------------------------------------------------------------------------
static bool HandleCharsWeb(ChatHandler* handler, Optional<std::string_view>)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    // This command only works in-game
    if (!handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] This command is intended to be used in-game only.");
        return false;
    }

    // Check if the HTTP server is running
    if (!PBC_HttpServerIsRunning())
    {
        handler->PSendSysMessage("[PBC] Web interface is not enabled.");
        return false;
    }

    Player* player = handler->GetSession()->GetPlayer();
    if (!player)
    {
        handler->PSendSysMessage("[PBC] Could not retrieve player data.");
        return false;
    }

    uint32_t accountId = handler->GetSession()->GetAccountId();
    std::string otp = PBC_HttpServerGenerateOTP(accountId);

    if (otp.empty())
    {
        handler->PSendSysMessage("[PBC] Failed to generate one-time password. Please try again.");
        return false;
    }

    handler->PSendSysMessage("[PBC] To use web-interface, go to {} and input your one-time password: {}",
        g_PBC_HttpServerBaseUrl, otp);
    return true;
}

// ---------------------------------------------------------------------------
// .chars narrate <char_name> <message>
// Adds a Narrator: *<message>* line to the specified character's history.
// In-game only — no LLM call, no event dispatch, pure history append.
// ---------------------------------------------------------------------------
static bool HandleCharsNarrate(ChatHandler* handler,
                               std::string_view charNameArg,
                               Tail messageArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    if (!handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] This command is only available in-game.");
        return false;
    }

    if (charNameArg.empty() || messageArg.empty())
    {
        handler->PSendSysMessage("[PBC] Usage: chars narrate <char_name> <message>");
        return false;
    }

    Player* target = ObjectAccessor::FindPlayerByName(std::string(charNameArg));
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Character '{}' not found or not online.", charNameArg);
        return false;
    }

    WorldSession* targetSess = target->GetSession();
    if (!targetSess)
    {
        handler->PSendSysMessage("[PBC] '{}' has no session.", target->GetName());
        return false;
    }

    bool isBot = PBC_IsBotSession(targetSess);
    bool isOwnCharacter = false;

    if (!isBot && handler->GetSession())
    {
        Player* callingPlayer = handler->GetSession()->GetPlayer();
        if (callingPlayer && callingPlayer->GetGUID() == target->GetGUID())
            isOwnCharacter = true;
    }

    if (!isBot && !isOwnCharacter)
    {
        handler->PSendSysMessage("[PBC] '{}' is not a playerbot.", target->GetName());
        return false;
    }

    uint64_t targetGuid = target->GetGUID().GetCounter();
    std::vector<uint64_t> owners = {targetGuid};
    PBC_AppendHistoryMessage(0, 0, std::string(messageArg), owners);

    handler->PSendSysMessage("[PBC] Narrator line added to '{}'s history.", target->GetName());
    return true;
}

// ---------------------------------------------------------------------------
// .chars trigger <char_name>
// Triggers a response from the specified character. The character responds
// as a party message if they are in a group, or as a say otherwise.
// The trigger event (*you feel the urge to say something*) is NOT written
// into the character's history.
// ---------------------------------------------------------------------------
static bool HandleCharsTrigger(ChatHandler* handler, std::string_view charNameArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    if (charNameArg.empty())
    {
        handler->PSendSysMessage("[PBC] Usage: chars trigger <char_name>");
        return false;
    }

    Player* target = ObjectAccessor::FindPlayerByName(std::string(charNameArg));
    if (!target)
    {
        handler->PSendSysMessage("[PBC] Character '{}' not found or not online.", charNameArg);
        return false;
    }

    // Allow triggering bot characters and the player's own character.
    WorldSession* ts = target->GetSession();
    if (!ts)
    {
        handler->PSendSysMessage("[PBC] '{}' has no session.", target->GetName());
        return false;
    }

    bool isBot = PBC_IsBotSession(ts);
    bool isOwnCharacter = false;

    if (!isBot && handler->GetSession())
    {
        Player* callingPlayer = handler->GetSession()->GetPlayer();
        if (callingPlayer && callingPlayer->GetGUID() == target->GetGUID())
            isOwnCharacter = true;
    }

    if (!isBot && !isOwnCharacter)
    {
        handler->PSendSysMessage("[PBC] '{}' is not a playerbot.", target->GetName());
        return false;
    }

    PBC_DispatchTriggerEvent(target);
    handler->PSendSysMessage("[PBC] Trigger event queued for '{}'.", target->GetName());
    return true;
}

// ---------------------------------------------------------------------------
// .chars narrate-party <message>
// Adds a Narrator: *<message>* line to every playerbot in the caller's group.
// In-game only — fails if the caller has no bots in their group.
// ---------------------------------------------------------------------------
static bool HandleCharsNarrateParty(ChatHandler* handler, Tail messageArg)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    if (!handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] This command is only available in-game.");
        return false;
    }

    if (messageArg.empty())
    {
        handler->PSendSysMessage("[PBC] Usage: chars narrate-party <message>");
        return false;
    }

    Player* player = handler->GetSession()->GetPlayer();
    if (!player)
    {
        handler->PSendSysMessage("[PBC] Could not retrieve player data.");
        return false;
    }

    Group* grp = player->GetGroup();
    if (!grp)
    {
        handler->PSendSysMessage("[PBC] You are not in a group.");
        return false;
    }

    std::vector<uint64_t> owners;
    int count = 0;

    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_IsBotSession(sess)) continue;

        owners.push_back(member->GetGUID().GetCounter());
        ++count;
    }

    // Also write the narrator line to the calling player's own character history.
    owners.push_back(player->GetGUID().GetCounter());
    PBC_AppendHistoryMessage(0, 0, std::string(messageArg), owners);
    ++count;

    if (count == 0)
    {
        handler->PSendSysMessage("[PBC] No playerbots found in your group.");
        return false;
    }

    handler->PSendSysMessage("[PBC] Narrator line added to {} character(s) in your group.", count);
    return true;
}

// ---------------------------------------------------------------------------
// .chars migrate-card-additions
// Console-only command that queues a CardAdditionsMigration event.
// The event thread reads all rows from the legacy mod_pbc_character_card_additions
// table, feeds each bot's additions through the condensation LLM prompt, and
// inserts the resulting discrete memories into mod_pbc_memories.
// ---------------------------------------------------------------------------
static bool HandleCharsMigrateCardAdditions(ChatHandler* handler, Optional<std::string_view>)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    if (handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] This command is only available from the server console.");
        return false;
    }

    PBC_EventItem ev;
    ev.type = PBC_EventType::CardAdditionsMigration;
    ev.migrationCondensationSystemPrompt  = g_PBC_CondensationSystemPrompt;
    ev.migrationCondensationUserPromptTmpl = g_PBC_CondensationUserPrompt;
    PBC_PushEvent(std::move(ev));

    g_PBC_CardAdditionsMigrationNeeded = false;

    handler->PSendSysMessage("[PBC] Card additions migration queued. Watch the server console for progress.");
    return true;
}


// ---------------------------------------------------------------------------
// .chars regen-last
// Regenerates the responses of the last event that produced character
// replies.  Only available when the last event is regen-eligible (no new
// messages appended to affected characters' histories since) and the
// caller is in the same group as the event's characters.
// In-game only.
// ---------------------------------------------------------------------------
static bool HandleCharsRegenLast(ChatHandler* handler, Optional<std::string_view>)
{
    if (!g_PBC_Enable) { handler->PSendSysMessage("[PBC] Module is disabled."); return false; }

    if (!handler->GetSession())
    {
        handler->PSendSysMessage("[PBC] This command is only available in-game.");
        return false;
    }

    Player* player = handler->GetSession()->GetPlayer();
    if (!player)
    {
        handler->PSendSysMessage("[PBC] Could not retrieve player data.");
        return false;
    }

    if (!PBC_CanRegenLastEvent())
    {
        handler->PSendSysMessage("[PBC] No regen-eligible last event available.");
        return true;
    }

    if (!PBC_IsPlayerInLastEventGroup(player))
    {
        handler->PSendSysMessage("[PBC] You are not in a group with the characters from the last event.");
        return false;
    }

    uint64_t requesterGuid = player->GetGUID().GetCounter();
    if (!PBC_DispatchRegenEvent(requesterGuid))
    {
        handler->PSendSysMessage("[PBC] Could not queue regeneration (event queue is busy or no record available).");
        return false;
    }

    handler->PSendSysMessage("[PBC] Regeneration of the last event's responses queued.");
    return true;
}


PBC_CommandScript::PBC_CommandScript() : CommandScript("PBC_CommandScript") {}

ChatCommandTable PBC_CommandScript::GetCommands() const
{
    static ChatCommandTable charsSubCommands =
    {
        { "reload",                   HandleCharsReload,                  SEC_GAMEMASTER, Console::Yes },
        { "condense",                 HandleCharsCondense,                SEC_PLAYER,    Console::Yes },
        { "info",                     HandleCharsInfo,                    SEC_PLAYER,    Console::Yes },
        { "reset",                    HandleCharsReset,                   SEC_GAMEMASTER, Console::Yes },
        { "history",                  HandleCharsHistory,                 SEC_PLAYER,    Console::Yes },
        { "relationship",             HandleCharsRelationship,            SEC_PLAYER,    Console::Yes },
        { "relationship-update",      HandleCharsRelationshipUpdate,      SEC_PLAYER,    Console::Yes },
        { "roll-modifier",            HandleCharsRollModifier,            SEC_PLAYER,    Console::Yes },
        { "context",                  HandleCharsContext,                 SEC_PLAYER,    Console::Yes },
        { "connection-test",          HandleCharsConnectionTest,          SEC_GAMEMASTER, Console::Yes },
        { "web",                      HandleCharsWeb,                     SEC_PLAYER,    Console::No  },
        { "narrate",                  HandleCharsNarrate,                 SEC_PLAYER,    Console::No  },
        { "narrate-party",            HandleCharsNarrateParty,            SEC_PLAYER,    Console::No  },
        { "trigger",                  HandleCharsTrigger,                 SEC_PLAYER,    Console::Yes },
        { "regen-last",               HandleCharsRegenLast,               SEC_PLAYER,    Console::No  },
        { "migrate-card-additions",   HandleCharsMigrateCardAdditions,    SEC_GAMEMASTER, Console::Yes },
    };

    static ChatCommandTable rootTable =
    {
        { "chars", charsSubCommands },
    };

    return rootTable;
}
