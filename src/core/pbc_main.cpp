#include "pbc_config.h"
#include "pbc_world.h"
#include "pbc_commands.h"
#include "pbc_log.h"
#include "pbc_player_scripts.h"
#include "pbc_group_scripts.h"
#include "pbc_quest_scripts.h"

void Addmod_pbcScripts()
{
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Registering mod-pbc scripts.");

    new PBC_WorldScript();
    new PBC_PlayerEvents();
    new PBC_GroupEvents();
    new PBC_AllCreatureQuestScript();
    new PBC_AllGameObjectQuestScript();
    new PBC_AllItemQuestScript();
    new PBC_CommandScript();
}

// Compatibility wrapper for checkouts that still live in the old
// mod-playerbots-characters directory: the AzerothCore build system generates
// the loader call from the module directory name, so old names must keep
// working. New checkouts use the mod-pbc directory and call Addmod_pbcScripts.
void Addmod_playerbots_charactersScripts()
{
    Addmod_pbcScripts();
}
