#include "DungeonChallenge.h"
#include "Group.h"
#include "MapMgr.h"
#include "GameTime.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"

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
        sDungeonChallengeMgr->LoadAffixData();
        sDungeonChallengeMgr->LoadLeaderboard();
        sDungeonChallengeMgr->LoadSpellOverrides();
        sDungeonChallengeMgr->LoadSnapshots();

        // Clean up stale pending challenges from previous sessions
        CharacterDatabase.DirectExecute(
            "DELETE FROM dungeon_challenge_pending WHERE created_at < DATE_SUB(NOW(), INTERVAL 5 MINUTE)");
    }
};

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
            return;

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

        // Create the challenge run
        ChallengeRun* run = sDungeonChallengeMgr->CreateChallengeRun(
            instanceId, pending->mapId, pending->difficulty, player);

        if (!run)
        {
            if (!fromDb)
                sDungeonChallengePending->RemovePending(player->GetGUID());
            return;
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
                "~10%% of mobs have ALL available affixes!",
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
    }

private:
    void FailRunIfPlayerLeft(Player* player)
    {
        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRunByParticipant(player->GetGUID());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        // If the player is still in the challenge dungeon instance, do nothing
        Map* map = player->GetMap();
        if (map && map->IsDungeon() && map->GetInstanceId() == run->instanceId)
            return;

        // Player left the dungeon — fail the run
        sDungeonChallengeMgr->FailRun(run);

        DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(run->mapId);
        std::string dungeonName = info ? info->name : "Unknown";

        // Notify all remaining participants
        for (auto const& guid : run->participants)
        {
            if (Player* p = ObjectAccessor::FindPlayer(guid))
            {
                ChatHandler(p->GetSession()).PSendSysMessage(
                    "|cffff0000[Dungeon Challenge]|r |cff69ccf0{}|r left the dungeon! "
                    "Challenge |cffff8000{}|r Level |cffff8000{}|r has been ended.",
                    player->GetName(), dungeonName, run->difficulty);
            }
        }
    }
};

// ============================================================================
// AllCreatureScript - Creature Processing & Affix Behavior
// ============================================================================

class DungeonChallengeCreatureScript : public AllCreatureScript
{
public:
    DungeonChallengeCreatureScript() : AllCreatureScript("DungeonChallengeCreatureScript") { }

    void OnAllCreatureUpdate(Creature* creature, uint32 diff) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        Map* map = creature->GetMap();
        if (!map || !map->IsDungeon())
            return;

        // Process unprocessed creatures via DataMap
        auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
        if (!creatureData->processed)
        {
            sDungeonChallengeMgr->ProcessCreature(creature, map);
        }

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        // --- MOB RESPAWN: non-boss, non-affix mobs respawn at original position when pulled ---
        if (creature->IsAlive() && creature->IsInCombat()
            && !creatureData->isRespawnCopy
            && !creatureData->combatTracked
            && creatureData->affixes.empty())
        {
            // Skip bosses
            bool isBoss = creature->GetCreatureTemplate()->rank >= 3 || creature->isWorldBoss();
            if (!isBoss)
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

        // --- CALL FOR HELP: pull allies within 30y when entering combat ---
        if (creatureData->HasAffix(AFFIX_CALL_FOR_HELP) && creature->IsInCombat() && !creatureData->hasCalled)
        {
            creatureData->hasCalled = true;
            Unit* victim = creature->GetVictim();
            if (victim)
            {
                for (auto const& pair : map->GetCreatureBySpawnIdStore())
                {
                    Creature* ally = pair.second;
                    if (!ally || !ally->IsAlive() || ally == creature || ally->IsInCombat())
                        continue;
                    if (ally->IsPet() || ally->IsSummon() || ally->IsTotem())
                        continue;
                    if (ally->GetCreatureTemplate()->faction == 35)
                        continue;
                    if (ally->GetDistance(creature) > 30.0f)
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
                uint32 damage = run->difficulty * 80;

                Map::PlayerList const& players = map->GetPlayers();
                for (auto const& ref : players)
                {
                    if (Player* player = ref.GetSource())
                    {
                        if (player->IsAlive() && creature->GetDistance(player) <= 8.0f)
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

    void OnPlayerCreatureKill(Player* /*killer*/, Creature* creature) override
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
                sDungeonChallengeMgr->LockInstanceAsNonChallenge(map);

                Map::PlayerList const& players = map->GetPlayers();
                for (auto const& ref : players)
                {
                    if (Player* player = ref.GetSource())
                    {
                        ChatHandler(player->GetSession()).PSendSysMessage(
                            "|cffff0000[Dungeon Challenge]|r Creature killed without active challenge! "
                            "This instance can no longer be used for a challenge.");
                    }
                }
            }
            return;
        }

        if (run->state != CHALLENGE_STATE_RUNNING)
            return;

        // Check if this was a boss (rank >= 3)
        if (creature->GetCreatureTemplate()->rank >= 3 || creature->isWorldBoss())
        {
            run->bossesKilled++;
            bool isFinalBoss = run->AllBossesKilled();

            DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(run->mapId);
            std::string bossName = creature->GetName();

            Map::PlayerList const& players = map->GetPlayers();
            for (auto const& ref : players)
            {
                if (Player* player = ref.GetSource())
                {
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cff00ff00[Dungeon Challenge]|r Boss |cffff8000{}|r defeated! ({}/{})",
                        bossName, run->bossesKilled, run->totalBosses);
                }
            }

            // Save boss kill snapshot
            sDungeonChallengeMgr->SaveBossKillSnapshot(run, creature, isFinalBoss, isFinalBoss && !run->IsTimedOut());

            // Check completion
            if (isFinalBoss)
            {
                sDungeonChallengeMgr->CompleteRun(run);
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
            HandleLilBroDeath(creature, run, map);
        }
    }

private:
    void HandleLilBroDeath(Creature* creature, ChallengeRun* run, Map* map)
    {
        auto* parentData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
        uint8 nextGeneration = parentData->lilBroGeneration + 1;

        uint32 entry = creature->GetEntry();
        float x = creature->GetPositionX();
        float y = creature->GetPositionY();
        float z = creature->GetPositionZ();
        float o = creature->GetOrientation();

        // Calculate HP for copies: 10% of the creature's max health
        uint32 copyHealth = std::max(1u, static_cast<uint32>(creature->GetMaxHealth() * 0.1f));

        for (uint32 i = 0; i < 2; ++i)
        {
            float offsetX = (i == 0) ? 2.0f : -2.0f;
            float offsetY = (i == 0) ? 1.0f : -1.0f;

            Creature* copy = creature->SummonCreature(entry, x + offsetX, y + offsetY, z, o,
                TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60000);
            if (!copy)
                continue;

            copy->SetMaxHealth(copyHealth);
            copy->SetFullHealth();

            auto* copyData = copy->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
            copyData->lilBroGeneration = nextGeneration;
            copyData->affixes = parentData->affixes;
            copyData->processed = true;

            // Apply difficulty damage scaling
            copyData->extraDamageMultiplier = sDungeonChallengeMgr->GetDamageMultiplier(run->difficulty);

            // Only original (generation 0) drops loot; all splits don't
            copyData->noLoot = true;

            if (Unit* victim = creature->GetVictim())
                copy->AI()->AttackStart(victim);
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
                    playerTarget->EnvironmentalDamage(DAMAGE_FIRE, 666);
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
                    playerTarget->EnvironmentalDamage(DAMAGE_FIRE, 666);
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

        auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");

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

        if (run->state != CHALLENGE_STATE_RUNNING)
            return;

        // Update elapsed time
        sDungeonChallengeMgr->UpdateRun(run, diff);

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
// Registration
// ============================================================================

void AddSC_dungeon_challenge_scripts()
{
    new DungeonChallengeWorldScript();
    new DungeonChallengePlayerScript();
    new DungeonChallengeCreatureScript();
    new DungeonChallengeCreatureDeathScript();
    new DungeonChallengeTimerScript();
    new DungeonChallengeUnitScript();
}
