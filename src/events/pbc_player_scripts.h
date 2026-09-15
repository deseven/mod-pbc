#ifndef MOD_PBC_PLAYER_SCRIPTS_H
#define MOD_PBC_PLAYER_SCRIPTS_H

#include "ScriptMgr.h"
#include "SharedDefines.h"
#include <string>

class Player;
class Group;
class Guild;
class Item;
class Creature;
class Quest;
class Roll;

// ---------------------------------------------------------------------------
// Listens to player events and feeds them to the character system.
// ---------------------------------------------------------------------------
class PBC_PlayerEvents : public PlayerScript
{
public:
    PBC_PlayerEvents();

    // Chat events
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                            std::string& msg, Player* receiver) override;
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                            std::string& msg) override;
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                            std::string& msg, Group* group) override;
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/,
                            std::string& msg, Guild* guild) override;

    // Login/logout — used for WS "online"/"offline" events
    void OnPlayerLogin(Player* player) override;
    void OnPlayerLogout(Player* player) override;

    // World events bots may react to
    void OnPlayerLootItem(Player* player, Item* item, uint32 count, ObjectGuid lootguid) override;
    void OnPlayerQuestRewardItem(Player* player, Item* item, uint32 count) override;
    void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 count, RollVote voteType, Roll* roll) override;
    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override;
    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override;
    void OnPlayerCreatureKill(Player* killer, Creature* killed) override;
    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override;
    void OnPlayerJustDied(Player* player) override;
};

// Bitmask of item classes that trigger loot events.
#define PBC_LOOT_EVENT_ITEM_CLASSES  ((1u << ITEM_CLASS_WEAPON) | (1u << ITEM_CLASS_ARMOR))

#endif // MOD_PBC_PLAYER_SCRIPTS_H
