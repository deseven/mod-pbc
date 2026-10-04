#ifndef MOD_PBC_BOT_HELPERS_H
#define MOD_PBC_BOT_HELPERS_H

class Player;
class WorldSession;

// ---------------------------------------------------------------------------
// Bot detection helpers  (main-thread only)
//
// Single source of truth for "is this character controlled by mod-playerbots?".
//
// Currently backed by WorldSession::IsHeadless(): mod-playerbots is the only
// code in this codebase that creates socket-less sessions, so a headless
// session is a bot session.  If that assumption ever changes (for example,
// switching to GET_PLAYERBOT_AI), update the implementation in
// pbc_bot_helpers.cpp — every call site goes through these helpers.
//
// All helpers are null-safe: an invalid/null session or player is treated as
// "not a bot" (and therefore "not a real player" either).
// ---------------------------------------------------------------------------

// True if 'sess' is a playerbot session.
bool PBC_IsBotSession(WorldSession* sess);

// True if 'sess' is a real (non-bot) player session.
bool PBC_IsRealPlayerSession(WorldSession* sess);

// True if 'player' is a playerbot.
bool PBC_IsBot(Player* player);

#endif // MOD_PBC_BOT_HELPERS_H
