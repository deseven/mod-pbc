#ifndef MOD_PBC_HTTP_H
#define MOD_PBC_HTTP_H

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

struct PBC_HistoryEntry;

// Outcome of a PBC_HttpClient::Post call, for callers that decide on retries.
struct PBC_HttpStatus
{
    int    code          = 0;    // HTTP status code, 0 if no response was received
    double retryAfterSec = -1.0; // Retry-After header in seconds, -1 if absent
};

// Thin wrapper around cpp-httplib for synchronous HTTP/HTTPS POST requests.
// Supports custom auth headers (Bearer token, x-api-key, etc.).
class PBC_HttpClient
{
public:
    PBC_HttpClient();

    // POST jsonData to url with optional extra headers.
    // extraHeaders is a list of name/value pairs added to the request.
    // If status is given, it receives the HTTP status code and Retry-After.
    // Returns the raw response body, or "" on error.
    std::string Post(const std::string& url,
                     const std::string& jsonData,
                     const std::vector<std::pair<std::string, std::string>>& extraHeaders = {},
                     PBC_HttpStatus* status = nullptr);

    void SetTimeoutSeconds(int seconds);

private:
    int m_timeoutSec;
};

// ---------------------------------------------------------------------------
// HTTP/WS server — inbound request handling.
//
// Start() binds to the given address:port and spawns a listener thread that
// serves both plain HTTP and WebSocket connections.  Stop() shuts the server
// down and joins the thread.  All httplib internals are hidden inside the
// .cpp file so the header stays lightweight.
// ---------------------------------------------------------------------------

// Start the HTTP/WS server.  Returns true if binding succeeded.
// On failure the server is not running — the caller should treat it as disabled.
bool PBC_HttpServerStart(const std::string& bindAddr, int port, int timeoutSec);

// Stop the server (if running) and join the listener thread.
void PBC_HttpServerStop();

// Returns true if the server is currently listening.
bool PBC_HttpServerIsRunning();

// ---------------------------------------------------------------------------
// OTP generation (called from the main thread by .chars web command).
//
// Generates a random 6-digit one-time password for the given account ID.
// The OTP is valid for 2 minutes and can be exchanged for a bearer token
// via the /api/token endpoint.  Returns the OTP string, or "" on error.
// ---------------------------------------------------------------------------
std::string PBC_HttpServerGenerateOTP(uint32_t accountId);

// ---------------------------------------------------------------------------
// WebSocket event notifications (thread-safe, no-op when server is not running)
//
// Called from the event thread or main thread to push real-time notifications
// to WS clients that are subscribed to the account that owns the character.
// ---------------------------------------------------------------------------

// Notify WS clients subscribed to the same account as botGuid of a simple
// event type ("thinks", "relationship", "memory"). The JSON payload includes
// the guid so the frontend can filter by character.
void PBC_WsNotify(uint64_t botGuid, const std::string& eventType);

// Notify WS clients subscribed to the same account as botGuid of a history
// event.  Sends id, text, type, message, author_guid, and author_name so the
// frontend can render the message correctly without an HTTP reload.
void PBC_WsNotifyHistory(uint64_t botGuid, const PBC_HistoryEntry& entry);

// Send an immediate preview of a pending history line (id=0) to WS clients.
// Used to stream character replies in real-time before they are persisted.
// When the real PBC_WsNotifyHistory arrives later with the proper id, the
// frontend replaces the preview entry.
void PBC_WsNotifyHistoryPreview(uint64_t botGuid, const std::string& text);

// Notify all WS clients subscribed to a specific account of an account-level
// event ("online", "offline", "party"). relatedGuid is the character GUID that
// triggered the event (set to 0 for events like "party" that have no single guid).
void PBC_WsNotifyAccount(uint32_t accountId, const std::string& eventType, uint64_t relatedGuid = 0);

// Notify WS clients subscribed to the same account as botGuid that a regen
// has occurred.  Sends the character guid and an array of objects
// ({id, text}) containing the affected history IDs and their new rendered
// text, so the frontend can replace the messages in place without a reload.
void PBC_WsNotifyRegen(uint64_t botGuid, const std::vector<uint64_t>& messageIds);

// Broadcast a "shutdown" event to every connected WS client.
// Called from PBC_HttpServerStop before the server is stopped.
void PBC_WsBroadcastShutdown();

#endif // MOD_PBC_HTTP_H
