#include "pbc_poll.h"
#include "pbc_config.h"
#include "pbc_utils.h"
#include "pbc_bot_helpers.h"
#include "pbc_locales.h"
#include "pbc_event_dispatch.h"
#include "pbc_group_helpers.h"
#include "pbc_scene_helpers.h"
#include "pbc_log.h"

#include "Player.h"
#include "Creature.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "GameTime.h"

#include <fmt/core.h>
#include <unordered_set>
#include <sstream>
#include <mutex>

// ---------------------------------------------------------------------------
// Global definitions for party/combat state (moved from pbc_config.cpp)
// ---------------------------------------------------------------------------
std::unordered_map<uint32_t, PBC_PartyState> g_PBC_PartyStates;
std::mutex g_PBC_PartyStateMutex;
std::unordered_map<uint32_t, PBC_GroupCombatTracker> g_PBC_GroupCombatTrackers;

// ---------------------------------------------------------------------------
// Local helpers for kill tracking (moved from pbc_combat_helpers.cpp)
// ---------------------------------------------------------------------------

// Returns true for dungeon/raid bosses, world bosses, and rare-elite or
// higher-ranked creatures.
static bool IsSignificantKill(const Creature* killed)
{
    if (!PBC_PTR_VALID(killed)) return false;
    if (killed->IsDungeonBoss() || killed->isWorldBoss()) return true;
    uint32 rank = killed->GetCreatureTemplate()->rank;
    return rank >= CREATURE_ELITE_RAREELITE;
}

// Produces a display string for the killed creature.
static std::string BuildBossLabel(const Creature* killed)
{
    std::string label = PBC_GetCreatureName(killed->GetEntry());
    if (label.empty())
        label = killed->GetName(); // fallback to live object name
    const std::string& sub = killed->GetCreatureTemplate()->SubName;
    if (!sub.empty())
        label += " (" + sub + ")";
    return label;
}

// ---------------------------------------------------------------------------
// PBC_TrackGroupKill
// ---------------------------------------------------------------------------
void PBC_TrackGroupKill(Player* killer, Creature* killed)
{
    if (!PBC_PTR_VALID(killer) || !PBC_PTR_VALID(killed)) return;

    Group* grp = killer->GetGroup();
    if (!grp) return;

    bool hasReal = false, hasBot = false;
    for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
    {
        Player* m = ref->GetSource();
        if (!m || !m->IsInWorld()) continue;
        WorldSession* ms = m->GetSession();
        if (!PBC_PTR_VALID(ms)) continue;
        if (PBC_IsBotSession(ms)) hasBot = true; else hasReal = true;
    }
    if (!hasReal || !hasBot) return;

    uint32_t grpGuid = grp->GetGUID().GetCounter();

    std::lock_guard<std::mutex> lock(g_PBC_PartyStateMutex);
    PBC_GroupCombatTracker& tracker = g_PBC_GroupCombatTrackers[grpGuid];

    if (!tracker.wasInCombat)
    {
        tracker.wasInCombat = true;
        tracker.combatStartTime = GameTime::GetGameTime().count();
        tracker.partySize = grp->GetMembersCount();
    }

    ++tracker.killCount;

    if (IsSignificantKill(killed))
    {
        tracker.notableEnemyNames.push_back(BuildBossLabel(killed));
    }
    else
    {
        std::string name = PBC_GetCreatureName(killed->GetEntry());
        if (name.empty())
            name = killed->GetName(); // fallback to live object name
        ++tracker.killedEnemies[name];
    }
}

// ---------------------------------------------------------------------------
// PBC_PollPartyState
// ---------------------------------------------------------------------------
void PBC_PollPartyState()
{
    if (!g_PBC_Enable) return;

    struct GroupInfo
    {
        Group* grp;
        Player* anchor;
        std::vector<Player*> bots;
        bool allInFlight;
        std::string sharedZone;
        bool anyAliveInCombat;
        bool allDead;
    };

    std::vector<GroupInfo> groups;
    std::unordered_set<uint32_t> seenGroups;

    WorldSessionMgr::SessionMap const& sessions = sWorldSessionMgr->GetAllSessions();
    for (auto const& [id, session] : sessions)
    {
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld()) continue;

        Group* grp = player->GetGroup();
        if (!grp) continue;

        uint32_t grpGuid = grp->GetGUID().GetCounter();
        if (seenGroups.count(grpGuid)) continue;

        bool hasReal = false;
        bool hasBot  = false;
        for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld()) continue;
            WorldSession* ms = member->GetSession();
            if (!PBC_PTR_VALID(ms)) continue;
            if (PBC_IsBotSession(ms))
                hasBot = true;
            else
                hasReal = true;
        }
        if (!hasReal || !hasBot) continue;

        seenGroups.insert(grpGuid);

        GroupInfo info;
        info.grp = grp;
        info.allInFlight = true;
        info.sharedZone.clear();
        info.anyAliveInCombat = false;
        info.allDead = true;

        bool firstMember = true;
        bool zoneMismatch = false;

        for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || !member->IsInWorld())
                continue;

            WorldSession* ms = member->GetSession();
            if (PBC_IsBotSession(ms))
                info.bots.push_back(member);

            bool inFlight = member->IsInFlight();
            if (!inFlight)
                info.allInFlight = false;

            std::string zone = PBC_BuildZoneName(member);
            if (firstMember)
            {
                info.sharedZone = zone;
                firstMember = false;
            }
            else if (zone != info.sharedZone)
            {
                zoneMismatch = true;
            }

            if (member->IsAlive())
            {
                info.allDead = false;
                if (member->IsInCombat())
                    info.anyAliveInCombat = true;
            }
        }

        if (zoneMismatch)
            info.sharedZone.clear();

        Player* anchor = nullptr;
        if (Player* leader = ObjectAccessor::FindPlayer(grp->GetLeaderGUID()))
            if (leader->IsInWorld())
                anchor = leader;
        if (!anchor && !info.bots.empty())
            anchor = info.bots.front();

        if (!anchor) continue;
        info.anchor = anchor;
        groups.push_back(std::move(info));
    }

    std::lock_guard<std::mutex> lock(g_PBC_PartyStateMutex);

    for (auto const& gi : groups)
    {
        uint32_t grpGuid = gi.grp->GetGUID().GetCounter();
        PBC_PartyState& state = g_PBC_PartyStates[grpGuid];
        PBC_GroupCombatTracker& tracker = g_PBC_GroupCombatTrackers[grpGuid];

        // --- Flight event ---
        if (g_PBC_ReplyChanceLocationChanged > 0)
        {
            if (gi.allInFlight && !state.inFlight)
            {
                std::string dest = PBC_BuildFlightDestination(gi.anchor);
                if (dest.empty())
                    dest = "an unknown destination";

                std::string eventLine = PBC_MakeEventLine(PBC_Localize("The party has started a flight to {0}", dest));
                std::string narratorText = PBC_Localize("The party started a flight to {0}", dest);

                PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: flight started — group={} dest={} bots={} chance={}%",
                         grpGuid, dest, gi.bots.size(), g_PBC_ReplyChanceLocationChanged);

                PBC_DispatchGroupEvent(gi.anchor, eventLine, narratorText,
                                       g_PBC_ReplyChanceLocationChanged);
            }
        }
        state.inFlight = gi.allInFlight;

        // --- Location event (debounced) ---
        if (g_PBC_ReplyChanceLocationChanged > 0)
        {
            if (gi.allInFlight)
            {
                // All party members are on a flight path — reset debounce state.
                // We keep state.location so we know where we departed from.
                if (!state.candidateLocation.empty())
                {
                    PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: location debounce interrupted by flight — group={} was debouncing '{}'->'{}'",
                             grpGuid, state.location, state.candidateLocation);
                }
                state.candidateLocation.clear();
                state.locationStableCycles = 0;
            }
            else if (!gi.sharedZone.empty())
            {
                // All party members share the same zone.
                if (state.location.empty())
                {
                    // First time we see this group — establish baseline silently.
                    state.location = gi.sharedZone;
                    if (!state.baselineLogged)
                    {
                        PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: location baseline set — group={} zone='{}' bots={}",
                                 grpGuid, state.location, gi.bots.size());
                        state.baselineLogged = true;
                    }
                }
                else if (gi.sharedZone != state.location)
                {
                    // Zone differs from last confirmed location — start or continue debounce.
                    if (state.candidateLocation != gi.sharedZone)
                    {
                        // New candidate zone (or first time seeing this change).
                        if (!state.candidateLocation.empty())
                        {
                            PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: location candidate changed — group={} from='{}' old_candidate='{}' new_candidate='{}'",
                                     grpGuid, state.location, state.candidateLocation, gi.sharedZone);
                        }
                        else
                        {
                            PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: location change detected — group={} from='{}' to='{}' debounce=1/{}",
                                     grpGuid, state.location, gi.sharedZone, g_PBC_LocationChangeDebounceCycles);
                        }
                        state.candidateLocation = gi.sharedZone;
                        state.locationStableCycles = 1;
                    }
                    else
                    {
                        // Same candidate — count stable cycles.
                        ++state.locationStableCycles;
                        PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: location debouncing — group={} candidate='{}' cycles={}/{}",
                                 grpGuid, state.candidateLocation, state.locationStableCycles, g_PBC_LocationChangeDebounceCycles);
                    }

                    if (state.locationStableCycles >= g_PBC_LocationChangeDebounceCycles)
                    {
                        std::string eventLine = PBC_MakeEventLine(PBC_Localize("Party has arrived in {0}", gi.sharedZone));
                        std::string narratorText = PBC_Localize("Party moved to {0}", gi.sharedZone);

                        PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: location changed — group={} from='{}' to='{}' bots={} chance={}%",
                                 grpGuid, state.location, gi.sharedZone, gi.bots.size(), g_PBC_ReplyChanceLocationChanged);

                        state.location = gi.sharedZone;
                        state.candidateLocation.clear();
                        state.locationStableCycles = 0;

                        PBC_DispatchGroupEvent(gi.anchor, eventLine, narratorText,
                                               g_PBC_ReplyChanceLocationChanged);
                    }
                }
                else
                {
                    // Same zone as last confirmed — clear any stale debounce state.
                    if (!state.candidateLocation.empty())
                    {
                        PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: location returned to confirmed zone — group={} zone='{}' clearing candidate='{}'",
                                 grpGuid, state.location, state.candidateLocation);
                    }
                    state.candidateLocation.clear();
                    state.locationStableCycles = 0;
                }
            }
            else
            {
                // sharedZone is empty: party members are in different zones.
                // Don't spam — log only when there was an active debounce being interrupted.
                if (!state.candidateLocation.empty())
                {
                    PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: location debounce on hold — group={} party not all in same zone (candidate='{}')",
                             grpGuid, state.candidateLocation);
                }
            }
        }

        // --- Combat state tracking ---
        if (gi.anyAliveInCombat && !tracker.wasInCombat)
        {
            tracker.wasInCombat = true;
            tracker.combatStartTime = GameTime::GetGameTime().count();
            tracker.partySize = gi.grp->GetMembersCount();
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: combat started — group={} partySize={}", grpGuid, tracker.partySize);
        }

        if (gi.anyAliveInCombat && tracker.wasInCombat && tracker.combatEndCycles > 0)
        {
            tracker.combatEndCycles = 0;
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: combat resumed during debounce — group={}", grpGuid);
        }

        if (gi.allDead && tracker.wasInCombat)
        {
            tracker.wiped = true;
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: party wiped — group={}", grpGuid);
        }

        if (!gi.anyAliveInCombat && !gi.allDead && tracker.wasInCombat)
        {
            ++tracker.combatEndCycles;

            if (tracker.combatEndCycles < g_PBC_CombatEndDebounceCycles)
            {
                PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: combat ended debouncing — group={} cycles={}/{}",
                         grpGuid, tracker.combatEndCycles, g_PBC_CombatEndDebounceCycles);
                continue;
            }

            if (g_PBC_ReplyChanceHardCombat == 0)
            {
                PBC_Log(PBC_LogLevel::PBC_DEBUG, "PollPartyState: combat ended but ReplyChanceHardCombat=0, skipping — group={}", grpGuid);
                tracker = PBC_GroupCombatTracker();
                continue;
            }

            bool significant = false;

            // Hard requirement: there must have been enemies involved.
            if (tracker.killCount > 0 || !tracker.notableEnemyNames.empty())
            {
                if (!tracker.notableEnemyNames.empty())
                    significant = true;

                if (tracker.deadCount > 0)
                    significant = true;

                if (tracker.killCount >= 10)
                    significant = true;
            }

            if (significant && !tracker.wiped)
            {
                std::string location;
                {
                    Player* locAnchor = gi.anchor;
                    if (Player* leader = ObjectAccessor::FindPlayer(gi.grp->GetLeaderGUID()))
                        if (leader->IsInWorld())
                            locAnchor = leader;
                    location = PBC_BuildPlaceName(locAnchor);
                }

                std::string enemiesSection;
                {
                    std::ostringstream oss;
                    oss << PBC_Localize("Regular enemies defeated: ");
                    if (!tracker.killedEnemies.empty())
                    {
                        bool first = true;
                        for (auto const& [name, count] : tracker.killedEnemies)
                        {
                            if (!first) oss << ", ";
                            first = false;
                            oss << name << " x" << count;
                        }
                    }
                    else
                    {
                        oss << PBC_Localize("none");
                    }

                    if (!tracker.notableEnemyNames.empty())
                    {
                        oss << "\n" << PBC_Localize("Significant enemies defeated: ");
                        for (size_t i = 0; i < tracker.notableEnemyNames.size(); ++i)
                        {
                            if (i > 0) oss << ", ";
                            oss << tracker.notableEnemyNames[i];
                        }
                    }

                    enemiesSection = oss.str();
                }

                std::string combatToughness;
                {
                    uint32_t dead = tracker.deadCount;
                    uint32_t size = tracker.partySize > 0 ? tracker.partySize : 1;
                    float deathRatio = static_cast<float>(dead) / static_cast<float>(size);
                    if (dead == 0)
                        combatToughness = PBC_Localize("The party confidently disposed of the enemies.");
                    else if (deathRatio <= 0.2f)
                        combatToughness = PBC_Localize("The party members suffered minor wounds.");
                    else if (deathRatio <= 0.4f)
                        combatToughness = PBC_Localize("The party members suffered major wounds.");
                    else
                        combatToughness = PBC_Localize("The party was almost wiped out and barely survived.");
                }

                time_t combatDuration = GameTime::GetGameTime().count() - tracker.combatStartTime;
                std::string durationStr;
                {
                    if (combatDuration < 30)
                        durationStr = PBC_Localize("short");
                    else if (combatDuration < 60)
                        durationStr = PBC_Localize("average");
                    else if (combatDuration < 150)
                        durationStr = PBC_Localize("long");
                    else
                        durationStr = PBC_Localize("very long");
                }

                std::string userPrompt = g_PBC_CombatEndedUserPrompt;
                PBC_ExpandNewlineEscapes(userPrompt);
                PBC_ReplaceToken(userPrompt, "location", location);
                PBC_ReplaceToken(userPrompt, "enemies_section", enemiesSection);
                PBC_ReplaceToken(userPrompt, "combat_toughness", combatToughness);
                PBC_ReplaceToken(userPrompt, "party_size", std::to_string(tracker.partySize));
                PBC_ReplaceToken(userPrompt, "combat_duration", durationStr);
                PBC_CleanUnknownTokens(userPrompt);

                PBC_EventItem ev;
                ev.type               = PBC_EventType::CombatSummarization;
                ev.chatType           = PBC_GetGroupChatType(gi.anchor);
                ev.canCreateEvents    = true;
                ev.combatSystemPrompt = g_PBC_CombatEndedSystemPrompt;
                ev.combatUserPrompt   = userPrompt;
                ev.anchorObjGuid      = gi.anchor->GetGUID();

                if (!PBC_RollGroupBotsIntoEvent(ev, gi.anchor, g_PBC_ReplyChanceHardCombat, "hard-combat"))
                {
                    // No bots rolled — still dispatch so silent chars get histLine later
                }

                AddTrackedPlayersToEvent(ev, gi.anchor);

                PBC_Log(PBC_LogLevel::PBC_DEBUG,
                         "PollPartyState: combat ended (significant) — group={} kills={} notable={} dead={}/{} toughness=\"{}\" duration={}s bots={}",
                         grpGuid, tracker.killCount, tracker.notableEnemyNames.size(),
                         tracker.deadCount, tracker.partySize, combatToughness,
                         static_cast<int>(combatDuration), ev.respondingChars.size());

                PBC_PushEvent(std::move(ev));
            }
            else
            {
                PBC_Log(PBC_LogLevel::PBC_DEBUG,
                         "PollPartyState: combat ended (not significant or wiped) — group={} kills={} wiped={}",
                         grpGuid, tracker.killCount, tracker.wiped);
            }

            tracker = PBC_GroupCombatTracker();
        }
    }

}
