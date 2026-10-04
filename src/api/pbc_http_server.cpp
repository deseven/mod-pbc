#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "pbc_http.h"
#include "pbc_http_auth.h"
#include "pbc_http_handlers.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_log.h"
#include "pbc_utils.h"

#define httplib pbc_httplib
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <fstream>
#include <regex>

#include "CharacterCache.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "WorldSession.h"

#include "pbc_json.h"

using json = pbc_json;

// ===========================================================================
// Server globals
// ===========================================================================

static std::unique_ptr<httplib::Server>  s_httpServer;
static std::unique_ptr<std::thread>      s_httpThread;
static std::atomic<bool>                 s_httpRunning{false};
static std::atomic<bool>                 s_httpShuttingDown{false};

// ===========================================================================
// WS subscription state — account-based
// ===========================================================================

static std::mutex s_wsSubMutex;
// ws connection → account ID it subscribed to (0 = not yet subscribed)
static std::unordered_map<httplib::ws::WebSocket*, uint32_t> s_wsConnectionSubs;
// account ID → set of ws connections subscribed to this account
static std::unordered_map<uint32_t, std::unordered_set<httplib::ws::WebSocket*>> s_wsAccountSubs;

// Subscribe a WS connection to an account (no GUID needed).
static void WsSubscribe(httplib::ws::WebSocket* ws, uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(s_wsSubMutex);

    // Unsubscribe from any previous account first
    auto it = s_wsConnectionSubs.find(ws);
    if (it != s_wsConnectionSubs.end() && it->second != 0)
    {
        s_wsAccountSubs[it->second].erase(ws);
        if (s_wsAccountSubs[it->second].empty())
            s_wsAccountSubs.erase(it->second);
    }

    s_wsConnectionSubs[ws] = accountId;
    s_wsAccountSubs[accountId].insert(ws);
}

// Remove all subscription state for a WS connection.
static void WsUnsubscribe(httplib::ws::WebSocket* ws)
{
    std::lock_guard<std::mutex> lock(s_wsSubMutex);

    auto it = s_wsConnectionSubs.find(ws);
    if (it != s_wsConnectionSubs.end())
    {
        if (it->second != 0)
        {
            s_wsAccountSubs[it->second].erase(ws);
            if (s_wsAccountSubs[it->second].empty())
                s_wsAccountSubs.erase(it->second);
        }
        s_wsConnectionSubs.erase(it);
    }
}

// Look up the account ID for a character GUID.
static uint32_t GetAccountIdByGuid(uint64_t guid)
{
    uint32_t accountId = sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(guid));
    return accountId;
}

// Send a raw JSON string to all WS subscribers of a given account.
static void WsSendToAccount(uint32_t accountId, const std::string& jsonMessage)
{
    std::lock_guard<std::mutex> lock(s_wsSubMutex);

    auto it = s_wsAccountSubs.find(accountId);
    if (it == s_wsAccountSubs.end())
        return;

    std::vector<httplib::ws::WebSocket*> toRemove;

    for (auto* ws : it->second)
    {
        if (!ws->send(jsonMessage))
            toRemove.push_back(ws);
    }

    for (auto* ws : toRemove)
    {
        it->second.erase(ws);
        s_wsConnectionSubs.erase(ws);
    }

    if (it->second.empty())
        s_wsAccountSubs.erase(it);
}

// Send a raw JSON string to all WS subscribers of the account that owns botGuid.
static void WsSendToGuidOwner(uint64_t botGuid, const std::string& jsonMessage)
{
    uint32_t accountId = GetAccountIdByGuid(botGuid);
    if (accountId == 0)
        return;
    WsSendToAccount(accountId, jsonMessage);
}

// Broadcast a raw JSON string to every connected WS client.
static void WsBroadcast(const std::string& jsonMessage)
{
    std::lock_guard<std::mutex> lock(s_wsSubMutex);

    std::vector<httplib::ws::WebSocket*> toRemove;

    for (auto& [ws, accountId] : s_wsConnectionSubs)
    {
        if (!ws->send(jsonMessage))
            toRemove.push_back(ws);
    }

    for (auto* ws : toRemove)
    {
        auto it = s_wsConnectionSubs.find(ws);
        if (it != s_wsConnectionSubs.end())
        {
            if (it->second != 0)
            {
                s_wsAccountSubs[it->second].erase(ws);
                if (s_wsAccountSubs[it->second].empty())
                    s_wsAccountSubs.erase(it->second);
            }
            s_wsConnectionSubs.erase(it);
        }
    }
}

// ===========================================================================
// Extract token from Sec-WebSocket-Protocol header (browser WebSocket auth).
// ===========================================================================

static std::string ExtractWebSocketToken(const httplib::Request& req)
{
    std::string proto = req.get_header_value("Sec-WebSocket-Protocol");
    if (proto.empty())
        return "";

    std::vector<std::string> parts;
    std::stringstream ss(proto);
    std::string part;
    while (std::getline(ss, part, ','))
    {
        size_t start = part.find_first_not_of(" \t");
        size_t end = part.find_last_not_of(" \t");
        if (start != std::string::npos)
            parts.push_back(part.substr(start, end - start + 1));
    }

    if (parts.size() >= 2 && parts[0] == "access_token")
        return parts[1];

    return "";
}

// ===========================================================================
// Public WS notification functions
// ===========================================================================

void PBC_WsNotify(uint64_t botGuid, const std::string& eventType)
{
    if (!s_httpRunning.load())
        return;

    json j;
    j["event"] = eventType;
    j["guid"] = botGuid;
    WsSendToGuidOwner(botGuid, j.dump());
}

void PBC_WsNotifyHistory(uint64_t botGuid, const PBC_HistoryEntry& entry)
{
    if (!s_httpRunning.load())
        return;

    std::string text = PBC_RenderHistoryLine(entry, botGuid);
    std::string authorName = PBC_GetCharacterName(entry.authorGuid);

    json j;
    j["event"] = "history";
    j["guid"] = botGuid;
    j["message"] = {
        {"id", entry.id},
        {"text", text},
        {"type", entry.type},
        {"message", entry.message},
        {"author_guid", entry.authorGuid},
        {"author_name", authorName}
    };
    WsSendToGuidOwner(botGuid, j.dump());
}

void PBC_WsNotifyHistoryPreview(uint64_t botGuid, const std::string& text)
{
    if (!s_httpRunning.load())
        return;

    json j;
    j["event"] = "history";
    j["guid"] = botGuid;
    j["message"] = {{"id", 0}, {"text", text}};
    WsSendToGuidOwner(botGuid, j.dump());
}

void PBC_WsNotifyAccount(uint32_t accountId, const std::string& eventType, uint64_t relatedGuid)
{
    if (!s_httpRunning.load())
        return;

    json j;
    j["event"] = eventType;
    if (relatedGuid != 0)
        j["guid"] = relatedGuid;
    WsSendToAccount(accountId, j.dump());
}

void PBC_WsNotifyRegen(uint64_t botGuid, const std::vector<uint64_t>& messageIds)
{
    if (!s_httpRunning.load())
        return;
    if (messageIds.empty())
        return;

    // Build the array of full message objects for every affected message,
    // rendered from the bot's perspective so the frontend can replace them
    // in place.  The structure mirrors the `history` event's `message` field
    // so the frontend can swap the affected entries directly.
    json messages = json::array();
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        for (uint64_t id : messageIds)
        {
            auto it = g_PBC_History.find(id);
            if (it == g_PBC_History.end())
                continue;
            const PBC_HistoryEntry& entry = it->second;
            messages.push_back({
                {"id", id},
                {"text", PBC_RenderHistoryLine(entry, botGuid)},
                {"type", entry.type},
                {"message", entry.message},
                {"author_guid", entry.authorGuid},
                {"author_name", PBC_GetCharacterName(entry.authorGuid)}
            });
        }
    }

    if (messages.empty())
        return;

    json j;
    j["event"]    = "regen";
    j["guid"]     = botGuid;
    j["messages"] = messages;
    WsSendToGuidOwner(botGuid, j.dump());
}

void PBC_WsBroadcastShutdown()
{
    if (!s_httpRunning.load())
        return;

    json j;
    j["event"] = "shutdown";
    WsBroadcast(j.dump());
}

// ===========================================================================
// Server lifecycle
// ===========================================================================

bool PBC_HttpServerStart(const std::string& bindAddr, int port, int timeoutSec)
{
    if (port <= 0)
        return false;

    s_httpShuttingDown.store(false);

    // Private key is required for the authorization layer
    if (g_PBC_HttpServerPrivateKey.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR,
                  "HTTP server not started: PBC.HttpServerPrivateKey is not set. "
                  "A private key is required for the authorization layer.");
        return false;
    }

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    PBC_Log(PBC_LogLevel::PBC_ERROR,
              "HTTP server not started: OpenSSL is not compiled in. "
              "The authorization layer requires OpenSSL for token encryption.");
    return false;
#else
    try
    {
        auto svr = std::make_unique<httplib::Server>();

        // -------------------------------------------------------------------
        // Post-routing handler: prevent caching of API responses
        //
        // API responses reflect live, per-account state and must never be
        // cached by the browser or any intermediary proxy.  Static frontend
        // assets are intentionally left cacheable.
        // -------------------------------------------------------------------
        svr->set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
            if (req.path == "/api" || req.path.rfind("/api/", 0) == 0)
            {
                res.set_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
                res.set_header("Pragma", "no-cache");
                res.set_header("Expires", "0");
            }
        });

        // -------------------------------------------------------------------
        // Pre-routing handler: WebSocket authorization
        // -------------------------------------------------------------------
        svr->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res)
            -> httplib::Server::HandlerResponse
        {
            if (req.path == "/ws" && req.get_header_value("Upgrade") == "websocket")
            {
                std::string token = ExtractWebSocketToken(req);
                if (token.empty())
                {
                    res.status = 401;
                    res.set_content("{\"error\":\"Authentication required. "
                                    "Use Sec-WebSocket-Protocol: access_token, <token>\"}",
                                    "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }

                uint32_t accountId = PBC_ValidateToken(token);
                if (accountId == 0)
                {
                    res.status = 401;
                    res.set_content("{\"error\":\"Invalid or expired token\"}", "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }

                PBC_Log(PBC_LogLevel::PBC_DEBUG, "WS: authenticated account ID={}", accountId);
            }

            return httplib::Server::HandlerResponse::Unhandled;
        });

        // -------------------------------------------------------------------
        // Frontend static file serving
        // -------------------------------------------------------------------
        bool frontendServing = false;
        if (!g_PBC_HttpServerFrontendPath.empty())
        {
            std::string frontendPath = g_PBC_HttpServerFrontendPath;
            std::error_code ec;
            auto canonicalPath = std::filesystem::canonical(PBC_PathFromUtf8(frontendPath), ec);
            if (!ec && std::filesystem::is_directory(canonicalPath))
            {
                std::string canonicalStr = PBC_PathToUtf8(canonicalPath);
                svr->set_mount_point("/", canonicalStr);

                svr->set_error_handler([canonicalStr](const httplib::Request& req, httplib::Response& res) {
                    if (res.status != 404 || req.method != "GET")
                        return;

                    if (req.path.size() >= 4 && req.path.substr(0, 4) == "/api")
                        return;
                    if (req.path.size() >= 3 && req.path.substr(0, 3) == "/ws")
                        return;

                    std::string indexPath = canonicalStr + "/index.html";
                    std::error_code ec2;
                    auto resolvedIndex = std::filesystem::canonical(PBC_PathFromUtf8(indexPath), ec2);
                    if (ec2 || !std::filesystem::is_regular_file(resolvedIndex))
                        return;

                    std::string resolvedStr = PBC_PathToUtf8(resolvedIndex);
                    if (resolvedStr.size() < canonicalStr.size() ||
                        resolvedStr.substr(0, canonicalStr.size()) != canonicalStr)
                        return;

                    std::ifstream ifs(resolvedIndex, std::ios::binary);
                    if (ifs.is_open())
                    {
                        std::string content((std::istreambuf_iterator<char>(ifs)),
                                            std::istreambuf_iterator<char>());
                        res.status = 200;
                        res.set_content(content, "text/html");
                    }
                });

                frontendServing = true;
                PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Frontend serving enabled from '{}' (canonical: '{}')",
                         frontendPath, canonicalStr);
            }
            else
            {
                PBC_Log(PBC_LogLevel::PBC_WARNING,
                         "Frontend path '{}' does not exist or is not a directory. "
                         "Frontend serving disabled.", frontendPath);
            }
        }

        if (!frontendServing)
        {
            // Simple health check
            svr->Get("/", [](const httplib::Request& req, httplib::Response& res) {
                HandleGetRoot(req, res);
            });
        }

        // -------------------------------------------------------------------
        // Route registration
        // -------------------------------------------------------------------

        // GET /api/token — no auth required
        svr->Get("/api/token", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            HandleGetToken(req, res);
        });

        // GET /api/account
        svr->Get("/api/account", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetAccount(req, res, authInfo);
        });

        // GET /api/party
        svr->Get("/api/party", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetParty(req, res, authInfo);
        });

        // GET /api/config
        svr->Get("/api/config", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetConfig(req, res, authInfo);
        });

        // GET /api/char/:guid/card
        svr->Get("/api/char/:guid/card", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetCharCard(req, res, authInfo);
        });

        // GET /api/char/:guid/context
        svr->Get("/api/char/:guid/context", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetCharContext(req, res, authInfo);
        });

        // GET /api/char/:guid/history
        svr->Get("/api/char/:guid/history", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetCharHistory(req, res, authInfo);
        });

        // POST /api/char/:guid/history
        svr->Post("/api/char/:guid/history", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostCharHistory(req, res, authInfo);
        });

        // DELETE /api/char/:guid/history
        svr->Delete("/api/char/:guid/history", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleDeleteCharHistory(req, res, authInfo);
        });

        // GET /api/char/:guid/memory/count
        svr->Get("/api/char/:guid/memory/count", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetCharMemoryCount(req, res, authInfo);
        });

        // GET /api/char/:guid/memory
        svr->Get("/api/char/:guid/memory", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetCharMemory(req, res, authInfo);
        });

        // POST /api/char/:guid/memory/:id
        svr->Post("/api/char/:guid/memory/:id", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostCharMemory(req, res, authInfo);
        });

        // DELETE /api/char/:guid/memory/:id
        svr->Delete("/api/char/:guid/memory/:id", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleDeleteCharMemory(req, res, authInfo);
        });

        // GET /api/char/:guid/relationships
        svr->Get("/api/char/:guid/relationships", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetCharRelationships(req, res, authInfo);
        });

        // POST /api/char/:guid/relationships
        svr->Post("/api/char/:guid/relationships", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostCharRelationships(req, res, authInfo);
        });

        // DELETE /api/char/:guid/relationships
        svr->Delete("/api/char/:guid/relationships", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleDeleteCharRelationships(req, res, authInfo);
        });

        // GET /api/char/:guid/data
        svr->Get("/api/char/:guid/data", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetCharData(req, res, authInfo);
        });

        // POST /api/char/:guid/data
        svr->Post("/api/char/:guid/data", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostCharData(req, res, authInfo);
        });

        // GET /api/char/:guid/debug/request
        svr->Get("/api/char/:guid/debug/request", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandleGetCharDebugRequest(req, res, authInfo);
        });

        // POST /api/char/:guid/whisper
        svr->Post("/api/char/:guid/whisper", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostCharWhisper(req, res, authInfo);
        });

        // POST /api/char/:guid/narrate
        svr->Post("/api/char/:guid/narrate", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostCharNarrate(req, res, authInfo);
        });

        // POST /api/party/narrate
        svr->Post("/api/party/narrate", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostPartyNarrate(req, res, authInfo);
        });

        // POST /api/char/:guid/trigger
        svr->Post("/api/char/:guid/trigger", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostCharTrigger(req, res, authInfo);
        });

        // POST /api/party/message
        svr->Post("/api/party/message", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostPartyMessage(req, res, authInfo);
        });

        // POST /api/regen-last
        svr->Post("/api/regen-last", [](const httplib::Request& req, httplib::Response& res) {
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP: {} {} from {}", req.method, req.path, req.remote_addr);
            PBC_AuthInfo authInfo;
            if (!AuthenticateRequest(req, res, authInfo)) return;
            HandlePostRegenLast(req, res, authInfo);
        });

        // -------------------------------------------------------------------
        // WebSocket endpoint: /ws
        // -------------------------------------------------------------------
        svr->WebSocket("/ws",
            [](const httplib::Request& req, httplib::ws::WebSocket& ws) {
                std::string token = ExtractWebSocketToken(req);
                uint32_t accountId = PBC_ValidateToken(token);

                PBC_Log(PBC_LogLevel::PBC_DEBUG, "WS: new connection from {} (account ID={})",
                             req.remote_addr, accountId);

                // Auto-subscribe on connect so account-level events (online/offline/party)
                // are always delivered regardless of client state.
                WsSubscribe(&ws, accountId);

                ws.send(json({{"event", "connected"}}).dump());

                // Keep the connection alive — read loop just discards any messages
                // from the client (the server auto-subscribes, so no explicit commands
                // are needed).
                std::string msg;
                while (ws.read(msg) && !s_httpShuttingDown.load())
                {
                    // Client messages are ignored; subscription is handled server-side.
                }

                WsUnsubscribe(&ws);

                if (s_httpShuttingDown.load())
                    ws.close(httplib::ws::CloseStatus::GoingAway, "server shutting down");

                PBC_Log(PBC_LogLevel::PBC_DEBUG, "WS: connection closed (account ID={})", accountId);
            },
            [](const std::vector<std::string>& protocols) -> std::string {
                for (const auto& proto : protocols)
                {
                    if (proto == "access_token")
                        return "access_token";
                }
                return "";
            }
        );

        // Set read/write timeouts
        svr->set_read_timeout(timeoutSec);
        svr->set_write_timeout(timeoutSec);

        // Try to bind before spawning the thread
        if (!svr->bind_to_port(bindAddr.c_str(), port))
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR,
                      "HTTP server failed to bind to {}:{} — port may be in use or address invalid. "
                      "HTTP server disabled; the rest of the module continues normally.",
                      bindAddr, port);
            return false;
        }

        // Binding succeeded
        s_httpServer = std::move(svr);
        s_httpRunning.store(true);

        s_httpThread = std::make_unique<std::thread>([bindAddr, port]() {
            PBC_Log(PBC_LogLevel::PBC_DEFAULT, "HTTP server listening on {}:{}", bindAddr, port);
            s_httpServer->listen_after_bind();
            s_httpRunning.store(false);
            PBC_Log(PBC_LogLevel::PBC_DEFAULT, "HTTP server stopped.");
        });

        return true;
    }
    catch (const std::exception& ex)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR,
                  "HTTP server exception during startup: {}. HTTP server disabled; "
                  "the rest of the module continues normally.",
                  ex.what());
        s_httpServer.reset();
        s_httpRunning.store(false);
        return false;
    }
#endif // CPPHTTPLIB_OPENSSL_SUPPORT
}

void PBC_HttpServerStop()
{
    s_httpShuttingDown.store(true);

    // Broadcast shutdown event to all connected WS clients before stopping.
    PBC_WsBroadcastShutdown();

    if (s_httpServer)
        s_httpServer->stop();

    if (s_httpThread && s_httpThread->joinable())
        s_httpThread->detach();

    s_httpThread.reset();
    s_httpRunning.store(false);
}

bool PBC_HttpServerIsRunning()
{
    return s_httpRunning.load();
}
