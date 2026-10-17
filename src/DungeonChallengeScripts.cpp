#include "DungeonChallenge.h"
#include "DBCStores.h"
#include "Group.h"
#include "InstanceSaveMgr.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "GameTime.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "ParagonUtils.h"
#include "ScriptedCreature.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "Chat.h"

using namespace Acore::ChatCommands;

// "m:ss" for chat lines.
static std::string FormatDuration(uint32 seconds)
{
    return fmt::format("{}:{:02}", seconds / 60, seconds % 60);
}

// ============================================================================
// WorldScript - Configuration Loading & Startup
// ============================================================================

class DungeonChallengeWorldScript : public WorldScript
{
public:
    DungeonChallengeWorldScript() : WorldScript("DungeonChallengeWorldScript") { }

    void OnBeforeConfigLoad(bool reload) override
    {
        sDungeonChallengeMgr->LoadConfig(reload);
    }

    void OnStartup() override
    {
        sDungeonChallengeMgr->LoadDungeonData();
        sDungeonChallengeMgr->LoadTuningTables();
        sDungeonChallengeMgr->LoadExpectedPlayerHealth();
        sDungeonChallengeMgr->LoadLeaderboard();
        sDungeonChallengeMgr->LoadSpellOverrides();
        sDungeonChallengeMgr->LoadSnapshots();
        sDungeonChallengeMgr->CleanupActiveRuns();

        // Clean up stale pending challenges from previous sessions
        CharacterDatabase.DirectExecute(
            "DELETE FROM dungeon_challenge_pending WHERE created_at < DATE_SUB(NOW(), INTERVAL 5 MINUTE)");

        // Clean up stale run-end signals from previous sessions
        CharacterDatabase.DirectExecute(
            "DELETE FROM dungeon_challenge_runend WHERE created_at < DATE_SUB(NOW(), INTERVAL 5 MINUTE)");
    }
};

// ============================================================================
// Helpers shared by the player/map scripts (run-end handling)
// ============================================================================

// === DEATH TRIGGER (default WIPE; swap to ANY by returning true) ===
// Decides whether a player's death should end the whole run.
static bool ShouldEndRunOnDeath(ChallengeRun* run, Player* deadPlayer, Map* map)
{
    if (sDungeonChallengeMgr->GetDeathEndsRunMode() == 1)
        return true; // ALTERNATIVE: any single death ends the run

    // WIPE: end only if no other participant is still alive inside this instance.
    for (auto const& guid : run->participants)
    {
        if (guid == deadPlayer->GetGUID())
            continue;
        Player* p = ObjectAccessor::FindPlayer(guid);
        if (p && p->IsAlive() && p->GetMap() == map && p->GetInstanceId() == run->instanceId)
            return false;
    }
    return true;
}

// Resurrect (if dead) and teleport a participant to their hearthstone/home bind.
static void SendParticipantHome(Player* p)
{
    if (!p)
        return;
    if (!p->IsAlive())
    {
        p->ResurrectPlayer(0.5f);
        p->SpawnCorpseBones();
    }
    p->TeleportTo(p->m_homebindMapId, p->m_homebindX, p->m_homebindY,
        p->m_homebindZ, p->GetOrientation());
}

// ============================================================================
// PlayerScript - Player Events
// ============================================================================

class DungeonChallengePlayerScript : public PlayerScript
{
public:
    DungeonChallengePlayerScript() : PlayerScript("DungeonChallengePlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cff00ff00[Dungeon Challenge]|r Module active! Use the "
            "|cffff8000Dungeon Challenge Stone|r to start a challenge.");
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        FailRunIfPlayerLeft(player);
    }

    // Set the challenge difficulty BEFORE the teleport resolves, so the very
    // first entry already creates the right instance. The Lua UI cannot set
    // difficulties — it writes the pending row (synchronously) and teleports;
    // this hook picks the row up. Dungeon maps are raised to heroic (maps
    // without a heroic MapDifficulty entry keep their single difficulty);
    // raid-type maps (FL Conclave/Hoto/Nak'talim) are forced to the raid
    // size configured in dungeon_challenge_dungeons.raid_difficulty — e.g.
    // Hoto's real content only exists in the 25-player spawn set. Entering
    // a raid map without a raid group requires Instance.IgnoreRaid = 1.
    bool OnPlayerBeforeTeleport(Player* player, uint32 mapid, float /*x*/, float /*y*/,
        float /*z*/, float /*orientation*/, uint32 /*options*/, Unit* /*target*/) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return true;

        DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(mapid);
        // The existing Lua pending row also identifies its outside handoff.
        // Reject an active old run BEFORE departure; ordinary leave ports with
        // no Start intent keep their existing abandonment behavior.
        QueryResult intent = (info || sDungeonChallengeMgr->GetDungeonInfo(player->GetMapId()))
            ? CharacterDatabase.Query("SELECT map_id, difficulty FROM dungeon_challenge_pending WHERE player_guid = {}",
                player->GetGUID().GetCounter()) : QueryResult();
        uint32 const requestedMap = intent ? intent->Fetch()[0].Get<uint32>() : 0;
        uint32 const requestedLevel = intent ? intent->Fetch()[1].Get<uint32>() : 0;
        Group* group = player->GetGroup();
        Player* leader = group ? ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID()) : player;
        auto matchesParty = [&](ChallengeRun const* run)
        {
            if (!run || !leader || run->leaderGuid != leader->GetGUID()
                || run->mapId != requestedMap || run->difficulty != requestedLevel
                || run->participants.size() != (group ? group->GetMembersCount() : 1))
                return false;
            if (!group)
                return run->participants.count(player->GetGUID()) != 0;
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (!ref->GetSource() || !run->participants.count(ref->GetSource()->GetGUID()))
                    return false;
            return true;
        };
        ChallengeRun* joining = intent && leader && mapid == requestedMap
            && leader->GetMapId() == requestedMap
            ? sDungeonChallengeMgr->GetChallengeRun(leader->GetInstanceId()) : nullptr;
        bool const matchingJoin = matchesParty(joining) && joining->state == CHALLENGE_STATE_RUNNING;
        auto refuse = [&]()
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cffff0000[Dungeon Challenge]|r A running or occupied instance, or a permanent raid lockout, prevents a fresh start.");
            return false;
        };
        if (intent)
        {
            if (!leader || (group && group->GetMembersCount() > 5))
                return refuse();
            if (sDungeonChallengePending->GetPending(player->GetGUID())
                || sDungeonChallengePending->GetPending(leader->GetGUID()))
                return refuse(); // Do not mix a new Lua intent with an old NPC request.
            auto safeMember = [&](Player* member)
            {
                if (!member)
                    return false;
                ChallengeRun* active = sDungeonChallengeMgr->GetChallengeRun(member->GetInstanceId());
                if (!active)
                    active = sDungeonChallengeMgr->GetChallengeRunByParticipant(member->GetGUID());
                if (member->GetMapId() == requestedMap)
                    for (auto const& ref : member->GetMap()->GetPlayers())
                        if (Player* occupant = ref.GetSource())
                            if (occupant != player && (!group || occupant->GetGroup() != group))
                                return false;
                return !active || active->state == CHALLENGE_STATE_COMPLETED
                    || active->state == CHALLENGE_STATE_FAILED || active->state == CHALLENGE_STATE_ABANDONED
                    || active->state == CHALLENGE_STATE_SUMMARY || (matchingJoin && active == joining);
            };
            if (!safeMember(player))
                return refuse();
            if (group)
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (!safeMember(ref->GetSource()))
                        return refuse();
            if (mapid != requestedMap)
                return true; // Outside ACK only; do not change any bind or mode.
            if (player->GetMapId() == mapid)
                return refuse();
        }
        if (player->GetMapId() == mapid || !info)
            return true;

        MapEntry const* targetMap = sMapStore.LookupEntry(mapid);
        if (!targetMap)
            return true;
        if (intent && !targetMap->IsDungeon())
            return refuse();

        bool const isRaidMap = targetMap->IsRaid();

        bool const hasHeroic = !isRaidMap && GetMapDifficultyData(mapid, DUNGEON_DIFFICULTY_HEROIC);

        // Only act on an actual pending challenge for this destination map
        PendingChallengeInfo const* pending = sDungeonChallengePending->GetPending(player->GetGUID());
        if (pending && pending->mapId != mapid)
            pending = nullptr;

        if (!pending && !intent)
            return true;

        if (intent)
        {
            Difficulty const mode = isRaidMap ? Difficulty(info->raidDifficulty)
                : (hasHeroic ? DUNGEON_DIFFICULTY_HEROIC : DUNGEON_DIFFICULTY_NORMAL);
            // GetBound normalizes shared/downscaled modes; Unbind indexes its
            // argument directly. Resolve the same real storage key once.
            Difficulty bindMode = IsSharedDifficultyMap(mapid) ? Difficulty(mode % 2) : mode;
            if (!GetDownscaledMapDifficultyData(mapid, bindMode))
                return refuse();
            auto bound = [&](Player* member) -> InstancePlayerBind const*
            {
                BoundInstancesMap const& binds = sInstanceSaveMgr->PlayerGetBoundInstances(member->GetGUID(), bindMode);
                auto const it = binds.find(mapid);
                return it == binds.end() ? nullptr : &it->second;
            };
            auto canUnbind = [&](Player* member)
            {
                if (!member || (member->GetMapId() == mapid
                    && (!matchingJoin || member->GetInstanceId() != joining->instanceId)))
                    return false;
                if (InstancePlayerBind const* bind = bound(member))
                {
                    if (matchingJoin && bind->save->GetInstanceId() == joining->instanceId)
                        return true;
                    // A Start click carries no consent to discard a weekly
                    // raid lockout, including shared normal/heroic slots.
                    if (isRaidMap && bind->perm)
                        return false;
                    if (ChallengeRun* old = sDungeonChallengeMgr->GetChallengeRun(bind->save->GetInstanceId()))
                        if (old->state == CHALLENGE_STATE_RUNNING || old->state == CHALLENGE_STATE_PREPARING
                            || old->state == CHALLENGE_STATE_COUNTDOWN)
                            return false;
                    if (Map* oldMap = sMapMgr->FindMap(mapid, bind->save->GetInstanceId()))
                        for (auto const& ref : oldMap->GetPlayers())
                            if (Player* occupant = ref.GetSource())
                                if (occupant != player && (!group || occupant->GetGroup() != group))
                                    return false;
                }
                return true;
            };
            if (!canUnbind(player) || !canUnbind(leader))
                return refuse();
            if (group)
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (!canUnbind(ref->GetSource()))
                        return refuse();
            // Only the actual destination mode. Never drop a leader's fresh
            // binding while admitting the remaining frozen participants.
            if (!matchingJoin && bound(leader))
                sInstanceSaveMgr->PlayerUnbindInstance(leader->GetGUID(), mapid, bindMode, true, leader);
            if (player != leader)
            {
                InstancePlayerBind const* own = bound(player);
                if (own && (!matchingJoin || own->save->GetInstanceId() != joining->instanceId))
                    sInstanceSaveMgr->PlayerUnbindInstance(player->GetGUID(), mapid, bindMode, true, player);
            }
        }

        // NPC pending behavior remains unchanged on single-mode maps.
        if (!isRaidMap && !hasHeroic)
            return true;

        // Group difficulty decides the new instance for grouped players, the
        // player's own difficulty for solo runs.
        if (isRaidMap)
        {
            Difficulty raidDiff = Difficulty(info->raidDifficulty);
            if (Group* group = player->GetGroup())
            {
                if (group->GetRaidDifficulty() != raidDiff)
                    group->SetRaidDifficulty(raidDiff);
            }
            else if (player->GetRaidDifficulty() != raidDiff)
            {
                player->SetRaidDifficulty(raidDiff);
                player->SendRaidDifficulty(false);
            }
        }
        else if (group)
        {
            if (group->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_HEROIC)
                group->SetDungeonDifficulty(DUNGEON_DIFFICULTY_HEROIC);
        }
        else if (player->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_HEROIC)
        {
            player->SetDungeonDifficulty(DUNGEON_DIFFICULTY_HEROIC);
            player->SendDungeonDifficulty(false);
        }

        return true;
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        // Check if the player left an active challenge dungeon
        FailRunIfPlayerLeft(player);

        Map* map = player->GetMap();
        if (!map || !map->IsDungeon())
            return;

        // Look for pending challenge data: first in-memory (NPC), then DB (Lua GameObject)
        PendingChallengeInfo const* pending = nullptr;
        PendingChallengeInfo dbPending;
        bool fromDb = false;

        // Check in-memory store (from NPC gossip)
        pending = sDungeonChallengePending->GetPending(player->GetGUID());

        // If player is in a group, also check under group leader GUID (backwards compat)
        Group* group = player->GetGroup();
        if (!pending && group)
        {
            ObjectGuid leaderGuid = group->GetLeaderGUID();
            pending = sDungeonChallengePending->GetPending(leaderGuid);
            // Only process when the leader enters
            if (pending && player->GetGUID() != leaderGuid)
                return;
        }

        // Check DB store (from Lua GameObject)
        if (!pending)
        {
            QueryResult result = CharacterDatabase.Query(
                "SELECT map_id, difficulty FROM dungeon_challenge_pending WHERE player_guid = {}",
                player->GetGUID().GetCounter());
            if (result)
            {
                Field* fields = result->Fetch();
                dbPending.mapId = fields[0].Get<uint32>();
                dbPending.difficulty = fields[1].Get<uint32>();
                pending = &dbPending;
                fromDb = true;

                // Clean up DB entry
                CharacterDatabase.DirectExecute(
                    "DELETE FROM dungeon_challenge_pending WHERE player_guid = {}",
                    player->GetGUID().GetCounter());
            }
        }

        if (!pending)
        {
            // A participant entering (or logging in to) the instance of a run
            // saved before a restart or unload gets it back (idea 10).
            if (ChallengeRun* restored = sDungeonChallengeMgr->TryRestoreRun(player, map))
            {
                DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(restored->mapId);
                uint32 left = restored->timerDuration > restored->GetEffectiveElapsed()
                    ? restored->timerDuration - restored->GetEffectiveElapsed() : 0;
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cff00ff00[Dungeon Challenge]|r Run restored: |cffff8000{}|r Level |cffff8000{}|r | "
                    "Time left: |cffffff00{}|r | Bosses: {}/{} | Deaths: {}",
                    info ? info->name : "Unknown", restored->difficulty, FormatDuration(left),
                    restored->bossesKilled, restored->totalBosses, restored->deathCount);
            }
            return;
        }

        uint32 instanceId = player->GetInstanceId();

        // Check if instance is locked as non-challenge
        if (sDungeonChallengeMgr->IsInstanceLocked(map))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r This instance is locked! "
                "Creatures were already killed before a challenge was started.");
            if (!fromDb)
                sDungeonChallengePending->RemovePending(player->GetGUID());
            return;
        }

        // Check if dungeon is heroic — challenges require heroic mode. Maps
        // without a heroic MapDifficulty entry (custom FL dungeons) run their
        // single difficulty and are exempt, raid-type maps run their raid
        // difficulty (a dungeon-difficulty bounce would loop forever there).
        // Normally unreachable: the OnPlayerBeforeTeleport hook already
        // raised the difficulty, so this only remains as a safety net.
        if (!map->IsRaid()
            && map->GetDifficulty() == DUNGEON_DIFFICULTY_NORMAL
            && GetMapDifficultyData(map->GetId(), DUNGEON_DIFFICULTY_HEROIC))
        {
            // Set heroic difficulty so the next entry will be heroic. For
            // groups the GROUP difficulty must be set (it decides the new
            // instance) — setting only the members would bounce forever.
            if (group)
            {
                if (group->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_HEROIC)
                    group->SetDungeonDifficulty(DUNGEON_DIFFICULTY_HEROIC);
            }
            else
            {
                player->SetDungeonDifficulty(DUNGEON_DIFFICULTY_HEROIC);
                player->SendDungeonDifficulty(false);
            }

            // Re-insert pending so player can re-enter without going back to stone
            CharacterDatabase.DirectExecute(
                "REPLACE INTO `dungeon_challenge_pending` "
                "(`player_guid`, `map_id`, `difficulty`) VALUES ({}, {}, {})",
                player->GetGUID().GetCounter(), pending->mapId, pending->difficulty);

            // Teleport back to homebind
            player->TeleportTo(player->m_homebindMapId,
                player->m_homebindX, player->m_homebindY,
                player->m_homebindZ, player->GetOrientation());

            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Challenges are only available in |cffff8000heroic|r mode! "
                "Your difficulty has been set to heroic. Please enter the dungeon again.");

            if (!fromDb)
                sDungeonChallengePending->RemovePending(player->GetGUID());
            return;
        }

        // Per-member Lua pending rows join the leader's existing run. They
        // must never overwrite its timer, affixes, origins or participants.
        if (ChallengeRun* existing = sDungeonChallengeMgr->GetChallengeRun(instanceId))
        {
            ObjectGuid const leaderGuid = group ? group->GetLeaderGUID() : player->GetGUID();
            bool matches = existing->state == CHALLENGE_STATE_RUNNING
                && existing->mapId == pending->mapId && existing->difficulty == pending->difficulty
                && existing->leaderGuid == leaderGuid
                && existing->participants.size() == (group ? group->GetMembersCount() : 1)
                && existing->participants.count(player->GetGUID());
            if (group)
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (!ref->GetSource() || !existing->participants.count(ref->GetSource()->GetGUID()))
                        matches = false;
            if (!matches)
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cffff0000[Dungeon Challenge]|r The instance belongs to a different run or party.");
            if (!fromDb)
                sDungeonChallengePending->RemovePending(player->GetGUID());
            return;
        }

        // The Lua handoff admits the leader first and waits for its map ACK.
        // Refuse a changed group instead of creating a second leader instance.
        if (fromDb && group && player->GetGUID() != group->GetLeaderGUID())
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cffff0000[Dungeon Challenge]|r The group leader must enter first.");
            return;
        }

        // Create the challenge run
        ChallengeRun* run = sDungeonChallengeMgr->CreateChallengeRun(
            instanceId, pending->mapId, pending->difficulty, player);

        if (!run)
        {
            if (!fromDb)
                sDungeonChallengePending->RemovePending(player->GetGUID());
            return;
        }

        // Store origin positions for post-completion teleport
        auto storeOrigin = [&](Player* p)
        {
            WorldLocation const& entry = p->GetEntryPoint();
            if (entry.GetMapId() != MAPID_INVALID)
                run->participantOrigins[p->GetGUID()] = entry;
            else
                run->participantOrigins[p->GetGUID()] = WorldLocation(
                    p->m_homebindMapId, p->m_homebindX, p->m_homebindY, p->m_homebindZ, 0.0f);
        };

        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    storeOrigin(member);
        }
        else
        {
            storeOrigin(player);
        }

        // Start immediately - assign affixes and start the run
        sDungeonChallengeMgr->AssignAffixesToCreatures(run, map);
        sDungeonChallengeMgr->StartRun(run);

        DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(run->mapId);
        std::string dungeonName = info ? info->name : "Unknown";

        // Notify all participants (group or solo)
        auto notifyPlayer = [&](Player* p)
        {
            ChatHandler(p->GetSession()).PSendSysMessage(
                "|cff00ff00[Dungeon Challenge]|r |cffff8000{}|r Level |cffff8000{}|r started! "
                "Timer: |cffffff00{} minutes|r | Bosses: |cff00ff00{}|r | "
                "~15%% of mobs have ALL available affixes!",
                dungeonName, run->difficulty, run->timerDuration / 60, run->totalBosses);
        };

        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                if (Player* member = ref->GetSource())
                    notifyPlayer(member);
            }
        }
        else
        {
            notifyPlayer(player);
        }

        if (!fromDb)
            sDungeonChallengePending->RemovePending(player->GetGUID());
    }

    void OnPlayerJustDied(Player* player) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        Map* map = player->GetMap();
        if (!map || !map->IsDungeon())
            return;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(player->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        run->deathCount++;
        run->penaltyTime += sDungeonChallengeMgr->GetDeathPenalty();
        sDungeonChallengeMgr->SaveActiveRun(run);

        // Notify all participants (group or solo)
        auto notifyDeath = [&](Player* p)
        {
            ChatHandler(p->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r |cff69ccf0{}|r has died! "
                "(Deaths: {}, +{}s time penalty, Total penalty: {}s)",
                player->GetName(), run->deathCount,
                sDungeonChallengeMgr->GetDeathPenalty(), run->penaltyTime);
        };

        Group* group = player->GetGroup();
        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                if (Player* member = ref->GetSource())
                    notifyDeath(member);
            }
        }
        else
        {
            notifyDeath(player);
        }

        // End the run on a wipe (default) or on any death (configurable). The
        // player is NOT teleported out yet — the summary opens first (see Lua),
        // and the repop block below keeps the dead player in place meanwhile.
        if (ShouldEndRunOnDeath(run, player, map))
            sDungeonChallengeMgr->EndRunByDeath(run);
    }

    // Keep a dead player in place during the run-end summary window instead of
    // sending them to a graveyard ("not instantly teleported out"). The core
    // checks this before any graveyard repop (Player::RepopAtGraveyard).
    bool OnPlayerCanRepopAtGraveyard(Player* player) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return true;

        // Cheap early-out: only relevant while the dead player is still in the
        // challenge instance (this hook fires on every repop server-wide).
        Map* map = player->GetMap();
        if (!map || !map->IsDungeon())
            return true;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(player->GetInstanceId());
        if (run && (run->state == CHALLENGE_STATE_SUMMARY
                 || run->state == CHALLENGE_STATE_COMPLETED
                 || run->state == CHALLENGE_STATE_FAILED))
            return false;
        return true;
    }

private:
    // True if any participant is still inside the challenge instance.
    bool AnyParticipantInInstance(ChallengeRun* run)
    {
        for (auto const& guid : run->participants)
        {
            Player* p = ObjectAccessor::FindPlayer(guid);
            if (p && p->GetMap() && p->GetMap()->IsDungeon()
                && p->GetInstanceId() == run->instanceId)
                return true;
        }
        return false;
    }

    // A participant left the instance during the summary window (Lua teleported
    // them home via the 30s timer or the Leave button, already unbinding them).
    // Revive them if they died, and drop the run once the instance is empty.
    void HandleParticipantExit(Player* player, ChallengeRun* run)
    {
        if (!player->IsAlive())
        {
            player->ResurrectPlayer(0.5f);
            player->SpawnCorpseBones();
        }

        if (!AnyParticipantInInstance(run))
            sDungeonChallengeMgr->RemoveChallengeRun(run->instanceId);
    }

    void FailRunIfPlayerLeft(Player* player)
    {
        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRunByParticipant(player->GetGUID());
        if (!run)
            return;

        // If the player is still in the challenge dungeon instance, do nothing
        Map* map = player->GetMap();
        if (map && map->IsDungeon() && map->GetInstanceId() == run->instanceId)
            return;

        // Player left while the run was being summarised (Leave button / auto-teleport)
        if (run->state == CHALLENGE_STATE_SUMMARY)
        {
            HandleParticipantExit(player, run);
            return;
        }

        if (run->state != CHALLENGE_STATE_RUNNING)
            return;

        // Player abandoned the run mid-way — end it and send everyone home
        sDungeonChallengeMgr->FailRun(run);

        DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(run->mapId);
        std::string dungeonName = info ? info->name : "Unknown";

        for (auto const& guid : run->participants)
        {
            Player* p = ObjectAccessor::FindPlayer(guid);
            if (!p)
                continue;

            ChatHandler(p->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r |cff69ccf0{}|r left the dungeon! "
                "Challenge |cffff8000{}|r Level |cffff8000{}|r has been ended.",
                player->GetName(), dungeonName, run->difficulty);

            // Teleport the remaining participants (still inside) back home
            if (p->GetMap() && p->GetMap()->IsDungeon() && p->GetInstanceId() == run->instanceId)
                SendParticipantHome(p);
        }

        sDungeonChallengeMgr->RemoveChallengeRun(run->instanceId);
    }
};

// ============================================================================
// Helper: Boss detection (rank >= 3, world boss, or dungeon boss via flags_extra)
// ============================================================================

static bool IsChallengeBoss(Creature const* creature)
{
    return sDungeonChallengeMgr->IsChallengeBoss(creature);
}

// ============================================================================
// AllCreatureScript - Creature Processing & Affix Behavior
// ============================================================================

class DungeonChallengeCreatureScript : public AllCreatureScript
{
public:
    DungeonChallengeCreatureScript() : AllCreatureScript("DungeonChallengeCreatureScript") { }

    // The core rebuilt the creature's stats from its template (respawn,
    // UpdateEntry): the scaled health is gone while `processed` was still set.
    // Clear the flags so the next update scales it again (and re-applies its
    // affixes). Get(), not GetDefault(): this runs for every creature that is
    // created anywhere, and only ones the DC already touched need it.
    void OnCreatureSelectLevel(CreatureTemplate const* /*cinfo*/, Creature* creature) override
    {
        auto* creatureData = creature->CustomData.Get<CreatureChallengeData>("mod-dungeon-challenge");
        if (!creatureData || !creatureData->scaled)
            return;

        creatureData->processed = false;
        creatureData->scaled = false;
        creatureData->baseCreateHealth = 0;
        creatureData->originalLevel = 0;
        if (!creatureData->affixes.empty())
            creatureData->affixesApplied = false;
    }

    void OnAllCreatureUpdate(Creature* creature, uint32 diff) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        Map* map = creature->GetMap();
        if (!map || !map->IsDungeon())
            return;

        // Resolve the run BEFORE touching per-creature CustomData. This hook runs
        // for EVERY creature on EVERY dungeon map every tick, and with
        // MapUpdateInterval=10 that is ~100x/s per creature; GetDefault() hashes a
        // 21-character string key and heap-allocates the blob on first touch.
        //
        // The reorder is behaviour-neutral because ProcessCreature() itself returns
        // without setting `processed` when no run is RUNNING (DungeonChallenge.cpp:
        // 550-551) -- which is precisely why the old order kept re-paying three
        // DataMap lookups per creature per tick, forever, on every dungeon map that
        // had no challenge running at all.
        //
        // Gating on _activeRuns (GetChallengeRun) rather than on ProcessCreature's
        // internal mapData->run also keeps this path away from that pointer, which
        // RemoveChallengeRun() leaves dangling (see the module todo.md).
        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
        if (!creatureData->processed)
        {
            sDungeonChallengeMgr->ProcessCreature(creature, map);
        }

        sDungeonChallengeMgr->UpdateCreatureRunAffixes(run, creature, diff);

        // --- MOB RESPAWN: non-boss, non-affix mobs respawn at original position when pulled ---
        // Placed spawns only (spawn id != 0). A summon has no original position:
        // without this, boss adds - and the players' own pets and guardians -
        // were copied twice as well.
        if (creature->IsAlive() && creature->IsInCombat()
            && creature->GetSpawnId() != 0
            && !creatureData->isRespawnCopy
            && !creatureData->combatTracked
            && creatureData->affixes.empty())
        {
            // Skip bosses (rank >= 3, world boss, or dungeon boss via flags_extra)
            if (!IsChallengeBoss(creature))
            {
                creatureData->combatTracked = true;
                creature->GetHomePosition(creatureData->homeX, creatureData->homeY,
                                          creatureData->homeZ, creatureData->homeO);
                creatureData->respawnTimer = 3000;
            }
        }

        // Handle respawn timer countdown and spawning
        if (creatureData->combatTracked && creatureData->respawnsTriggered < 2 && creatureData->respawnTimer > 0)
        {
            if (creatureData->respawnTimer <= diff)
            {
                creatureData->respawnTimer = 0;

                Creature* copy = creature->SummonCreature(creature->GetEntry(),
                    creatureData->homeX, creatureData->homeY, creatureData->homeZ, creatureData->homeO,
                    TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60000);
                if (copy)
                {
                    auto* copyData = copy->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
                    copyData->isRespawnCopy = true;
                    copyData->processed = true;
                    copyData->noLoot = true;

                    // Apply difficulty scaling
                    if (run)
                    {
                        sDungeonChallengeMgr->ScaleCreatureForDifficulty(copy, run->difficulty);
                    }

                    // Attack the player who pulled the original mob
                    if (Unit* victim = creature->GetVictim())
                        copy->AI()->AttackStart(victim);
                }

                creatureData->respawnsTriggered++;
                if (creatureData->respawnsTriggered < 2)
                    creatureData->respawnTimer = 3000; // schedule next respawn
            }
            else
            {
                creatureData->respawnTimer -= diff;
            }
        }

        // --- RESPAWN COPY EVADE: despawn copies that leave combat ---
        if (creatureData->isRespawnCopy && creature->IsAlive())
        {
            if (creature->IsInCombat())
                creatureData->copyWasInCombat = true;
            else if (creatureData->copyWasInCombat)
                creature->DespawnOrUnsummon();
        }

        if (!creature->IsAlive() || creatureData->affixes.empty())
            return;

        // --- AFFIX REAPPLICATION: reapply affixes after mob reset/evade ---
        // When a creature evades, its auras are removed and HP resets to base.
        // Detect this by checking if a known affix aura is missing.
        if (creatureData->affixesApplied && !creature->IsInCombat())
        {
            // Check if any expected aura is gone (evade strips auras)
            bool aurasLost = false;
            for (auto const& affix : creatureData->affixes)
            {
                uint32 spellId = 0;
                switch (affix)
                {
                    case AFFIX_SPEEDY:        spellId = SPELL_AFFIX_SPEEDY; break;
                    case AFFIX_BIG_BOY:       spellId = SPELL_AFFIX_BIG_BOY; break;
                    case AFFIX_CC_IMMUNITY:    spellId = SPELL_AFFIX_CC_IMMUNITY; break;
                    case AFFIX_HEAVY_HITS:     spellId = SPELL_AFFIX_HEAVY_HITS; break;
                    case AFFIX_BIGGER_BOY:     spellId = SPELL_AFFIX_BIGGER_BOY; break;
                    case AFFIX_IMMOLATION:     spellId = SPELL_AFFIX_IMMOLATION; break;
                    case AFFIX_HELL_TOUCHED:   spellId = SPELL_AFFIX_HELL_TOUCHED; break;
                    case AFFIX_CALL_FOR_HELP:  spellId = SPELL_AFFIX_CALL_FOR_HELP; break;
                    case AFFIX_LIL_BRO:        spellId = SPELL_AFFIX_LIL_BRO; break;
                    case AFFIX_DAMAGE_REDUCE:  spellId = SPELL_AFFIX_DAMAGE_REDUCE; break;
                    default: break;
                }
                if (spellId && !creature->HasAura(spellId))
                {
                    aurasLost = true;
                    break;
                }
            }

            if (aurasLost)
            {
                creatureData->affixesApplied = false;
                creatureData->processed = false;
            }
        }

        if (!creatureData->affixesApplied && !creatureData->affixes.empty())
        {
            // Mob was reset — reapply difficulty scaling + all affixes
            sDungeonChallengeMgr->ScaleCreatureForDifficulty(creature, run->difficulty);
            for (auto const& affix : creatureData->affixes)
                sDungeonChallengeMgr->ApplyAffixToCreature(creature, affix, run->difficulty);

            // Reset Call for Help flag so it can trigger again
            creatureData->hasCalled = false;
        }

        // --- CALL FOR HELP: pull allies within 30y when entering combat ---
        if (creatureData->HasAffix(AFFIX_CALL_FOR_HELP) && creature->IsInCombat() && !creatureData->hasCalled)
        {
            Unit* victim = creature->GetVictim();
            if (!victim)
                victim = creature->GetThreatMgr().GetCurrentVictim();
            if (victim)
            {
                creatureData->hasCalled = true;
                float const callRange = sDungeonChallengeMgr->GetAffixValue(AFFIX_CALL_FOR_HELP, 1, 30.0f);
                for (auto const& pair : map->GetCreatureBySpawnIdStore())
                {
                    Creature* ally = pair.second;
                    if (!ally || !ally->IsAlive() || ally == creature || ally->IsInCombat())
                        continue;
                    if (ally->IsPet() || ally->IsSummon() || ally->IsTotem())
                        continue;
                    if (ally->GetCreatureTemplate()->faction == 35)
                        continue;
                    if (ally->GetDistance(creature) > callRange)
                        continue;

                    ally->AI()->AttackStart(victim);
                }

                Map::PlayerList const& players = map->GetPlayers();
                for (auto const& ref : players)
                    if (Player* p = ref.GetSource())
                        ChatHandler(p->GetSession()).PSendSysMessage(
                            "|cffff8000[Affix: Call for Help]|r |cffff0000{}|r called allies for help!",
                            creature->GetName());
            }
        }

        // --- IMMOLATION AURA: periodic fire damage to nearby players ---
        if (creatureData->HasAffix(AFFIX_IMMOLATION))
        {
            creatureData->immolationTimer += diff;
            if (creatureData->immolationTimer >= 2000)
            {
                creatureData->immolationTimer -= 2000;
                uint32 damage = uint32(run->difficulty * sDungeonChallengeMgr->GetAffixValue(AFFIX_IMMOLATION, 1, 80.0f));
                float const range = sDungeonChallengeMgr->GetAffixValue(AFFIX_IMMOLATION, 2, 8.0f);

                Map::PlayerList const& players = map->GetPlayers();
                for (auto const& ref : players)
                {
                    if (Player* player = ref.GetSource())
                    {
                        if (player->IsAlive() && creature->GetDistance(player) <= range)
                            player->EnvironmentalDamage(DAMAGE_FIRE, damage);
                    }
                }
            }
        }
    }
};

// ============================================================================
// PlayerScript - Creature Kill Hook for Affix Processing & Non-Mythic Lock
// ============================================================================

class DungeonChallengeCreatureDeathScript : public PlayerScript
{
public:
    DungeonChallengeCreatureDeathScript() : PlayerScript("DungeonChallengeCreatureDeathScript") { }

    void OnPlayerCreatureKill(Player* killer, Creature* creature) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        Map* map = creature->GetMap();
        if (!map || !map->IsDungeon())
            return;

        // Check if this dungeon is capable of challenges
        if (!sDungeonChallengeMgr->IsDungeonCapable(map->GetId()))
            return;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());

        // NON-MYTHIC LOCK: If a creature dies and there's no active challenge,
        // permanently lock this instance to prevent later challenge activation
        if (!run || run->state == CHALLENGE_STATE_PREPARING)
        {
            // Skip friendly/critter mobs
            if (creature->GetCreatureTemplate()->faction != 35 &&
                creature->GetCreatureTemplate()->unit_class != 0 &&
                !creature->IsPet() && !creature->IsSummon())
            {
                // Silent on purpose (operator, 2026-10-03): every kill of a
                // normal run used to tell every player the instance is now
                // locked. Players learn it only when they try to start a
                // challenge here (the "This instance is locked!" message).
                sDungeonChallengeMgr->LockInstanceAsNonChallenge(map);
            }
            return;
        }

        if (run->state != CHALLENGE_STATE_RUNNING)
            return;

        // Award paragon XP for all mob kills during active challenges.
        // Skip Lil' Bro copies (generation > 0) — they are split spawns,
        // not real mobs. Bosses and regular mobs both award XP here because
        // the paragon module's own CalculateXPGain skips most dungeon mobs
        // (level < player level in normal mode, non-elite mobs get 0 XP).
        {
            auto* cData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
            bool isLilBroCopy = cData->lilBroGeneration > 0;

            if (!isLilBroCopy && !creature->IsPet() && !creature->IsSummon())
            {
                uint32 xpAmount = IsChallengeBoss(creature) ? 3 : 1;

                if (Group* group = killer->GetGroup())
                {
                    Group::MemberSlotList const& members = group->GetMemberSlots();
                    for (auto const& slot : members)
                    {
                        Player* p = ObjectAccessor::GetPlayer(killer->GetMap(), slot.guid);
                        if (p)
                            IncreaseParagonXP(p, xpAmount);
                    }
                }
                else
                {
                    IncreaseParagonXP(killer, xpAmount);
                }
            }
        }

        // Check if this was a boss (rank >= 3, world boss, or dungeon boss)
        if (IsChallengeBoss(creature))
        {
            // Grouped encounter (e.g. the Malachar trio in FL Ak'Tazia):
            // all members share one boss credit, granted when the LAST
            // living member of the group dies. Earlier member kills are
            // not counted, announced, or snapshotted.
            bool countKill = true;
            if (uint32 groupId = sDungeonChallengeMgr->GetBossGroupId(creature->GetEntry()))
            {
                for (auto const& pair : map->GetCreatureBySpawnIdStore())
                {
                    Creature* other = pair.second;
                    if (!other || other == creature || !other->IsAlive())
                        continue;
                    if (sDungeonChallengeMgr->GetBossGroupId(other->GetEntry()) == groupId)
                    {
                        countKill = false;
                        break;
                    }
                }
            }

            if (countKill)
            {
                run->bossesKilled++;
                bool isFinalBoss = run->AllBossesKilled();

                std::string bossName = creature->GetName();

                // Combat time (idea 8): from the first player hit to the kill.
                uint32 combatTime = 0;
                if (auto* bossData = creature->CustomData.Get<CreatureChallengeData>("mod-dungeon-challenge"))
                    if (bossData->engageTimeMs)
                        combatTime = uint32((uint64(GameTime::GetGameTimeMS().count()) - bossData->engageTimeMs) / IN_MILLISECONDS);

                Map::PlayerList const& players = map->GetPlayers();
                for (auto const& ref : players)
                {
                    if (Player* player = ref.GetSource())
                    {
                        ChatHandler(player->GetSession()).PSendSysMessage(
                            "|cff00ff00[Dungeon Challenge]|r Boss |cffff8000{}|r defeated! ({}/{}) - fight |cffffff00{}|r",
                            bossName, run->bossesKilled, run->totalBosses, FormatDuration(combatTime));
                    }
                }

                run->lastBossName = bossName;
                run->lastBossCombatTime = combatTime;

                // Save boss kill snapshot
                sDungeonChallengeMgr->SaveBossKillSnapshot(run, creature, isFinalBoss, isFinalBoss && !run->IsTimedOut(), combatTime);
                if (!isFinalBoss)
                    sDungeonChallengeMgr->SaveActiveRun(run);

                // Check completion
                if (isFinalBoss)
                {
                    sDungeonChallengeMgr->CompleteRun(run);
                }
            }
        }

        // Process affix on-death effects
        auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");

        // Remove loot from non-lootable Lil' Bro copies (generation > 0)
        if (creatureData->noLoot)
        {
            creature->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
            creature->loot.clear();
        }

        if (creatureData->affixes.empty())
            return;

        // --- LIL' BRO: split 1->2->4 (generation 0->1->2, stop at gen 2) ---
        if (creatureData->HasAffix(AFFIX_LIL_BRO) && creatureData->lilBroGeneration < 2)
        {
            HandleLilBroDeath(creature, run, map, killer);
        }
    }

private:
    void HandleLilBroDeath(Creature* creature, ChallengeRun* run, Map* map, Player* killer)
    {
        auto* parentData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
        uint8 nextGeneration = parentData->lilBroGeneration + 1;

        uint32 entry = creature->GetEntry();
        float x = creature->GetPositionX();
        float y = creature->GetPositionY();
        float z = creature->GetPositionZ();
        float o = creature->GetOrientation();

        // Calculate HP for copies: 10% of the creature's max health
        uint32 copyHealth = std::max(1u, static_cast<uint32>(creature->GetMaxHealth()
            * sDungeonChallengeMgr->GetAffixValue(AFFIX_LIL_BRO, 1, 0.1f)));

        // Find a valid target: dead creature may have lost its victim
        Unit* target = creature->GetVictim();
        if (!target)
            target = killer;

        for (uint32 i = 0; i < 2; ++i)
        {
            float offsetX = (i == 0) ? 2.0f : -2.0f;
            float offsetY = (i == 0) ? 1.0f : -1.0f;

            Creature* copy = creature->SummonCreature(entry, x + offsetX, y + offsetY, z, o,
                TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60000);
            if (!copy)
                continue;

            auto* copyData = copy->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
            // The template's health stays the base a later re-scaling (after an
            // evade) starts from, as it did before the split health was set.
            copyData->baseCreateHealth = copy->GetCreateHealth();
            DungeonChallengeMgr::SetCreatureBaseHealth(copy, copyHealth);

            copyData->lilBroGeneration = nextGeneration;
            copyData->affixes = parentData->affixes;
            copyData->processed = true;
            copyData->scaled = true;
            copyData->affixesApplied = true;

            // Apply difficulty damage scaling (level and dungeon factor, as the parent)
            copyData->extraDamageMultiplier = sDungeonChallengeMgr->GetCreatureDamageMultiplier(copy, run->difficulty);
            copyData->healthMultiplier = parentData->healthMultiplier;

            // Only original (generation 0) drops loot; all splits don't
            copyData->noLoot = true;

            // Apply affix auras to copies (visual + effects)
            for (auto const& affix : parentData->affixes)
                sDungeonChallengeMgr->ApplyAffixToCreature(copy, affix, run->difficulty);

            if (target)
                copy->AI()->AttackStart(target);
        }

        Map::PlayerList const& players = map->GetPlayers();
        for (auto const& ref : players)
            if (Player* p = ref.GetSource())
                ChatHandler(p->GetSession()).PSendSysMessage(
                    "|cffff8000[Affix: Lil' Bro]|r |cffff0000{}|r split into two!",
                    creature->GetName());
    }
};

// ============================================================================
// UnitScript - Damage Modification via Hooks
// ============================================================================

class DungeonChallengeUnitScript : public UnitScript
{
public:
    DungeonChallengeUnitScript() : UnitScript("DungeonChallengeUnitScript") { }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        Map* map = attacker ? attacker->GetMap() : (target ? target->GetMap() : nullptr);
        if (!map || !map->IsDungeon())
            return;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        // --- Creature → Player: damage multiplier + Hell Touched ---
        Creature* creature = attacker ? attacker->ToCreature() : nullptr;
        if (creature)
        {
            auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
            if (creatureData->extraDamageMultiplier != 1.0f)
                damage = static_cast<uint32>(damage * creatureData->extraDamageMultiplier);

            // Hell Touched: extra 666 unmitigatable fire+shadow damage + debuff
            if (creatureData->HasAffix(AFFIX_HELL_TOUCHED))
            {
                if (Player* playerTarget = target->ToPlayer())
                {
                    playerTarget->EnvironmentalDamage(DAMAGE_FIRE, uint32(sDungeonChallengeMgr->GetAffixValue(AFFIX_HELL_TOUCHED, 1, 666.0f)));
                    playerTarget->CastSpell(playerTarget, SPELL_AFFIX_HELL_TOUCHED_DEBUFF, true);
                }
            }
        }

        // --- Player → Creature: damage reduction from DAMAGE_REDUCE affix ---
        Creature* targetCreature = target ? target->ToCreature() : nullptr;
        if (targetCreature)
        {
            auto* tData = targetCreature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
            if (tData->incomingDamageReduction > 0.0f)
                damage = static_cast<uint32>(damage * (1.0f - tData->incomingDamageReduction));
        }
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        Map* map = attacker ? attacker->GetMap() : (target ? target->GetMap() : nullptr);
        if (!map || !map->IsDungeon())
            return;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        // --- Creature → Player: spell override + damage multiplier + Hell Touched ---
        Creature* creature = attacker ? attacker->ToCreature() : nullptr;
        if (creature)
        {
            auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");

            // An uplifted creature's spells grow to its new level (idea 9).
            if (creatureData->originalLevel)
                damage = sDungeonChallengeMgr->ScaleUpliftedSpellDamage(creature, damage);

            // Check for spell-specific override first
            if (spellInfo)
            {
                SpellOverrideEntry const* override = sDungeonChallengeMgr->GetSpellOverride(spellInfo->Id, map->GetId());
                if (override && override->modPct >= 0.0f)
                {
                    damage = static_cast<int32>(damage * override->modPct);
                    return;
                }
            }

            if (creatureData->extraDamageMultiplier != 1.0f)
                damage = static_cast<int32>(damage * creatureData->extraDamageMultiplier);

            // Hell Touched: extra 666 fire+shadow on direct spell damage + debuff
            if (creatureData->HasAffix(AFFIX_HELL_TOUCHED))
            {
                if (Player* playerTarget = target->ToPlayer())
                {
                    playerTarget->EnvironmentalDamage(DAMAGE_FIRE, uint32(sDungeonChallengeMgr->GetAffixValue(AFFIX_HELL_TOUCHED, 1, 666.0f)));
                    playerTarget->CastSpell(playerTarget, SPELL_AFFIX_HELL_TOUCHED_DEBUFF, true);
                }
            }
        }

        // --- Player → Creature: damage reduction ---
        Creature* targetCreature = target ? target->ToCreature() : nullptr;
        if (targetCreature)
        {
            auto* tData = targetCreature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
            if (tData->incomingDamageReduction > 0.0f)
                damage = static_cast<int32>(damage * (1.0f - tData->incomingDamageReduction));
        }
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        Creature* creature = attacker ? attacker->ToCreature() : nullptr;
        if (!creature)
            return;

        Map* map = creature->GetMap();
        if (!map || !map->IsDungeon())
            return;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        // The periodic HEAL tick calls this damage hook too (SpellAuraEffects.cpp,
        // HandlePeriodicHealAurasTick). Heals are scaled in ModifyHealReceived;
        // multiplying them here as well used to give mob HoTs the damage factor.
        if (spellInfo && (spellInfo->HasAura(SPELL_AURA_PERIODIC_HEAL) || spellInfo->HasAura(SPELL_AURA_OBS_MOD_HEALTH)))
            return;
        // The Entangling Roots run affix hurts as cast; its caster's factor does not apply.
        if (spellInfo && spellInfo->Id == SPELL_ENTANGLING_ROOTS)
            return;

        auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");

        // An uplifted creature's DoTs grow to its new level (idea 9).
        if (creatureData->originalLevel)
            damage = uint32(sDungeonChallengeMgr->ScaleUpliftedSpellDamage(creature, int32(std::min<uint32>(damage, std::numeric_limits<int32>::max()))));

        // Check for spell-specific DoT override
        if (spellInfo)
        {
            SpellOverrideEntry const* override = sDungeonChallengeMgr->GetSpellOverride(spellInfo->Id, map->GetId());
            if (override && override->dotModPct >= 0.0f)
            {
                damage = static_cast<uint32>(damage * override->dotModPct);
                return;
            }
        }

        // Apply general creature damage multiplier (DoTs don't trigger Hell Touched)
        if (creatureData->extraDamageMultiplier != 1.0f)
            damage = static_cast<uint32>(damage * creatureData->extraDamageMultiplier);
    }

    // Mob heals grow with mob health: a heal sized for 1x health barely moves a
    // mob at 16x (Mythic+ survey 2026-10-10 §4.3). Only between two scaled NPC
    // creatures. The core's two call sites pass healer and target in opposite
    // orders (Unit::HealBySpell vs the periodic heal tick), so the check is
    // symmetric and the factor is the run's, not either unit's.
    void ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellInfo const* /*spellInfo*/) override
    {
        if (!heal || !target || !healer || !sDungeonChallengeMgr->IsEnabled())
            return;

        Creature* first = target->ToCreature();
        Creature* second = healer->ToCreature();
        if (!first || !second)
            return;

        Map* map = first->GetMap();
        if (!map || !map->IsDungeon() || second->GetMap() != map)
            return;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        auto* firstData = first->CustomData.Get<CreatureChallengeData>("mod-dungeon-challenge");
        auto* secondData = second->CustomData.Get<CreatureChallengeData>("mod-dungeon-challenge");
        if (!firstData || !secondData || !firstData->scaled || !secondData->scaled)
            return;
        if (first->IsCharmedOwnedByPlayerOrPlayer() || second->IsCharmedOwnedByPlayerOrPlayer())
            return;

        // Each creature carries its own factor (level x the dungeon's boss or
        // trash factor); the order of the two units is not reliable, so the
        // larger one wins.
        float const factor = std::max(firstData->healthMultiplier, secondData->healthMultiplier);
        heal = static_cast<uint32>(std::min<double>(double(heal) * factor, double(std::numeric_limits<uint32>::max())));
    }

    // Combat time (idea 8): a boss's clock starts at the first hit from a
    // player or a player's unit, and stops on an evade.
    void OnDamage(Unit* attacker, Unit* victim, uint32& /*damage*/) override
    {
        Creature* boss = victim ? victim->ToCreature() : nullptr;
        if (!boss || !attacker || !sDungeonChallengeMgr->IsEnabled())
            return;
        Map* map = boss->GetMap();
        if (!map || !map->IsDungeon())
            return;
        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING || !sDungeonChallengeMgr->IsChallengeBoss(boss))
            return;
        if (!attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
            return;

        auto* data = boss->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
        if (!data->engageTimeMs)
            data->engageTimeMs = uint64(GameTime::GetGameTimeMS().count());
    }

    void OnUnitEnterEvadeMode(Unit* unit, uint8 /*evadeReason*/) override
    {
        if (Creature* creature = unit ? unit->ToCreature() : nullptr)
            if (auto* data = creature->CustomData.Get<CreatureChallengeData>("mod-dungeon-challenge"))
                data->engageTimeMs = 0;
    }
};

// ============================================================================
// Timer & Countdown Script (AllMapScript)
// ============================================================================

class DungeonChallengeTimerScript : public AllMapScript
{
public:
    DungeonChallengeTimerScript() : AllMapScript("DungeonChallengeTimerScript") { }

    void OnMapUpdate(Map* map, uint32 diff) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        if (!map || !map->IsDungeon())
            return;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run)
            return;

        // Handle keystone countdown
        if (run->state == CHALLENGE_STATE_COUNTDOWN)
        {
            HandleCountdown(run, map, diff);
            return;
        }

        // Run ended (clear or death): summary window. Lua drives the visible
        // 30s countdown + teleport-home + Leave button. This is the C++ safety
        // net for when the client addon/Lua is unavailable, plus final cleanup.
        if (run->state == CHALLENGE_STATE_SUMMARY
            || run->state == CHALLENGE_STATE_COMPLETED
            || run->state == CHALLENGE_STATE_FAILED)
        {
            run->summaryElapsedMs += diff;
            if (run->summaryElapsedMs >= sDungeonChallengeMgr->GetFallbackSeconds() * 1000)
            {
                // Teleport any participant still inside back to their home bind.
                for (auto const& guid : run->participants)
                {
                    Player* p = ObjectAccessor::FindPlayer(guid);
                    if (p && p->GetInstanceId() == map->GetInstanceId())
                    {
                        SendParticipantHome(p);
                        ChatHandler(p->GetSession()).PSendSysMessage(
                            "|cff00ff00[Dungeon Challenge]|r You have been returned home.");
                    }
                }
                sDungeonChallengeMgr->RemoveChallengeRun(map->GetInstanceId());
            }
            return;
        }

        if (run->state != CHALLENGE_STATE_RUNNING)
            return;

        // Update elapsed time
        sDungeonChallengeMgr->UpdateRun(run, diff);
        sDungeonChallengeMgr->UpdateRunAffixes(run, map, diff);

        // Periodic timer announcements
        uint32 effectiveElapsed = run->GetEffectiveElapsed();
        uint32 remaining = run->timerDuration > effectiveElapsed ?
            run->timerDuration - effectiveElapsed : 0;

        static std::unordered_map<uint32, uint32> lastAnnounce;
        uint32 instanceId = map->GetInstanceId();
        uint32 lastTime = lastAnnounce[instanceId];
        bool shouldAnnounce = false;

        if (remaining <= 10 && lastTime > 10)
            shouldAnnounce = true;
        else if (remaining <= 30 && lastTime > 30)
            shouldAnnounce = true;
        else if (remaining <= 60 && lastTime > 60)
            shouldAnnounce = true;
        else if (remaining <= 300 && lastTime > 300)
            shouldAnnounce = true;
        else if (lastTime > 0 && (lastTime / 60 != remaining / 60) && remaining % 60 == 0)
            shouldAnnounce = true;

        if (remaining == 0 && lastTime > 0)
        {
            Map::PlayerList const& players = map->GetPlayers();
            for (auto const& ref : players)
            {
                if (Player* player = ref.GetSource())
                {
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff0000[Dungeon Challenge]|r Time is up! "
                        "You can still finish the dungeon, but without time bonus.");
                }
            }
        }
        else if (shouldAnnounce && remaining > 0)
        {
            std::string color = remaining > 300 ? "|cffffff00" : remaining > 60 ? "|cffff8000" : "|cffff0000";

            Map::PlayerList const& players = map->GetPlayers();
            for (auto const& ref : players)
            {
                if (Player* player = ref.GetSource())
                {
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cff00ff00[Dungeon Challenge]|r Time remaining: {}{}:{:02}|r "
                        "(Penalty: +{}s) | Bosses: {}/{} | Deaths: {}",
                        color, remaining / 60, remaining % 60,
                        run->penaltyTime, run->bossesKilled, run->totalBosses, run->deathCount);
                }
            }
        }

        lastAnnounce[instanceId] = remaining;
    }

    void OnDestroyInstance(MapInstanced* /*mapInstanced*/, Map* map) override
    {
        if (!map || !map->IsDungeon())
            return;

        // Clean up challenge run when instance is destroyed
        sDungeonChallengeMgr->RemoveChallengeRun(map->GetInstanceId());
    }

private:
    void HandleCountdown(ChallengeRun* run, Map* map, uint32 diff)
    {
        // Track countdown in milliseconds
        static std::unordered_map<uint32, uint32> countdownMs;
        uint32 instanceId = map->GetInstanceId();

        countdownMs[instanceId] += diff;

        if (countdownMs[instanceId] >= 1000)
        {
            countdownMs[instanceId] -= 1000;

            if (run->countdownTimer > 0)
            {
                // Announce countdown at key moments
                if (run->countdownTimer <= 5 || run->countdownTimer == 10)
                {
                    Map::PlayerList const& players = map->GetPlayers();
                    for (auto const& ref : players)
                    {
                        if (Player* player = ref.GetSource())
                        {
                            ChatHandler(player->GetSession()).PSendSysMessage(
                                "|cff00ff00[Dungeon Challenge]|r Challenge starts in |cffff8000{}|r...",
                                run->countdownTimer);
                        }
                    }
                }

                run->countdownTimer--;
            }
            else
            {
                // Countdown finished - start the run!
                sDungeonChallengeMgr->StartRun(run);
                countdownMs.erase(instanceId);

                Map::PlayerList const& players = map->GetPlayers();
                for (auto const& ref : players)
                {
                    if (Player* player = ref.GetSource())
                    {
                        DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(run->mapId);
                        std::string dungeonName = info ? info->name : "Unknown";
                        ChatHandler(player->GetSession()).PSendSysMessage(
                            "|cff00ff00[Dungeon Challenge]|r |cffff8000GO!|r "
                            "|cffff8000{}|r Level |cffff8000{}|r - Timer running! "
                            "({} minutes, {} bosses)",
                            dungeonName, run->difficulty, run->timerDuration / 60, run->totalBosses);
                    }
                }
            }
        }
    }
};

// ============================================================================
// Lightning Sphere (run affix, NPC 500100): rooted and passive; every 1.2 s it
// pulses 1.5-5.25 % of each player's maximum health to every player within
// 100 yd until it is killed or despawns after 60 s (after silviu20092's
// mod-mythic-plus, MIT).
// ============================================================================

class npc_dc_lightning_sphere : public CreatureScript
{
public:
    npc_dc_lightning_sphere() : CreatureScript("npc_dc_lightning_sphere") { }

    struct npc_dc_lightning_sphereAI : public ScriptedAI
    {
        explicit npc_dc_lightning_sphereAI(Creature* creature) : ScriptedAI(creature) { }

        uint32 pulseTimer = 1200;

        void Reset() override
        {
            me->SetReactState(REACT_PASSIVE);
            me->SetControlled(true, UNIT_STATE_ROOT);
        }

        void UpdateAI(uint32 diff) override
        {
            if (pulseTimer > diff)
            {
                pulseTimer -= diff;
                return;
            }
            pulseTimer = 1200;

            ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(me->GetInstanceId());
            if (!run || run->state != CHALLENGE_STATE_RUNNING)
                return;

            std::list<Player*> players;
            Acore::AnyPlayerInObjectRangeCheck check(me, 100.0f);
            Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(me, players, check);
            Cell::VisitObjects(me, searcher, 100.0f);

            me->CastSpell(me, SPELL_LIGHTNING_VISUAL, true);
            for (Player* player : players)
            {
                if (!player->IsAlive() || player->IsGameMaster())
                    continue;
                uint32 damage = uint32(frand(1.5f, 5.25f) / 100.0f * player->GetMaxHealth());
                Unit::DealDamage(me, player, damage, nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NATURE, nullptr, false, true);
                me->SendAttackStateUpdate(HITINFO_NO_ANIMATION, player, 1, SPELL_SCHOOL_MASK_NATURE, damage, 0, 0, VICTIMSTATE_HIT, 0);
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_dc_lightning_sphereAI(creature);
    }
};

// ============================================================================
// Command script: .dchallenge
// ============================================================================

class DungeonChallengeCommandScript : public CommandScript
{
public:
    DungeonChallengeCommandScript() : CommandScript("DungeonChallengeCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable dchallengeTable =
        {
            { "affixes", HandleAffixes, SEC_PLAYER,        Console::Yes },
            { "status",  HandleStatus,  SEC_GAMEMASTER,    Console::No  },
            { "reload",  HandleReload,  SEC_ADMINISTRATOR, Console::Yes },
            { "scale",   HandleScale,   SEC_ADMINISTRATOR, Console::Yes },
            { "week",    HandleWeek,    SEC_ADMINISTRATOR, Console::Yes },
            { "force",   HandleForce,   SEC_ADMINISTRATOR, Console::No  },
            { "start",   HandleStart,   SEC_ADMINISTRATOR, Console::No  },
            { "forget",  HandleForget,  SEC_ADMINISTRATOR, Console::No  },
        };
        static ChatCommandTable commandTable =
        {
            { "dchallenge", dchallengeTable },
        };
        return commandTable;
    }

    static std::string AffixNames(std::vector<DungeonChallengeAffix> const& affixes)
    {
        std::string text;
        for (DungeonChallengeAffix affix : affixes)
        {
            AffixInfo const* info = sDungeonChallengeMgr->GetAffixInfo(affix);
            if (!text.empty())
                text += ", ";
            text += info ? info->name : fmt::format("#{}", uint32(affix));
        }
        return text.empty() ? "none" : text;
    }

    // This week's rotation and what a run at the given level gets.
    static bool HandleAffixes(ChatHandler* handler, Optional<uint32> level)
    {
        uint32 week = sDungeonChallengeMgr->GetCurrentWeek();
        handler->PSendSysMessage("Dungeon Challenge week {}: weekly affixes: {}", week,
            AffixNames(sDungeonChallengeMgr->GetWeeklyAffixes(week)));
        if (level)
            handler->PSendSysMessage("Level {}: {}", *level, AffixNames(sDungeonChallengeMgr->GetAffixesForDifficulty(*level, week)));
        return true;
    }

    // The run of the GM's instance, and the scaling of the selected creature.
    static bool HandleStatus(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(player->GetInstanceId());
        if (!run || !player->GetMap()->IsDungeon())
        {
            handler->SendSysMessage("No Dungeon Challenge run in this instance.");
            return true;
        }

        handler->PSendSysMessage("Run: map {} level {} state {}{} | elapsed {} + penalty {}s of {} | bosses {}/{} | deaths {}",
            run->mapId, run->difficulty, uint32(run->state), run->restored ? " (restored)" : "",
            FormatDuration(run->elapsedTime), run->penaltyTime, FormatDuration(run->timerDuration),
            run->bossesKilled, run->totalBosses, run->deathCount);
        handler->PSendSysMessage("Affixes (week {}): {}", run->affixWeek, AffixNames(run->affixes));
        if (!run->lastBossName.empty())
            handler->PSendSysMessage("Last boss: {} - fight {}", run->lastBossName, FormatDuration(run->lastBossCombatTime));
        DungeonScale const& scale = sDungeonChallengeMgr->GetScale(run->mapId);
        handler->PSendSysMessage("Dungeon factors: trash health {:.2f} damage {:.2f} | boss health {:.2f} damage {:.2f} | uplift {}",
            scale.trashHealth, scale.trashDamage, scale.bossHealth, scale.bossDamage, uint32(scale.upliftLevel));

        if (Creature* target = handler->getSelectedCreature())
        {
            auto* data = target->CustomData.Get<CreatureChallengeData>("mod-dungeon-challenge");
            handler->PSendSysMessage("Target {} ({}): boss {} | level {} | health factor {:.2f} | damage factor {:.2f} | max health {} | affixes {}",
                target->GetName(), target->GetEntry(), sDungeonChallengeMgr->IsChallengeBoss(target) ? "yes" : "no",
                uint32(target->GetLevel()), data ? data->healthMultiplier : 1.0f, data ? data->extraDamageMultiplier : 1.0f,
                target->GetMaxHealth(), data ? AffixNames(data->affixes) : "none");
        }
        return true;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        sDungeonChallengeMgr->LoadTuningTables();
        handler->SendSysMessage("Dungeon Challenge: affixes, dungeon factors, rewards and boss tables reloaded (runs in progress keep their affixes).");
        return true;
    }

    // Live tuning of a dungeon's factors, until the next reload or restart.
    static bool HandleScale(ChatHandler* handler, uint32 mapId, float trashHealth, float trashDamage, float bossHealth, float bossDamage)
    {
        DungeonScale scale = sDungeonChallengeMgr->GetScale(mapId);
        scale.trashHealth = std::max(0.01f, trashHealth);
        scale.trashDamage = std::max(0.0f, trashDamage);
        scale.bossHealth  = std::max(0.01f, bossHealth);
        scale.bossDamage  = std::max(0.0f, bossDamage);
        sDungeonChallengeMgr->SetScale(mapId, scale);
        handler->PSendSysMessage("Dungeon Challenge map {}: trash health {:.2f} damage {:.2f}, boss health {:.2f} damage {:.2f} until the next reload (new runs and newly scaled creatures).",
            mapId, scale.trashHealth, scale.trashDamage, scale.bossHealth, scale.bossDamage);
        return true;
    }

    // The next roll of a run affix in the GM's run fires now (tests, demos).
    static bool HandleForce(ChatHandler* handler, uint32 affixId)
    {
        Player* player = handler->GetSession()->GetPlayer();
        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(player->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
        {
            handler->SendSysMessage("No running Dungeon Challenge run in this instance.");
            return true;
        }
        DungeonChallengeAffix affix = DungeonChallengeAffix(affixId);
        AffixInfo const* info = sDungeonChallengeMgr->GetAffixInfo(affix);
        if (!info || info->kind != AFFIX_KIND_RUN || !run->HasAffix(affix))
        {
            handler->PSendSysMessage("Affix {} is not a run affix of this run.", affixId);
            return true;
        }
        run->forcedAffix = affix;
        handler->PSendSysMessage("Forced: {} fires at its next check.", info->name);
        return true;
    }

    // Starts a run for the GM (and the GM's group) as the NPC does: heroic,
    // teleport to the entrance, the run begins on entry.
    static bool HandleStart(ChatHandler* handler, uint32 mapId, uint32 level)
    {
        Player* player = handler->GetSession()->GetPlayer();
        DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(mapId);
        if (!info || !level || level > sDungeonChallengeMgr->GetMaxDifficulty())
        {
            handler->SendSysMessage("Unknown or disabled dungeon, or a level out of range.");
            return true;
        }
        Group* group = player->GetGroup();
        if (group && group->GetMembersCount() > 5)
        {
            handler->SendSysMessage("Maximum of 5 players.");
            return true;
        }

        if (group)
            group->SetDungeonDifficulty(DUNGEON_DIFFICULTY_HEROIC);
        else
            player->SetDungeonDifficulty(DUNGEON_DIFFICULTY_HEROIC);
        player->SendDungeonDifficulty(group != nullptr);

        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    member->TeleportTo(mapId, info->entranceX, info->entranceY, info->entranceZ, info->entranceO);
        }
        else
            player->TeleportTo(mapId, info->entranceX, info->entranceY, info->entranceZ, info->entranceO);

        sDungeonChallengePending->AddPending(player->GetGUID(), mapId, level);
        handler->PSendSysMessage("Dungeon Challenge {} level {} starting.", info->name, level);
        return true;
    }

    // Drops the run of the GM's instance from memory and keeps its saved row,
    // as a restart does: the next participant to enter brings it back.
    static bool HandleForget(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(player->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
        {
            handler->SendSysMessage("No running Dungeon Challenge run in this instance.");
            return true;
        }
        sDungeonChallengeMgr->SaveActiveRun(run);
        sDungeonChallengeMgr->RemoveChallengeRun(player->GetInstanceId());
        handler->SendSysMessage("Run forgotten in memory; its saved row stays.");
        return true;
    }

    // Pin the rotation to a week number for previews and tests; 0 = the real
    // week again. Not saved; new runs take the pinned week's affixes.
    static bool HandleWeek(ChatHandler* handler, uint32 week)
    {
        sDungeonChallengeMgr->SetWeekPin(week);
        uint32 current = sDungeonChallengeMgr->GetCurrentWeek();
        handler->PSendSysMessage("Dungeon Challenge rotation week {}{}: weekly affixes: {}", current,
            week ? " (pinned)" : "", AffixNames(sDungeonChallengeMgr->GetWeeklyAffixes(current)));
        return true;
    }
};

// ============================================================================
// Registration
// ============================================================================

void AddSC_dungeon_challenge_scripts()
{
    new npc_dc_lightning_sphere();
    new DungeonChallengeCommandScript();
    new DungeonChallengeWorldScript();
    new DungeonChallengePlayerScript();
    new DungeonChallengeCreatureScript();
    new DungeonChallengeCreatureDeathScript();
    new DungeonChallengeTimerScript();
    new DungeonChallengeUnitScript();
}
