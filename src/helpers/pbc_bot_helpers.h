#ifndef MOD_PBC_BOT_HELPERS_H
#define MOD_PBC_BOT_HELPERS_H

class Player;
class WorldSession;

// ---------------------------------------------------------------------------
// Bot detection helpers  (main-thread only)
//
// The Playerbots and CoA cores expose their bot-session marker through
// different WorldSession methods. Keep those differences in the implementation.
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

#ifdef PBC_COA_PLAYERBOTS
// True if mod-coa-playerbots has this live character in its session roster.
bool PBC_IsCoaCompanion(Player* player);
#endif

#endif // MOD_PBC_BOT_HELPERS_H
