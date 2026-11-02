#include "pbc_bot_helpers.h"
#include "pbc_utils.h"
#include "Player.h"
#include "WorldSession.h"

bool PBC_IsBotSession(WorldSession* sess)
{
    return PBC_PTR_VALID(sess) && sess->IsHeadless();
}

bool PBC_IsRealPlayerSession(WorldSession* sess)
{
    return PBC_PTR_VALID(sess) && !sess->IsHeadless();
}

bool PBC_IsBot(Player* player)
{
    return PBC_PTR_VALID(player) && PBC_IsBotSession(player->GetSession());
}
