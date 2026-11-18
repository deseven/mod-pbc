#include "pbc_bot_helpers.h"
#include "pbc_utils.h"
#include "Player.h"
#include "WorldSession.h"

#ifdef PBC_COA_PLAYERBOTS
#include "BotMgr.h"

bool PBC_IsCoaCompanion(Player* player)
{
    return player && BotMgr::instance()->FindBotPlayer(player->GetGUID().GetCounter()) == player;
}
#endif

namespace
{
template <typename Session>
bool IsPlayerbotSession(const Session& session)
{
    if constexpr (requires { session.IsHeadlessBot(); })
        return session.IsHeadlessBot();
    else
        return session.IsHeadless();
}
} // namespace

bool PBC_IsBotSession(WorldSession* sess)
{
    if (!PBC_PTR_VALID(sess))
        return false;
    if (IsPlayerbotSession(*sess))
        return true;
#ifdef PBC_COA_PLAYERBOTS
    // CoA companions can opt out of the core bot marker.
    return sess->IsSocketClosed() && PBC_IsCoaCompanion(sess->GetPlayer());
#else
    return false;
#endif
}

bool PBC_IsRealPlayerSession(WorldSession* sess)
{
    return PBC_PTR_VALID(sess) && !PBC_IsBotSession(sess);
}

bool PBC_IsBot(Player* player)
{
    return PBC_PTR_VALID(player) && PBC_IsBotSession(player->GetSession());
}
