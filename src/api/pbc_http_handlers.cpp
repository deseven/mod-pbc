#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "pbc_http.h"
#include "pbc_http_handlers.h"
#include "pbc_http_auth.h"
#include "pbc_config.h"
#include "pbc_log.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_utils.h"
#include "pbc_bot_helpers.h"
#include "pbc_character.h"
#include "pbc_event_dispatch.h"

#define httplib pbc_httplib
#include <httplib.h>

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Group.h"
#include "WorldSession.h"

#include "pbc_json.h"

using json = pbc_json;

// ===========================================================================
// Character info helpers (used by /api/account and /api/party)
// ===========================================================================

static const char* HttpGenderStr(uint8_t gender)
{
    switch (gender)
    {
        case GENDER_MALE:   return "Male";
        case GENDER_FEMALE: return "Female";
        default:            return "Unknown";
    }
}

static const char* HttpRaceStr(uint8_t race)
{
    switch (race)
    {
        case RACE_HUMAN:          return "Human";
        case RACE_ORC:            return "Orc";
        case RACE_DWARF:          return "Dwarf";
        case RACE_NIGHTELF:       return "Night Elf";
        case RACE_UNDEAD_PLAYER:  return "Undead";
        case RACE_TAUREN:         return "Tauren";
        case RACE_GNOME:          return "Gnome";
        case RACE_TROLL:          return "Troll";
        case RACE_BLOODELF:       return "Blood Elf";
        case RACE_DRAENEI:        return "Draenei";
        default:                  return "Unknown";
    }
}

static const char* HttpClassStr(uint8_t cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR:      return "Warrior";
        case CLASS_PALADIN:      return "Paladin";
        case CLASS_HUNTER:       return "Hunter";
        case CLASS_ROGUE:        return "Rogue";
        case CLASS_PRIEST:       return "Priest";
        case CLASS_DEATH_KNIGHT: return "Death Knight";
        case CLASS_SHAMAN:       return "Shaman";
        case CLASS_MAGE:         return "Mage";
        case CLASS_WARLOCK:      return "Warlock";
        case CLASS_DRUID:        return "Druid";
        default:                 return "Unknown";
    }
}

// ===========================================================================
// Extract Bearer token from Authorization header.
// ===========================================================================

static std::string ExtractBearerToken(const httplib::Request& req)
{
    std::string auth = req.get_header_value("Authorization");
    if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ")
        return auth.substr(7);
    return "";
}

// ===========================================================================
// Common handler utilities
// ===========================================================================

bool AuthenticateRequest(const httplib::Request& req, httplib::Response& res,
                         PBC_AuthInfo& authInfo)
{
    // 1. Extract Bearer token
    std::string token = ExtractBearerToken(req);
    if (token.empty())
    {
        res.status = 401;
        res.set_content("{\"error\":\"Missing Authorization header. "
                        "Use Authorization: Bearer <token>\"}",
                        "application/json");
        return false;
    }

    // 2. Validate token → account ID
    uint32_t accountId = PBC_ValidateToken(token);
    if (accountId == 0)
    {
        res.status = 401;
        res.set_content("{\"error\":\"Invalid or expired token\"}", "application/json");
        return false;
    }

    // 3. Look up account name
    std::string accountName = PBC_GetAccountName(accountId);
    if (accountName.empty())
    {
        res.status = 401;
        res.set_content("{\"error\":\"Account not found\"}", "application/json");
        return false;
    }

    authInfo.accountId   = accountId;
    authInfo.accountName = accountName;
    return true;
}

Player* FindOnlinePlayerForAccount(uint32_t accountId)
{
    // Query characters DB for online characters on this account
    QueryResult result = CharacterDatabase.Query(
        "SELECT guid FROM characters WHERE account = {} AND online = 1", accountId);
    if (!result)
        return nullptr;

    do
    {
        uint32_t guidLow = (*result)[0].Get<uint32_t>();
        Player* player = ObjectAccessor::FindPlayer(ObjectGuid(HighGuid::Player, guidLow));
        if (player && player->IsInWorld())
        {
            WorldSession* sess = player->GetSession();
            // Return the first real (non-bot) player
            if (PBC_IsRealPlayerSession(sess))
                return player;
        }
    } while (result->NextRow());

    return nullptr;
}

Player* FindOnlineCharacter(uint64_t guid)
{
    ObjectGuid objGuid(guid);
    Player* player = ObjectAccessor::FindPlayer(objGuid);
    if (!player || !player->IsInWorld())
        return nullptr;
    return player;
}

uint64_t ParseGuidParam(const httplib::Request& req, httplib::Response& res)
{
    std::string guidStr;
    auto it = req.path_params.find("guid");
    if (it != req.path_params.end())
        guidStr = it->second;

    if (guidStr.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing guid in URL path\"}", "application/json");
        return 0;
    }

    uint64_t guid = 0;
    try { guid = std::stoull(guidStr); } catch (...) {}
    if (guid == 0)
    {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid guid in URL path\"}", "application/json");
        return 0;
    }

    return guid;
}

// ===========================================================================
// Helper: verify a character GUID belongs to the authenticated account.
// Uses CharacterCache (works for offline characters too).
// Returns true if the character belongs to the account.
// Sets 404 response and returns false if the character doesn't exist or
// belongs to a different account.
// ===========================================================================

static bool VerifyCharOwnership(uint64_t charGuid, uint32_t accountId,
                                httplib::Response& res)
{
    uint32_t charAccount = sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(charGuid));
    if (charAccount == 0)
    {
        res.status = 404;
        res.set_content("{\"error\":\"Character not found\"}", "application/json");
        return false;
    }
    if (charAccount != accountId)
    {
        res.status = 403;
        res.set_content("{\"error\":\"Character does not belong to your account\"}", "application/json");
        return false;
    }
    return true;
}

// ===========================================================================
// Helper: resolve a bot Player* from a char GUID, with validation.
// Only works for online characters. Returns nullptr on failure.
// ===========================================================================

static Player* ResolveOnlineBot(uint64_t charGuid, const PBC_AuthInfo& authInfo,
                                httplib::Response& res)
{
    // First verify ownership
    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return nullptr;

    Player* bot = FindOnlineCharacter(charGuid);
    if (!bot)
    {
        res.status = 404;
        res.set_content("{\"error\":\"Character is not online\"}", "application/json");
        return nullptr;
    }

    WorldSession* session = bot->GetSession();
    bool isOwnCharacter = (sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(charGuid)) == authInfo.accountId);
    if (!session || (PBC_IsBotSession(session) && !isOwnCharacter))
    {
        res.status = 400;
        res.set_content("{\"error\":\"Specified guid is not a character\"}", "application/json");
        return nullptr;
    }

    return bot;
}

// ===========================================================================
// Shared mutation-response helpers
// ===========================================================================

// Map a mutation result (PBC_HistoryResult) to the appropriate HTTP response.
// Returns true if the result was Ok (caller should continue with success).
// On NotFound or Desync, the response is already set and false is returned.
static bool RespondMutationResult(httplib::Response& res, PBC_HistoryResult result,
                                  const std::string& entityName)
{
    if (result == PBC_HistoryResult::NotFound)
    {
        res.status = 404;
        res.set_content("{\"error\":\"" + entityName + " not found\"}", "application/json");
        return false;
    }
    if (result == PBC_HistoryResult::Desync)
    {
        res.status = 409;
        res.set_content("{\"error\":\"desync\"}", "application/json");
        return false;
    }
    return true;
}

// Extract a required string field from a JSON body.  If the field is missing
// or empty, sets a 400 error on the response and returns an empty string.
static std::string ExtractRequiredBodyField(const json& body, const std::string& field,
                                            httplib::Response& res)
{
    std::string value = body.value(field, "");
    if (value.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing or empty '" + field + "' field in request body\"}",
                        "application/json");
    }
    return value;
}

// Extract an optional 'importance' field from a JSON body (1-10, default 5).
static uint8_t ExtractOptionalImportance(const json& body)
{
    if (!body.contains("importance"))
        return 5;
    try { return static_cast<uint8_t>(std::clamp(body["importance"].get<int>(), 1, 10)); }
    catch (...) { return 5; }
}

// Parse and validate the 'id' query parameter as a raw uint64_t (real DB id).
// Returns the parsed id on success, or 0 on error (response already set).
static uint64_t ParseQueryId(const httplib::Request& req, httplib::Response& res)
{
    std::string idStr = req.get_param_value("id");
    if (idStr.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing id parameter\"}", "application/json");
        return 0;
    }
    uint64_t id = 0;
    try { id = std::stoull(idStr); } catch (...) {}
    if (id == 0)
    {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid id parameter (must be >= 1)\"}", "application/json");
        return 0;
    }
    return id;
}

// ===========================================================================
// REST endpoint handlers
// ===========================================================================

// ---------------------------------------------------------------------------
// GET / — health check
// ---------------------------------------------------------------------------
void HandleGetRoot(const httplib::Request& req, httplib::Response& res)
{
    PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
    res.set_content("hello", "text/plain");
}

// ---------------------------------------------------------------------------
// GET /api/token
// ---------------------------------------------------------------------------
void HandleGetToken(const httplib::Request& req, httplib::Response& res)
{
    std::string otp = req.get_param_value("otp");
    if (otp.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing otp parameter\"}", "application/json");
        return;
    }

    std::string token = PBC_ExchangeOTP(otp);
    if (token.empty())
    {
        res.status = 401;
        res.set_content("{\"error\":\"Invalid or expired OTP\"}", "application/json");
        return;
    }

    res.set_content("{\"token\":\"" + token + "\"}", "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/account
// ---------------------------------------------------------------------------
void HandleGetAccount(const httplib::Request& /*req*/, httplib::Response& res,
                      const PBC_AuthInfo& authInfo)
{
    // Query characters DB for all characters on this account
    QueryResult result = CharacterDatabase.Query(
        "SELECT guid, name, gender, race, class, level, online FROM characters WHERE account = {} ORDER BY name",
        authInfo.accountId);

    json chars = json::array();
    if (result)
    {
        do
        {
            uint32_t guidLow  = (*result)[0].Get<uint32_t>();
            std::string name  = (*result)[1].Get<std::string>();
            uint8_t gender    = (*result)[2].Get<uint8_t>();
            uint8_t race      = (*result)[3].Get<uint8_t>();
            uint8_t cls       = (*result)[4].Get<uint8_t>();
            uint8_t level     = (*result)[5].Get<uint8_t>();
            uint8_t online    = (*result)[6].Get<uint8_t>();

            bool isOnline = (online == 1);
            bool isPlayer = false;

            if (isOnline)
            {
                // Check if this is a real player (not a bot)
                Player* p = ObjectAccessor::FindPlayer(ObjectGuid(HighGuid::Player, guidLow));
                if (p && p->IsInWorld())
                {
                    WorldSession* sess = p->GetSession();
                    isPlayer = PBC_IsRealPlayerSession(sess);
                }
            }

            json charJson;
            charJson["guid"]      = guidLow;
            charJson["name"]      = name;
            charJson["gender"]    = HttpGenderStr(gender);
            charJson["race"]      = HttpRaceStr(race);
            charJson["class"]     = HttpClassStr(cls);
            charJson["level"]     = level;
            charJson["is_online"] = isOnline;
            charJson["is_player"] = isPlayer;

            chars.push_back(charJson);
        } while (result->NextRow());
    }

    json response;
    response["account"]     = authInfo.accountName;
    response["account_id"]  = authInfo.accountId;
    response["characters"]  = chars;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/party
// ---------------------------------------------------------------------------
void HandleGetParty(const httplib::Request& /*req*/, httplib::Response& res,
                    const PBC_AuthInfo& authInfo)
{
    json partyGuids = json::array();

    // Find the real player for this account
    Player* player = FindOnlinePlayerForAccount(authInfo.accountId);
    if (!player)
    {
        // No real player online → empty party
        json response;
        response["party"] = partyGuids;
        res.set_content(response.dump(), "application/json");
        return;
    }

    Group* grp = player->GetGroup();
    if (!grp)
    {
        json response;
        response["party"] = partyGuids;
        res.set_content(response.dump(), "application/json");
        return;
    }

    // Collect all party member GUIDs that belong to our account
    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld())
            continue;

        uint64_t memberGuid = member->GetGUID().GetCounter();
        uint32_t memberAccount = sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(memberGuid));

        // Only include characters belonging to the authenticated account
        if (memberAccount == authInfo.accountId)
            partyGuids.push_back(memberGuid);
    }

    json response;
    response["party"] = partyGuids;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/config
// ---------------------------------------------------------------------------
void HandleGetConfig(const httplib::Request& /*req*/, httplib::Response& res,
                     const PBC_AuthInfo& /*authInfo*/)
{
    json config = json::array();
    config.push_back({{"key", "MaxHistoryCtx"},            {"value", g_PBC_MaxHistoryCtx}});
    config.push_back({{"key", "MaxMemoriesCtx"},           {"value", g_PBC_MaxMemoriesCtx}});
    config.push_back({{"key", "ReplyChanceWhisper"},       {"value", g_PBC_ReplyChanceWhisper}});
    config.push_back({{"key", "ReplyChanceMention"},       {"value", g_PBC_ReplyChanceMention}});
    config.push_back({{"key", "ReplyChanceMessage"},       {"value", g_PBC_ReplyChanceMessage}});
    config.push_back({{"key", "RollPenaltyOnAnswer"},      {"value", g_PBC_RollPenaltyOnAnswer}});
    config.push_back({{"key", "ReplyChanceItem"},          {"value", g_PBC_ReplyChanceItem}});
    config.push_back({{"key", "ReplyChanceDuel"},          {"value", g_PBC_ReplyChanceDuel}});
    config.push_back({{"key", "ReplyChanceLevelUp"},       {"value", g_PBC_ReplyChanceLevelUp}});
    config.push_back({{"key", "ReplyChanceHardCombat"},    {"value", g_PBC_ReplyChanceHardCombat}});
    config.push_back({{"key", "ReplyChanceQuestTaken"},    {"value", g_PBC_ReplyChanceQuestTaken}});
    config.push_back({{"key", "ReplyChanceQuestCompleted"},{"value", g_PBC_ReplyChanceQuestCompleted}});
    config.push_back({{"key", "ReplyChanceLocationChanged"},{"value", g_PBC_ReplyChanceLocationChanged}});

    json response;
    response["config"] = config;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/char/:guid/card
// ---------------------------------------------------------------------------
void HandleGetCharCard(const httplib::Request& req, httplib::Response& res,
                       const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    // Verify ownership
    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    // Try CharacterCache first — it holds name, race, class, sex, level
    CharacterCacheEntry const* cacheEntry = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid(charGuid));

    std::string charName;
    Player* bot = nullptr;

    if (cacheEntry)
    {
        charName = cacheEntry->Name;
        // Also try to get the live Player for full substitution
        bot = FindOnlineCharacter(charGuid);
    }
    else
    {
        // Not in cache — try the online player, then DB
        bot = FindOnlineCharacter(charGuid);
        if (bot)
        {
            charName = bot->GetName();
        }
        else
        {
            QueryResult result = CharacterDatabase.Query(
                "SELECT name FROM characters WHERE guid = {}", charGuid);
            if (result)
                charName = (*result)[0].Get<std::string>();
        }
    }

    if (charName.empty())
    {
        res.status = 404;
        res.set_content("{\"error\":\"Character not found\"}", "application/json");
        return;
    }

    // Get the raw card text
    std::string cardText;
    auto cardIt = g_PBC_CharacterCards.find(charName);
    if (cardIt != g_PBC_CharacterCards.end())
        cardText = cardIt->second;
    else
        cardText = g_PBC_DefaultCharacterDescription;

    // Substitute template variables
    if (bot)
    {
        // Online — full substitution via live Player*
        cardText = PBC_SubstituteVars(cardText, bot, "", false, false);
    }
    else if (cacheEntry)
    {
        // Offline but cached — substitute static vars from CharacterCache
        PBC_VarMap vars;
        vars["char_name"]   = cacheEntry->Name;
        vars["char_gender"] = PBC_GenderStr(cacheEntry->Sex);
        vars["char_race"]   = PBC_RaceStr(cacheEntry->Race);
        vars["char_class"]  = PBC_ClassStr(cacheEntry->Class);
        vars["char_level"]  = std::to_string(cacheEntry->Level);

        PBC_SubstituteFromMap(cardText, vars, false);
        PBC_CleanUnknownTokens(cardText);
    }
    // else: offline and not in cache (extremely rare) — return raw text,
    //       which still shows placeholders.  Better than nothing.

    json response;
    response["card"] = cardText;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/char/:guid/context
// ---------------------------------------------------------------------------
void HandleGetCharContext(const httplib::Request& req, httplib::Response& res,
                          const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    Player* bot = ResolveOnlineBot(charGuid, authInfo, res);
    if (!bot) return;

    json response;
    std::string context = PBC_SubstituteVars(g_PBC_CharacterContext, bot, "", false, true);
    PBC_StripEmptyAnnotatedLines(context);
    response["context"] = context;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/char/:guid/history
// ---------------------------------------------------------------------------
void HandleGetCharHistory(const httplib::Request& req, httplib::Response& res,
                          const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    // Verify ownership
    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    int page  = 1;
    int limit = 0;

    std::string pageStr  = req.get_param_value("page");
    std::string limitStr = req.get_param_value("limit");

    if (!pageStr.empty())
    {
        try { page = std::stoi(pageStr); } catch (...) { page = 1; }
        if (page < 1) page = 1;
    }
    if (!limitStr.empty())
    {
        try { limit = std::stoi(limitStr); } catch (...) { limit = 50; }
        if (limit < 0)  limit = 0;
        if (limit > 200) limit = 200;
    }

    size_t total = 0;
    json messages = json::array();

    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        auto it = g_PBC_HistoryOwners.find(charGuid);
        if (it != g_PBC_HistoryOwners.end())
        {
            total = it->second.size();

            if (total > 0)
            {
                if (limit == 0)
                {
                    for (size_t i = 0; i < total; ++i)
                    {
                        uint64_t historyId = it->second[i];
                        auto entryIt = g_PBC_History.find(historyId);
                        if (entryIt == g_PBC_History.end()) continue;
                        const auto& e = entryIt->second;
                        json msg;
                        msg["id"]          = e.id;
                        msg["text"]        = PBC_RenderHistoryLine(e, charGuid);
                        msg["author_guid"] = e.authorGuid;
                        msg["type"]        = e.type;
                        msg["message"]     = e.message;
                        msg["author_name"] = PBC_GetCharacterName(e.authorGuid);
                        messages.push_back(msg);
                    }
                }
                else
                {
                    size_t skipFromEnd = static_cast<size_t>((page - 1) * limit);
                    if (skipFromEnd < total)
                    {
                        size_t endIdx   = total - skipFromEnd;
                        size_t startIdx = (endIdx > static_cast<size_t>(limit))
                                          ? endIdx - static_cast<size_t>(limit)
                                          : 0;
                        for (size_t i = startIdx; i < endIdx; ++i)
                        {
                            uint64_t historyId = it->second[i];
                            auto entryIt = g_PBC_History.find(historyId);
                            if (entryIt == g_PBC_History.end()) continue;
                            const auto& e = entryIt->second;
                            json msg;
                            msg["id"]          = e.id;
                            msg["text"]        = PBC_RenderHistoryLine(e, charGuid);
                            msg["author_guid"] = e.authorGuid;
                            msg["type"]        = e.type;
                            msg["message"]     = e.message;
                            msg["author_name"] = PBC_GetCharacterName(e.authorGuid);
                            messages.push_back(msg);
                        }
                    }
                }
            }
        }
    }

    int totalPages = (limit > 0 && total > 0)
                     ? static_cast<int>((total + limit - 1) / limit)
                     : (total > 0 ? 1 : 0);

    json response;
    response["messages"]    = messages;
    response["page"]        = page;
    response["limit"]       = limit;
    response["total"]       = total;
    response["total_pages"] = totalPages;

    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/char/:guid/history
// ---------------------------------------------------------------------------
void HandlePostCharHistory(const httplib::Request& req, httplib::Response& res,
                           const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    uint64_t historyId = ParseQueryId(req, res);
    if (historyId == 0) return;

    json body;
    try { body = json::parse(req.body); } catch (...) {}
    std::string newMessage = ExtractRequiredBodyField(body, "message", res);
    if (newMessage.empty()) return;

    PBC_HistoryResult result = PBC_UpdateHistoryMessage(historyId, newMessage);
    if (!RespondMutationResult(res, result, "Message")) return;

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API history edit: historyId={}", historyId);
    res.set_content("{\"status\":\"updated\"}", "application/json");
}

// ---------------------------------------------------------------------------
// DELETE /api/char/:guid/history
// ---------------------------------------------------------------------------
void HandleDeleteCharHistory(const httplib::Request& req, httplib::Response& res,
                             const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    uint64_t historyId = ParseQueryId(req, res);
    if (historyId == 0) return;

    PBC_HistoryResult result = PBC_DeleteHistoryMessage(historyId);
    if (!RespondMutationResult(res, result, "Message")) return;

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API history delete: historyId={}", historyId);
    res.set_content("{\"status\":\"deleted\"}", "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/char/:guid/memory/count
// ---------------------------------------------------------------------------
void HandleGetCharMemoryCount(const httplib::Request& req, httplib::Response& res,
                              const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
        auto it = g_PBC_Memories.find(charGuid);
        if (it != g_PBC_Memories.end())
            count = it->second.size();
    }

    json response;
    response["count"] = count;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/char/:guid/memory
// ---------------------------------------------------------------------------
void HandleGetCharMemory(const httplib::Request& req, httplib::Response& res,
                         const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    std::string orderBy  = req.get_param_value("order_by");
    std::string orderDir = req.get_param_value("order_dir");
    int page  = 1;
    int limit = 0;

    std::string pageStr  = req.get_param_value("page");
    std::string limitStr = req.get_param_value("limit");

    if (!pageStr.empty())
    {
        try { page = std::stoi(pageStr); } catch (...) { page = 1; }
        if (page < 1) page = 1;
    }
    if (!limitStr.empty())
    {
        try { limit = std::stoi(limitStr); } catch (...) { limit = 50; }
        if (limit < 0)  limit = 0;
        if (limit > 200) limit = 200;
    }

    if (orderBy.empty()) orderBy = "id";
    if (orderBy != "id" && orderBy != "memory_text" && orderBy != "importance")
    {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid order_by. Must be id, memory_text, or importance\"}", "application/json");
        return;
    }

    if (orderDir.empty())
        orderDir = (orderBy == "memory_text") ? "asc" : "desc";
    if (orderDir != "asc" && orderDir != "desc")
    {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid order_dir. Must be asc or desc\"}", "application/json");
        return;
    }

    bool desc = (orderDir == "desc");

    size_t total = 0;
    json memories = json::array();

    {
        std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
        auto it = g_PBC_Memories.find(charGuid);
        if (it != g_PBC_Memories.end())
        {
            std::vector<const PBC_MemoryEntry*> entries;
            entries.reserve(it->second.size());
            for (const auto& e : it->second)
                entries.push_back(&e);

            std::sort(entries.begin(), entries.end(),
                [orderBy, desc](const PBC_MemoryEntry* a, const PBC_MemoryEntry* b)
                {
                    bool less;
                    if (orderBy == "id")
                        less = a->dbId < b->dbId;
                    else if (orderBy == "importance")
                        less = a->importance < b->importance;
                    else
                        less = a->text < b->text;
                    return desc ? !less : less;
                });

            total = entries.size();

            if (total > 0)
            {
                if (limit == 0)
                {
                    for (const auto* e : entries)
                        memories.push_back({{"id", e->dbId}, {"memory_text", e->text}, {"importance", e->importance}, {"created_at", e->createdAt}});
                }
                else
                {
                    size_t startIdx = static_cast<size_t>((page - 1) * limit);
                    if (startIdx < total)
                    {
                        size_t endIdx = std::min(startIdx + static_cast<size_t>(limit), total);
                        for (size_t i = startIdx; i < endIdx; ++i)
                            memories.push_back({{"id", entries[i]->dbId}, {"memory_text", entries[i]->text}, {"importance", entries[i]->importance}, {"created_at", entries[i]->createdAt}});
                    }
                }
            }
        }
    }

    int totalPages = (limit > 0 && total > 0)
                     ? static_cast<int>((total + limit - 1) / limit)
                     : (total > 0 ? 1 : 0);

    json response;
    response["memories"]    = memories;
    response["page"]        = page;
    response["limit"]       = limit;
    response["total"]       = total;
    response["total_pages"] = totalPages;

    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/char/:guid/memory/:id
// ---------------------------------------------------------------------------
void HandlePostCharMemory(const httplib::Request& req, httplib::Response& res,
                          const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    std::string memIdStr;
    auto it = req.path_params.find("id");
    if (it != req.path_params.end())
        memIdStr = it->second;

    uint64_t memId = 0;
    try { memId = std::stoull(memIdStr); } catch (...) {}
    if (memId == 0)
    {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid memory id in URL path\"}", "application/json");
        return;
    }

    json body;
    try { body = json::parse(req.body); } catch (...) {}
    std::string newText      = ExtractRequiredBodyField(body, "memory_text", res);
    std::string originalText = ExtractRequiredBodyField(body, "original", res);
    if (newText.empty() || originalText.empty()) return;

    uint8_t newImportance = ExtractOptionalImportance(body);

    PBC_HistoryResult result = PBC_UpdateMemory(charGuid, memId, newText, newImportance, originalText);
    if (!RespondMutationResult(res, result, "Memory")) return;

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API memory edit: character GUID={} memory id={}", charGuid, memId);
    res.set_content("{\"status\":\"updated\"}", "application/json");
}

// ---------------------------------------------------------------------------
// DELETE /api/char/:guid/memory/:id
// ---------------------------------------------------------------------------
void HandleDeleteCharMemory(const httplib::Request& req, httplib::Response& res,
                            const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    std::string memIdStr;
    auto it = req.path_params.find("id");
    if (it != req.path_params.end())
        memIdStr = it->second;

    uint64_t memId = 0;
    try { memId = std::stoull(memIdStr); } catch (...) {}
    if (memId == 0)
    {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid memory id in URL path\"}", "application/json");
        return;
    }

    json body;
    try { body = json::parse(req.body); } catch (...) {}
    std::string originalText = ExtractRequiredBodyField(body, "original", res);
    if (originalText.empty()) return;

    PBC_HistoryResult result = PBC_DeleteMemory(charGuid, memId, originalText);
    if (!RespondMutationResult(res, result, "Memory")) return;

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API memory delete: character GUID={} memory id={}", charGuid, memId);
    res.set_content("{\"status\":\"deleted\"}", "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/char/:guid/relationships
// ---------------------------------------------------------------------------
void HandleGetCharRelationships(const httplib::Request& req, httplib::Response& res,
                                const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    json relationships = json::object();
    {
        std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
        auto relIt = g_PBC_Relationships.find(charGuid);
        if (relIt != g_PBC_Relationships.end())
            for (const auto& kv : relIt->second)
                relationships[kv.first] = {{"text", kv.second.text}, {"updated_at", kv.second.updatedAt}};
    }

    json response;
    response["relationships"] = relationships;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/char/:guid/relationships
// ---------------------------------------------------------------------------
void HandlePostCharRelationships(const httplib::Request& req, httplib::Response& res,
                                 const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    std::string targetName = req.get_param_value("name");
    if (targetName.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing name parameter\"}", "application/json");
        return;
    }

    json body;
    try { body = json::parse(req.body); } catch (...) {}
    std::string newText      = ExtractRequiredBodyField(body, "text", res);
    std::string originalText = ExtractRequiredBodyField(body, "original", res);
    if (newText.empty() || originalText.empty()) return;

    PBC_HistoryResult result = PBC_UpdateRelationship(charGuid, targetName, newText, originalText);
    if (!RespondMutationResult(res, result, "Relationship")) return;

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API relationship edit: character GUID={} target={}", charGuid, targetName);
    res.set_content("{\"status\":\"updated\"}", "application/json");
}

// ---------------------------------------------------------------------------
// DELETE /api/char/:guid/relationships
// ---------------------------------------------------------------------------
void HandleDeleteCharRelationships(const httplib::Request& req, httplib::Response& res,
                                   const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    std::string targetName = req.get_param_value("name");
    if (targetName.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing name parameter\"}", "application/json");
        return;
    }

    json body;
    try { body = json::parse(req.body); } catch (...) {}
    std::string originalText = ExtractRequiredBodyField(body, "original", res);
    if (originalText.empty()) return;

    PBC_HistoryResult result = PBC_DeleteRelationship(charGuid, targetName, originalText);
    if (!RespondMutationResult(res, result, "Relationship")) return;

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API relationship delete: character GUID={} target={}", charGuid, targetName);
    res.set_content("{\"status\":\"deleted\"}", "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/char/:guid/data
// ---------------------------------------------------------------------------
void HandleGetCharData(const httplib::Request& req, httplib::Response& res,
                       const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    int32_t rollMod = 0;
    {
        std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
        auto it = g_PBC_RollChanceModifiers.find(charGuid);
        if (it != g_PBC_RollChanceModifiers.end())
            rollMod = it->second;
    }

    json data = json::array();
    data.push_back({{"key", "roll_modifier"}, {"value", rollMod}});

    json response;
    response["data"] = data;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/char/:guid/data
// ---------------------------------------------------------------------------
void HandlePostCharData(const httplib::Request& req, httplib::Response& res,
                        const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    json body;
    try { body = json::parse(req.body); } catch (...) {}

    if (!body.contains("data") || !body["data"].is_array())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing or invalid 'data' array in request body\"}", "application/json");
        return;
    }

    for (const auto& item : body["data"])
    {
        std::string key = item.value("key", "");
        if (key == "roll_modifier")
        {
            int32_t value = 0;
            try { value = item["value"].get<int32_t>(); } catch (...) {}
            if (value < -100 || value > 100)
            {
                res.status = 400;
                res.set_content("{\"error\":\"roll_modifier must be between -100 and 100\"}", "application/json");
                return;
            }

            {
                std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
                if (value == 0)
                    g_PBC_RollChanceModifiers.erase(charGuid);
                else
                    g_PBC_RollChanceModifiers[charGuid] = value;
            }
            DB_UpsertRollChanceModifier(charGuid, value);

            PBC_Log(PBC_LogLevel::PBC_DEBUG, "API data update: character GUID={} roll_modifier={}", charGuid, value);
        }
    }

    res.set_content("{\"status\":\"updated\"}", "application/json");
}

// ---------------------------------------------------------------------------
// GET /api/char/:guid/debug/request
// ---------------------------------------------------------------------------
void HandleGetCharDebugRequest(const httplib::Request& req, httplib::Response& res,
                               const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    Player* bot = ResolveOnlineBot(charGuid, authInfo, res);
    if (!bot) return;

    std::string eventLine = req.get_param_value("event");
    if (eventLine.empty())
        eventLine = PBC_MakeEventLine(PBC_PickTriggerEventLine(charGuid, bot->GetName()));

    PBC_CharacterSnapshot snap = PBC_SnapshotCharacter(bot);
    std::string userPrompt = PBC_BuildUserPromptFromSnapshot(snap, eventLine);

    int histTokens = PBC_EstimateHistoryTokens(snap.charGuidRaw);
    bool condensationNeeded = histTokens > static_cast<int>(g_PBC_MaxHistoryCtx);

    json response;
    response["system_prompt"] = g_PBC_SystemPrompt;
    response["user_prompt"]   = userPrompt;
    response["event"]         = eventLine;
    response["condensation_needed"] = condensationNeeded;
    response["history_tokens"]      = histTokens;
    response["history_token_limit"] = g_PBC_MaxHistoryCtx;

    std::string memoriesBlock = PBC_GetMemoriesBlock(snap.charGuidRaw);
    int memTokens = memoriesBlock.empty() ? 0 : PBC_EstimateTokens(memoriesBlock);
    response["memory_tokens"]      = memTokens;
    response["memory_token_limit"] = g_PBC_MaxMemoriesCtx;

    json snapshotJson;
    snapshotJson["character_card"] = snap.characterCard;
    snapshotJson["context"]        = snap.context;
    snapshotJson["scene"]          = snap.scene;
    snapshotJson["combat_status"]  = snap.combatStatus;
    snapshotJson["equipment"]      = snap.equipment;
    snapshotJson["char_group"]     = snap.charGroup;
    snapshotJson["char_los"]       = snap.charLos;
    snapshotJson["memories"]       = memoriesBlock;
    snapshotJson["relationships"]  = PBC_GetRelationshipsBlock(snap);

    {
        std::ostringstream histOss;
        for (const auto& line : snap.history)
            histOss << line << "\n";
        snapshotJson["chat_history"] = histOss.str();
    }

    response["snapshot"] = snapshotJson;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/char/:guid/whisper
// ---------------------------------------------------------------------------
void HandlePostCharWhisper(const httplib::Request& req, httplib::Response& res,
                           const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    // Find the real player for this account (as sender)
    Player* sender = FindOnlinePlayerForAccount(authInfo.accountId);
    if (!sender)
    {
        res.status = 410;
        res.set_content("{\"error\":\"No real player from your account is online\"}", "application/json");
        return;
    }

    uint64_t senderGuid = sender->GetGUID().GetCounter();

    // Block whispering to yourself
    if (charGuid == senderGuid)
    {
        res.status = 400;
        res.set_content("{\"error\":\"Cannot whisper to yourself\"}", "application/json");
        return;
    }

    // Verify target is online and a character
    Player* bot = ResolveOnlineBot(charGuid, authInfo, res);
    if (!bot) return;

    json body;
    try { body = json::parse(req.body); } catch (...) {}
    std::string message = body.value("message", "");
    if (message.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing or empty 'message' field in request body\"}", "application/json");
        return;
    }

    // Queue the whisper request for the main thread
    {
        PBC_PendingWhisperRequest wr;
        wr.message    = message;
        wr.senderGuid = senderGuid;
        wr.targetGuid = charGuid;

        std::lock_guard<std::mutex> lock(g_PBC_PendingWhisperRequestsMutex);
        g_PBC_PendingWhisperRequests.push(std::move(wr));
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API whisper queued: player GUID={} -> character GUID={}", senderGuid, charGuid);
    res.set_content("{\"status\":\"queued\"}", "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/char/:guid/narrate
// ---------------------------------------------------------------------------
void HandlePostCharNarrate(const httplib::Request& req, httplib::Response& res,
                           const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    // Verify ownership (no online requirement)
    if (!VerifyCharOwnership(charGuid, authInfo.accountId, res))
        return;

    json body;
    try { body = json::parse(req.body); } catch (...) {}
    std::string message = body.value("message", "");
    if (message.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing or empty 'message' field in request body\"}", "application/json");
        return;
    }

    std::vector<uint64_t> owners = {charGuid};
    PBC_AppendHistoryMessage(0, 0, message, owners);

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API narrate: character GUID={} message=\"{}\"", charGuid, message);
    res.set_content("{\"status\":\"ok\"}", "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/party/narrate
// ---------------------------------------------------------------------------
void HandlePostPartyNarrate(const httplib::Request& req, httplib::Response& res,
                            const PBC_AuthInfo& authInfo)
{
    json body;
    try { body = json::parse(req.body); } catch (...) {}
    std::string message = body.value("message", "");
    if (message.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing or empty 'message' field in request body\"}", "application/json");
        return;
    }

    // Find the real player for this account
    Player* player = FindOnlinePlayerForAccount(authInfo.accountId);
    if (!player)
    {
        res.status = 400;
        res.set_content("{\"error\":\"No real player from your account is online\"}", "application/json");
        return;
    }

    Group* grp = player->GetGroup();
    if (!grp)
    {
        res.status = 400;
        res.set_content("{\"error\":\"Player is not in a group\"}", "application/json");
        return;
    }

    std::vector<uint64_t> owners;
    int count = 0;

    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld()) continue;
        WorldSession* sess = member->GetSession();
        if (!PBC_IsBotSession(sess)) continue;

        uint64_t memberGuid = member->GetGUID().GetCounter();
        uint32_t memberAccount = sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(memberGuid));

        // Only narrate to characters belonging to the authenticated account
        if (memberAccount != authInfo.accountId)
            continue;

        owners.push_back(memberGuid);
        ++count;
    }

    // Also write the narrator line to the player's own character history
    owners.push_back(player->GetGUID().GetCounter());
    PBC_AppendHistoryMessage(0, 0, message, owners);

    if (count == 0)
    {
        res.status = 400;
        res.set_content("{\"error\":\"No characters found in your group belonging to your account\"}", "application/json");
        return;
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API party narrate: account={} characters={}",
             authInfo.accountId, count);

    json response;
    response["status"]           = "ok";
    response["characters_count"] = count;
    res.set_content(response.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/char/:guid/trigger
// ---------------------------------------------------------------------------
void HandlePostCharTrigger(const httplib::Request& req, httplib::Response& res,
                           const PBC_AuthInfo& authInfo)
{
    uint64_t charGuid = ParseGuidParam(req, res);
    if (charGuid == 0) return;

    // Verify the target is either a bot or the player's own tracked character
    Player* target = FindOnlineCharacter(charGuid);
    if (!target || !target->IsInWorld())
    {
        res.status = 404;
        res.set_content("{\"error\":\"Target is not online\"}", "application/json");
        return;
    }

    WorldSession* ts = target->GetSession();
    if (!ts)
    {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid target\"}", "application/json");
        return;
    }

    // Verify target belongs to the authenticated account
    uint32_t targetAccount = sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(charGuid));
    if (targetAccount != authInfo.accountId)
    {
        res.status = 403;
        res.set_content("{\"error\":\"Character does not belong to your account\"}", "application/json");
        return;
    }

    bool isBot = PBC_IsBotSession(ts);
    bool isOwnCharacter = !isBot;

    if (!isBot && !isOwnCharacter)
    {
        res.status = 400;
        res.set_content("{\"error\":\"Specified guid is not a character\"}", "application/json");
        return;
    }

    // Queue the trigger request for the main thread
    {
        PBC_PendingTriggerRequest tr;
        tr.targetGuid = charGuid;

        std::lock_guard<std::mutex> lock(g_PBC_PendingTriggerRequestsMutex);
        g_PBC_PendingTriggerRequests.push(std::move(tr));
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API trigger queued: character GUID={} (isBot={} isOwnCharacter={})",
             charGuid, isBot, isOwnCharacter);
    res.set_content("{\"status\":\"queued\"}", "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/party/message
// ---------------------------------------------------------------------------
void HandlePostPartyMessage(const httplib::Request& req, httplib::Response& res,
                            const PBC_AuthInfo& authInfo)
{
    json body;
    try { body = json::parse(req.body); } catch (...) {}
    std::string message = body.value("message", "");
    if (message.empty())
    {
        res.status = 400;
        res.set_content("{\"error\":\"Missing or empty 'message' field in request body\"}", "application/json");
        return;
    }

    // Find the real player for this account
    Player* player = FindOnlinePlayerForAccount(authInfo.accountId);
    if (!player)
    {
        res.status = 400;
        res.set_content("{\"error\":\"No real player from your account is online\"}", "application/json");
        return;
    }

    Group* grp = player->GetGroup();
    if (!grp)
    {
        res.status = 400;
        res.set_content("{\"error\":\"Player is not in a group\"}", "application/json");
        return;
    }

    // Queue the party message request for the main thread
    {
        PBC_PendingPartyMessageRequest pm;
        pm.senderName = player->GetName();
        pm.senderGuid = player->GetGUID().GetCounter();
        pm.message    = message;

        std::lock_guard<std::mutex> lock(g_PBC_PendingPartyMessageRequestsMutex);
        g_PBC_PendingPartyMessageRequests.push(std::move(pm));
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API party message queued: player GUID={}",
             player->GetGUID().GetCounter());
    res.set_content("{\"status\":\"queued\"}", "application/json");
}

// ---------------------------------------------------------------------------
// POST /api/regen-last
// ---------------------------------------------------------------------------
void HandlePostRegenLast(const httplib::Request& /*req*/, httplib::Response& res,
                         const PBC_AuthInfo& authInfo)
{
    // Find the real player for this account
    Player* player = FindOnlinePlayerForAccount(authInfo.accountId);
    if (!player)
    {
        res.status = 410;
        res.set_content("{\"error\":\"No real player from your account is online\"}", "application/json");
        return;
    }

    if (!PBC_CanRegenLastEvent())
    {
        res.status = 409;
        res.set_content("{\"error\":\"No regen-eligible last event available\"}", "application/json");
        return;
    }

    if (!PBC_IsPlayerInLastEventGroup(player))
    {
        res.status = 403;
        res.set_content("{\"error\":\"You are not in a group with the characters from the last event\"}", "application/json");
        return;
    }

    // Queue the regen request for the main thread (the dispatch function
    // checks the event queue is empty and pushes the Regen event).
    uint64_t requesterGuid = player->GetGUID().GetCounter();
    if (!PBC_DispatchRegenEvent(requesterGuid))
    {
        res.status = 409;
        res.set_content("{\"error\":\"Could not queue regeneration (event queue is busy)\"}", "application/json");
        return;
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "API regen-last queued: player GUID={}", requesterGuid);
    res.set_content("{\"status\":\"queued\"}", "application/json");
}
