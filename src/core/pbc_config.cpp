#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_log.h"
#include "Config.h"
#include "World.h"
#include "Common.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Chat.h"
#include "Group.h"
#include "SharedDefines.h"

#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <unordered_set>

using json = pbc_json;

// Global variable definitions

bool     g_PBC_Enable              = true;
bool     g_PBC_DebugEnabled        = false;
bool     g_PBC_DebugShowFullRequest = false;
bool     g_PBC_DisplayNarratorEvents = true;
bool     g_PBC_CardAdditionsMigrationNeeded = false;

// Connection registry
std::unordered_map<std::string, PBC_APIConfig> g_PBC_Connections;
std::mutex g_PBC_ConnectionsMutex;

uint32_t    g_PBC_MaxHistoryCtx              = 0;
uint32_t    g_PBC_MaxMemoriesCtx             = 8192;

std::string g_PBC_SystemPrompt;
std::string g_PBC_UserPrompt;
std::string g_PBC_CondensationSystemPrompt;
std::string g_PBC_CondensationUserPrompt;
std::string g_PBC_DefaultCharacterDescription;
std::string g_PBC_CharacterContext;

std::string g_PBC_RelationshipUpdateSystemPrompt;
std::string g_PBC_RelationshipUpdateUserPrompt;

std::string g_PBC_PromptsPath = "../../../modules/mod-pbc/prompts";
std::string g_PBC_CharacterCardsPath = "../../../modules/mod-pbc/characters";

uint32_t g_PBC_ReplyChanceWhisper   = 100;
uint32_t g_PBC_ReplyChanceMention   = 100;
uint32_t g_PBC_ReplyChanceMessage   = 100;
uint32_t g_PBC_RollPenaltyOnAnswer  = 45;
uint32_t g_PBC_ReplyChanceItem     = 5;
uint32_t g_PBC_ReplyChanceDuel     = 5;
uint32_t g_PBC_ReplyChanceLevelUp  = 5;
uint32_t g_PBC_ReplyChanceHardCombat    = 25;
uint32_t g_PBC_ReplyChanceQuestCompleted = 20;
uint32_t g_PBC_ReplyChanceQuestTaken     = 10;
uint32_t g_PBC_ReplyChanceLocationChanged = 15;

uint32_t g_PBC_LocationChangeDebounceCycles = 5;
uint32_t g_PBC_CombatEndDebounceCycles      = 5;

std::string g_PBC_QuestCompletedSystemPrompt;
std::string g_PBC_QuestCompletedUserPrompt;
std::string g_PBC_QuestTakenSystemPrompt;
std::string g_PBC_QuestTakenUserPrompt;

std::string g_PBC_CombatEndedSystemPrompt;
std::string g_PBC_CombatEndedUserPrompt;

std::vector<std::string> g_PBC_Blacklist;

bool        g_PBC_IgnoreAllAddonMessages   = true;

int         g_PBC_HttpServerPort            = 0;
std::string g_PBC_HttpServerBind            = "127.0.0.1";
int         g_PBC_HttpServerTimeout         = 15;
std::string g_PBC_HttpServerBaseUrl         = "http://127.0.0.1:8501";
std::string g_PBC_HttpServerPrivateKey;
std::string g_PBC_HttpServerFrontendPath    = "../../../modules/mod-pbc/frontend/dist";

std::queue<PBC_PendingAction> g_PBC_PendingActions;
std::mutex                    g_PBC_PendingActionsMutex;

std::queue<PBC_PendingEventRequest> g_PBC_PendingEventRequests;
std::mutex                          g_PBC_PendingEventRequestsMutex;

std::queue<PBC_PendingWhisperRequest> g_PBC_PendingWhisperRequests;
std::mutex                            g_PBC_PendingWhisperRequestsMutex;

std::queue<PBC_PendingPartyMessageRequest> g_PBC_PendingPartyMessageRequests;
std::mutex                                  g_PBC_PendingPartyMessageRequestsMutex;

std::queue<PBC_PendingTriggerRequest> g_PBC_PendingTriggerRequests;
std::mutex                            g_PBC_PendingTriggerRequestsMutex;

std::queue<PBC_EventItem>  g_PBC_EventQueue;
std::mutex                 g_PBC_EventQueueMutex;
std::atomic<bool>          g_PBC_EventThreadDone{ true };

std::shared_ptr<PBC_LastEventRecord> g_PBC_LastEventRecord;
std::mutex                           g_PBC_LastEventMutex;

std::unordered_map<uint64_t, PBC_HistoryEntry>     g_PBC_History;
std::unordered_map<uint64_t, std::deque<uint64_t>> g_PBC_HistoryOwners;
std::mutex g_PBC_HistoryMutex;

std::unordered_map<uint64_t, std::vector<PBC_MemoryEntry>> g_PBC_Memories;
std::mutex g_PBC_MemoriesMutex;

std::unordered_map<std::string, std::string> g_PBC_CharacterCards;

std::unordered_map<uint64_t, std::unordered_map<std::string, PBC_RelationshipEntry>> g_PBC_Relationships;
std::mutex g_PBC_RelationshipsMutex;

std::unordered_map<uint64_t, int32_t> g_PBC_RollChanceModifiers;
std::mutex g_PBC_DataMutex;

std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;

static std::vector<std::string> SplitByComma(const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ','))
    {
        if (!tok.empty())
            out.push_back(tok);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Connection file loading
//
// Each connection is a JSONC file (JSON with comments).
//
// Files are cached by resolved path so a file referenced by multiple slots
// (e.g. DefaultConnection and UtilityConnection pointing to the same file)
// is read and parsed only once per config load.
// ---------------------------------------------------------------------------

// Parses a connection JSONC file into a PBC_APIConfig. Returns false on error.
static bool PBC_LoadConnectionFile(const std::string& path, PBC_APIConfig& out)
{
    std::ifstream f(path);
    if (!f)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Connection file not found: {}", path);
        return false;
    }

    std::stringstream buf;
    buf << f.rdbuf();
    std::string content = buf.str();

    if (content.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Connection file is empty: {}", path);
        return false;
    }

    json root;
    try
    {
        // ignore_comments + ignore_trailing_commas → full JSONC support
        // (comments stripped, trailing commas allowed).
        root = json::parse(content, nullptr,
                           /*allow_exceptions=*/true,
                           /*ignore_comments=*/true,
                           /*ignore_trailing_commas=*/true);
    }
    catch (const std::exception& ex)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Failed to parse connection file '{}': {}", path, ex.what());
        return false;
    }

    if (!root.is_object())
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Connection file '{}' must contain a JSON object.", path);
        return false;
    }

    // Required fields
    if (!root.contains("apiType") || !root["apiType"].is_string())
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Connection file '{}' missing required string field 'apiType'.", path);
        return false;
    }
    if (!root.contains("baseUrl") || !root["baseUrl"].is_string())
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Connection file '{}' missing required string field 'baseUrl'.", path);
        return false;
    }
    if (!root.contains("model") || !root["model"].is_string())
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Connection file '{}' missing required string field 'model'.", path);
        return false;
    }

    out.apiType = root["apiType"].get<std::string>();
    out.baseUrl = root["baseUrl"].get<std::string>();
    out.model   = root["model"].get<std::string>();

    // Optional fields
    out.apiKey           = root.value("apiKey", "");
    out.requestTimeoutSec = root.value("requestTimeoutSec", 30);

    // requestParameters: default to an empty object
    if (root.contains("requestParameters") && root["requestParameters"].is_object())
        out.requestParameters = root["requestParameters"];
    else
        out.requestParameters = json::object();

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "Loaded connection: apiType='{}' model='{}' baseUrl='{}' timeout={}s",
             out.apiType, out.model, out.baseUrl, out.requestTimeoutSec);

    return true;
}

// ---------------------------------------------------------------------------
// Legacy config fallback
//
// When PBC.DefaultConnection is empty but the legacy flat API parameters are
// present, synthesize virtual connection objects from them so existing users
// can upgrade without immediately migrating their config.
//
// The legacy parameters are read with showLogs=false to suppress the
// missing-option warning that acore would otherwise emit for keys that are no
// longer in the dist config.
//
// This logic is isolated so it can be removed entirely in a future release.
// ---------------------------------------------------------------------------

// Builds a PBC_APIConfig from the legacy flat parameters. Returns false if the
// legacy baseUrl and model are both empty (i.e. no legacy config present).
static bool PBC_BuildLegacyConnection(
    const std::string& apiType,
    const std::string& baseUrl,
    const std::string& apiKey,
    const std::string& model,
    int maxResponseTokens,
    double temperature,
    const std::string& modelExtraParameters,
    int requestTimeoutSec,
    PBC_APIConfig& out)
{
    if (baseUrl.empty() && model.empty())
        return false;

    out.apiType           = apiType.empty() ? "openai" : apiType;
    out.baseUrl           = baseUrl;
    out.apiKey            = apiKey;
    out.model             = model;
    out.requestTimeoutSec = requestTimeoutSec > 0 ? requestTimeoutSec : 30;
    out.requestParameters = json::object();

    // MaxResponseLength → max_tokens (only if > 0)
    if (maxResponseTokens > 0)
        out.requestParameters["max_tokens"] = maxResponseTokens;

    // Temperature (always set — matches the old behavior where it was sent unconditionally)
    out.requestParameters["temperature"] = temperature;

    // ModelExtraParameters: a raw JSON fragment using single quotes instead of
    // double quotes. Best-effort parse: replace single quotes with double
    // quotes, wrap in braces, and attempt to parse.
    if (!modelExtraParameters.empty())
    {
        std::string fixed = modelExtraParameters;
        std::replace(fixed.begin(), fixed.end(), '\'', '"');
        // Wrap in braces so it becomes a valid JSON object
        fixed = "{" + fixed + "}";

        try
        {
            json extra = json::parse(fixed);
            if (extra.is_object())
                out.requestParameters.update(extra, /*merge_objects=*/true);
        }
        catch (const std::exception& ex)
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING,
                     "Failed to parse legacy ModelExtraParameters (value='{}'), ignoring. "
                     "Migrate to a connection file to set requestParameters manually. Error: {}",
                     modelExtraParameters, ex.what());
        }
    }

    return true;
}

static bool PBC_LoadLegacyConnections()
{
    // Read all legacy keys silently (showLogs=false) so acore does not warn
    // about keys that are no longer in the dist config.
    constexpr bool kSilent = false;

    std::string apiType     = sConfigMgr->GetOption<std::string>("PBC.APIType", "openai", kSilent);
    std::string baseUrl     = sConfigMgr->GetOption<std::string>("PBC.BaseUrl", "", kSilent);
    std::string apiKey      = sConfigMgr->GetOption<std::string>("PBC.ApiKey", "", kSilent);
    std::string model       = sConfigMgr->GetOption<std::string>("PBC.Model", "", kSilent);
    int maxResponseTokens   = sConfigMgr->GetOption<int>("PBC.MaxResponseLength", 0, kSilent);
    double temperature      = std::round(static_cast<double>(sConfigMgr->GetOption<float>("PBC.Temperature", 1.0f, kSilent)) * 100.0) / 100.0;
    std::string extraParams = sConfigMgr->GetOption<std::string>("PBC.ModelExtraParameters", "", kSilent);
    int timeoutSec          = sConfigMgr->GetOption<int>("PBC.RequestTimeoutSec", 30, kSilent);

    PBC_APIConfig defaultCfg;
    if (!PBC_BuildLegacyConnection(apiType, baseUrl, apiKey, model,
                                   maxResponseTokens, temperature, extraParams, timeoutSec, defaultCfg))
    {
        // No legacy config present at all
        return false;
    }

    PBC_Log(PBC_LogLevel::PBC_WARNING,
             "Using legacy API connection parameters (PBC.BaseUrl, PBC.Model, etc.) instead of "
             "connection files. These parameters are deprecated — migrate to JSONC connection files "
             "(see PBC.DefaultConnection and the connections/ directory).");

    g_PBC_Connections["default"] = std::move(defaultCfg);

    // Alt model — synthesize condensation and/or relationship connections
    bool useAltCondense     = sConfigMgr->GetOption<bool>("PBC.UseAltModelForCondensation", false, kSilent);
    bool useAltRelationship = sConfigMgr->GetOption<bool>("PBC.UseAltModelForRelationshipUpdate", false, kSilent);

    if (useAltCondense || useAltRelationship)
    {
        std::string altApiType     = sConfigMgr->GetOption<std::string>("PBC.AltModelAPIType", "openai", kSilent);
        std::string altBaseUrl     = sConfigMgr->GetOption<std::string>("PBC.AltModelBaseUrl", "", kSilent);
        std::string altApiKey      = sConfigMgr->GetOption<std::string>("PBC.AltModelApiKey", "", kSilent);
        std::string altModel       = sConfigMgr->GetOption<std::string>("PBC.AltModel", "", kSilent);
        int altMaxResponseTokens   = sConfigMgr->GetOption<int>("PBC.AltModelMaxResponseLength", 0, kSilent);
        double altTemperature      = std::round(static_cast<double>(sConfigMgr->GetOption<float>("PBC.AltModelTemperature", 1.0f, kSilent)) * 100.0) / 100.0;
        std::string altExtraParams = sConfigMgr->GetOption<std::string>("PBC.AltModelModelExtraParameters", "", kSilent);
        int altTimeoutSec          = sConfigMgr->GetOption<int>("PBC.AltModelRequestTimeoutSec", 30, kSilent);

        PBC_APIConfig altCfg;
        if (PBC_BuildLegacyConnection(altApiType, altBaseUrl, altApiKey, altModel,
                                      altMaxResponseTokens, altTemperature, altExtraParams, altTimeoutSec, altCfg))
        {
            if (useAltCondense)
                g_PBC_Connections["condensation"] = altCfg;
            if (useAltRelationship)
                g_PBC_Connections["relationship"] = altCfg;
        }
    }

    return true;
}

// Loads connections from the four PBC.*Connection config paths into the
// registry. Returns true if the "default" connection was loaded successfully.
static bool PBC_LoadConnections()
{
    std::lock_guard<std::mutex> lock(g_PBC_ConnectionsMutex);
    g_PBC_Connections.clear();

    std::string defaultPath     = sConfigMgr->GetOption<std::string>("PBC.DefaultConnection", "");
    std::string utilityPath     = sConfigMgr->GetOption<std::string>("PBC.UtilityConnection", "");
    std::string condensationPath = sConfigMgr->GetOption<std::string>("PBC.CondensationConnection", "");
    std::string relationshipPath = sConfigMgr->GetOption<std::string>("PBC.RelationshipUpdateConnection", "");

    // If DefaultConnection is empty, try the legacy fallback.
    if (defaultPath.empty())
    {
        if (PBC_LoadLegacyConnections())
            return true;

        PBC_Log(PBC_LogLevel::PBC_ERROR,
                 "PBC.DefaultConnection is not set and no legacy API parameters were found. "
                 "This is a required setting when the module is enabled.");
        return false;
    }

    // Cache: resolved path → loaded config, so a file referenced by multiple
    // slots is read and parsed only once.
    std::unordered_map<std::string, PBC_APIConfig> cache;

    auto loadSlot = [&](const std::string& name, const std::string& path) -> bool
    {
        if (path.empty())
            return false; // empty → falls back to default

        auto cacheIt = cache.find(path);
        if (cacheIt != cache.end())
        {
            g_PBC_Connections[name] = cacheIt->second;
            return true;
        }

        PBC_APIConfig cfg;
        if (!PBC_LoadConnectionFile(path, cfg))
            return false;

        cache[path] = cfg;
        g_PBC_Connections[name] = cfg;
        return true;
    };

    // Default is required.
    if (!loadSlot("default", defaultPath))
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Failed to load default connection from '{}'.", defaultPath);
        return false;
    }

    // Task-specific slots (optional — fall back to default if empty or fail).
    if (!loadSlot("utility", utilityPath))
        g_PBC_Connections["utility"] = g_PBC_Connections["default"];

    if (!loadSlot("condensation", condensationPath))
        g_PBC_Connections["condensation"] = g_PBC_Connections["default"];

    if (!loadSlot("relationship", relationshipPath))
        g_PBC_Connections["relationship"] = g_PBC_Connections["default"];

    return true;
}

const PBC_APIConfig* PBC_GetConnection(const std::string& name)
{
    std::lock_guard<std::mutex> lock(g_PBC_ConnectionsMutex);

    auto it = g_PBC_Connections.find(name);
    if (it != g_PBC_Connections.end())
        return &it->second;

    // Fall back to default
    it = g_PBC_Connections.find("default");
    if (it != g_PBC_Connections.end())
        return &it->second;

    return nullptr;
}

void PBC_LoadConfig(bool /*isStartup*/)
{
    g_PBC_Enable              = sConfigMgr->GetOption<bool>("PBC.Enable", true);
    g_PBC_DebugEnabled        = sConfigMgr->GetOption<bool>("PBC.DebugEnabled", false);
    g_PBC_DebugShowFullRequest = sConfigMgr->GetOption<bool>("PBC.DebugShowFullRequest", false);
    g_PBC_DisplayNarratorEvents = sConfigMgr->GetOption<bool>("PBC.DisplayNarratorEvents", true);

    g_PBC_MaxHistoryCtx              = sConfigMgr->GetOption<uint32_t>("PBC.MaxHistoryCtx", 0);
    g_PBC_MaxMemoriesCtx             = sConfigMgr->GetOption<uint32_t>("PBC.MaxMemoriesCtx", 8192);

    g_PBC_PromptsPath = sConfigMgr->GetOption<std::string>("PBC.PromptsPath",
                                    "../../../modules/mod-pbc/prompts");
    g_PBC_CharacterCardsPath  = sConfigMgr->GetOption<std::string>("PBC.CharacterCardsPath",
                                    "../../../modules/mod-pbc/characters");

    g_PBC_ReplyChanceWhisper   = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceWhisper", 100);
    g_PBC_ReplyChanceMention   = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceMention", 100);
    g_PBC_ReplyChanceMessage   = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceMessage", 100);
    g_PBC_RollPenaltyOnAnswer  = sConfigMgr->GetOption<uint32_t>("PBC.RollPenaltyOnAnswer", 45);
    g_PBC_ReplyChanceItem     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceItem", 5);
    g_PBC_ReplyChanceDuel     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceDuel", 5);
    g_PBC_ReplyChanceLevelUp  = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceLevelUp", 5);
    g_PBC_ReplyChanceHardCombat    = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceHardCombat", 25);
    g_PBC_ReplyChanceQuestCompleted = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestCompleted", 20);
    g_PBC_ReplyChanceQuestTaken     = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestTaken", 10);
    g_PBC_ReplyChanceLocationChanged = sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceLocationChanged", 15);

    g_PBC_LocationChangeDebounceCycles = sConfigMgr->GetOption<uint32_t>("PBC.LocationChangeDebounceCycles", 5);
    g_PBC_CombatEndDebounceCycles      = sConfigMgr->GetOption<uint32_t>("PBC.CombatEndDebounceCycles", 5);

    std::string blacklistStr = sConfigMgr->GetOption<std::string>("PBC.Blacklist", "");
    g_PBC_Blacklist = SplitByComma(blacklistStr);

    g_PBC_IgnoreAllAddonMessages = sConfigMgr->GetOption<bool>("PBC.IgnoreAllAddonMessages", true);

    g_PBC_HttpServerPort         = sConfigMgr->GetOption<int>("PBC.HttpServerPort", 0);
    g_PBC_HttpServerBind         = sConfigMgr->GetOption<std::string>("PBC.HttpServerBind", "127.0.0.1");
    g_PBC_HttpServerTimeout      = sConfigMgr->GetOption<int>("PBC.HttpServerTimeout", 15);
    g_PBC_HttpServerBaseUrl      = sConfigMgr->GetOption<std::string>("PBC.HttpServerBaseUrl", "http://127.0.0.1:8501");
    g_PBC_HttpServerPrivateKey   = sConfigMgr->GetOption<std::string>("PBC.HttpServerPrivateKey", "");
    g_PBC_HttpServerFrontendPath = sConfigMgr->GetOption<std::string>("PBC.HttpServerFrontendPath",
                                                                       "../../../modules/mod-pbc/frontend/dist");

    if (g_PBC_Enable)
    {
        bool configValid = true;

        // Load connections (from JSONC files or legacy fallback)
        if (!PBC_LoadConnections())
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Failed to load LLM API connections. Module DISABLED — fix your playerbots_characters.conf and reload with .chars reload.");
            g_PBC_Enable = false;
            return;
        }

        if (g_PBC_MaxHistoryCtx == 0)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "PBC.MaxHistoryCtx is not set (or is 0). This is a required setting when the module is enabled.");
            configValid = false;
        }

        if (!configValid)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Required configuration is missing or empty. Module DISABLED — fix your playerbots_characters.conf and reload with .chars reload.");
            g_PBC_Enable = false;
            return;
        }

        if (g_PBC_HttpServerPort > 0 && g_PBC_HttpServerPrivateKey.empty())
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "PBC.HttpServerPrivateKey is not set but PBC.HttpServerPort is {}. "
                      "The private key is required for the authorization layer when the HTTP server is enabled. "
                      "HTTP server will NOT start.", g_PBC_HttpServerPort);
        }
    }

    if (g_PBC_Enable && !PBC_LoadPrompts())
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Failed to load prompts. Module DISABLED — fix prompt path and reload with .chars reload.");
        g_PBC_Enable = false;
        return;
    }

    // Log connection summary
    {
        std::lock_guard<std::mutex> lock(g_PBC_ConnectionsMutex);
        auto logConn = [](const char* slotName)
        {
            auto it = g_PBC_Connections.find(slotName);
            if (it == g_PBC_Connections.end())
            {
                PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Connection '{}': (not configured)", slotName);
                return;
            }
            const auto& c = it->second;
            PBC_Log(PBC_LogLevel::PBC_DEFAULT,
                     "Connection '{}': apiType='{}' model='{}' baseUrl='{}' timeout={}s",
                     slotName, c.apiType, c.model, c.baseUrl, c.requestTimeoutSec);
        };
        logConn("default");
        logConn("utility");
        logConn("condensation");
        logConn("relationship");
    }

    PBC_Log(PBC_LogLevel::PBC_DEFAULT,
        "Config: Enable={} MaxHistoryCtx={} MaxMemoriesCtx={} "
        "Chances: Whisper={}% Mention={}% Message={}% RollPenalty={}% "
        "Item={}% Duel={}% LevelUp={}% HardCombat={}% QuestCompleted={}% QuestTaken={}%",
        g_PBC_Enable, g_PBC_MaxHistoryCtx, g_PBC_MaxMemoriesCtx,
        g_PBC_ReplyChanceWhisper, g_PBC_ReplyChanceMention,
        g_PBC_ReplyChanceMessage, g_PBC_RollPenaltyOnAnswer,
        g_PBC_ReplyChanceItem,
        g_PBC_ReplyChanceDuel, g_PBC_ReplyChanceLevelUp,
        g_PBC_ReplyChanceHardCombat, g_PBC_ReplyChanceQuestCompleted, g_PBC_ReplyChanceQuestTaken);

    PBC_Log(PBC_LogLevel::PBC_DEFAULT,
        "HTTP Server: Port={} Bind='{}' Timeout={}s BaseUrl='{}' PrivateKey={} FrontendPath='{}'",
        g_PBC_HttpServerPort, g_PBC_HttpServerBind, g_PBC_HttpServerTimeout, g_PBC_HttpServerBaseUrl,
        g_PBC_HttpServerPrivateKey.empty() ? "(not set)" : "(set)", g_PBC_HttpServerFrontendPath);
}

// Try to read a file into target. Returns true if the file exists and is non-empty.
static bool TryReadFile(const std::string& path, std::string& target)
{
    std::ifstream f(path);
    if (!f)
        return false;

    std::stringstream buf;
    buf << f.rdbuf();
    if (buf.str().empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "Prompt file is empty: {}", path);
        return false;
    }

    target = buf.str();
    PBC_NormalizeNewlines(target);
    return true;
}

// Load a prompt by name (e.g. "Main.system").  Checks in priority order:
//   1. <prompts_path>/<DBC.Locale>/<name>.custom.txt
//   2. <prompts_path>/<DBC.Locale>/<name>.default.txt
//   3. <prompts_path>/enUS/<name>.custom.txt
//   4. <prompts_path>/enUS/<name>.default.txt
//
// Sets isCustom=true if a .custom.txt file was loaded.
// Returns true on success, false if no prompt file could be loaded.
static bool PBC_GetPrompt(const std::string& promptName,
                          std::string& promptText,
                          bool& isCustom)
{
    namespace fs = std::filesystem;
    isCustom = false;

    // Directories to try: configured locale first, then enUS fallback
    fs::path base(g_PBC_PromptsPath);
    std::string localeName = GetNameByLocaleConstant(sWorld->GetDefaultDbcLocale());

    std::vector<fs::path> dirs;
    if (localeName != "enUS")
    {
        fs::path localeDir = base / localeName;
        if (fs::exists(localeDir) && fs::is_directory(localeDir))
            dirs.push_back(localeDir);
    }
    dirs.push_back(base / "enUS");

    // In each directory, try custom first, then default
    for (auto const& dir : dirs)
    {
        std::string customPath = (dir / (promptName + ".custom.txt")).string();
        if (TryReadFile(customPath, promptText))
        {
            isCustom = true;
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Loaded custom prompt '{}' from '{}' ({} chars)",
                     promptName, dir.string(), promptText.size());
            return true;
        }
    }

    for (auto const& dir : dirs)
    {
        std::string defaultPath = (dir / (promptName + ".default.txt")).string();
        if (TryReadFile(defaultPath, promptText))
        {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Loaded default prompt '{}' from '{}' ({} chars)",
                     promptName, dir.string(), promptText.size());
            return true;
        }
    }

    PBC_Log(PBC_LogLevel::PBC_ERROR, "Prompt '{}' not found in '{}' or 'enUS' — module disabled.",
             promptName, localeName);
    return false;
}

bool PBC_LoadPrompts()
{
    // Verify enUS fallback directory exists
    namespace fs = std::filesystem;
    fs::path enusDir = fs::path(g_PBC_PromptsPath) / "enUS";
    if (!fs::exists(enusDir) || !fs::is_directory(enusDir))
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Fallback prompts directory not found: {}", enusDir.string());
        return false;
    }

    std::string localeName = GetNameByLocaleConstant(sWorld->GetDefaultDbcLocale());
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Loading prompts (DBC.Locale={})", localeName);

    struct PromptEntry { const char* name; std::string& target; };
    const PromptEntry prompts[] = {
        { "Main.system",                      g_PBC_SystemPrompt                   },
        { "Main.user",                        g_PBC_UserPrompt                     },
        { "Condensation.system",              g_PBC_CondensationSystemPrompt       },
        { "Condensation.user",                g_PBC_CondensationUserPrompt         },
        { "DefaultCharacterDescription",      g_PBC_DefaultCharacterDescription    },
        { "CharacterContext",                  g_PBC_CharacterContext               },
        { "QuestCompleted.system",            g_PBC_QuestCompletedSystemPrompt     },
        { "QuestCompleted.user",              g_PBC_QuestCompletedUserPrompt       },
        { "QuestTaken.system",                g_PBC_QuestTakenSystemPrompt         },
        { "QuestTaken.user",                  g_PBC_QuestTakenUserPrompt           },
        { "RelationshipUpdate.system",        g_PBC_RelationshipUpdateSystemPrompt },
        { "RelationshipUpdate.user",          g_PBC_RelationshipUpdateUserPrompt   },
        { "CombatEnded.system",               g_PBC_CombatEndedSystemPrompt        },
        { "CombatEnded.user",                 g_PBC_CombatEndedUserPrompt          },
    };

    bool allOk = true;
    int customCount = 0;

    for (auto const& entry : prompts)
    {
        bool isCustom = false;
        if (!PBC_GetPrompt(entry.name, entry.target, isCustom))
        {
            allOk = false;
        }
        else if (isCustom)
        {
            ++customCount;
        }
    }

    if (!allOk)
        return false;

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Loaded {} prompt(s) ({} custom)",
             static_cast<int>(sizeof(prompts) / sizeof(prompts[0])), customCount);
    return true;
}


void PBC_LoadCharacterCards()
{
    g_PBC_CharacterCards.clear();

    std::filesystem::path dir(g_PBC_CharacterCardsPath);
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "Character cards directory not found: {}", g_PBC_CharacterCardsPath);
        return;
    }

    int loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();

        std::string filename = path.filename().string();
        const std::string cardSuffix = ".card.txt";
        if (filename.size() <= cardSuffix.size() ||
            filename.substr(filename.size() - cardSuffix.size()) != cardSuffix)
            continue;

        std::string name = filename.substr(0, filename.size() - cardSuffix.size());

        std::ifstream f(path);
        if (!f) { PBC_Log(PBC_LogLevel::PBC_WARNING, "Cannot open card file: {}", path.string()); continue; }

        std::stringstream buf;
        buf << f.rdbuf();
        std::string cardText = buf.str();
        PBC_NormalizeNewlines(cardText);
        g_PBC_CharacterCards[name] = std::move(cardText);
        ++loaded;

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "Loaded card '{}' ({} chars)", name, g_PBC_CharacterCards[name].size());
    }

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Loaded {} character card(s) from '{}'", loaded, g_PBC_CharacterCardsPath);
}

uint32_t PBC_GetEffectiveChance(uint64_t botGuid, uint32_t baseChance)
{
    std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
    auto it = g_PBC_RollChanceModifiers.find(botGuid);
    if (it == g_PBC_RollChanceModifiers.end())
        return baseChance;
    int32_t effective = static_cast<int32_t>(baseChance) + it->second;
    return static_cast<uint32_t>(std::max(0, std::min(100, effective)));
}


