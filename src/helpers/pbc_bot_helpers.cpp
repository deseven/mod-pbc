#include "pbc_bot_helpers.h"
#include "pbc_utils.h"
#include "Player.h"
#include "WorldSession.h"

#ifdef PBC_COA_PLAYERBOTS
#include "BotMgr.h"
#include <mutex>
#include <unordered_set>

namespace
{
// Login and logout run on the world thread. Dashboard workers may read this set.
std::mutex companionSessionsMutex;
std::unordered_set<WorldSession const*> companionSessions;

bool IsCoaCompanionSession(WorldSession const* session)
{
    std::lock_guard lock(companionSessionsMutex);
    return companionSessions.contains(session);
}
}

void PBC_RegisterCoaCompanion(Player* player)
{
    WorldSession* session = player->GetSession();
    if (!session || !session->IsSocketClosed() ||
        BotMgr::instance()->FindBotPlayer(player->GetGUID().GetCounter()) != player)
        return;

    std::lock_guard lock(companionSessionsMutex);
    companionSessions.insert(session);
}

void PBC_ForgetCoaCompanion(Player* player)
{
    std::lock_guard lock(companionSessionsMutex);
    companionSessions.erase(player->GetSession());
}

bool PBC_IsCoaCompanion(Player* player)
{
    return player && IsCoaCompanionSession(player->GetSession());
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
    return IsCoaCompanionSession(sess);
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
