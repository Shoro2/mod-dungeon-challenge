#include "DungeonChallenge.h"
#include "Group.h"
#include "MapMgr.h"
#include "GameTime.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"

// ============================================================================
// WorldScript - Configuration Loading & Timer Updates
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
    }

    void OnWorldUpdate(uint32 diff) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        // Timer update handled per-instance in PlayerScript/InstanceScript
        // This is a fallback for global state management if needed
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
            "|cff00ff00[Dungeon Challenge]|r Modul aktiv! Sprich mit dem "
            "|cffff8000Dungeon Challenge NPC|r um eine Herausforderung zu starten.");
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        Map* map = player->GetMap();
        if (!map || !map->IsDungeon())
            return;

        // Check if this player's group leader has a pending challenge
        Group* group = player->GetGroup();
        if (!group)
            return;

        ObjectGuid leaderGuid = group->GetLeaderGUID();
        PendingChallengeInfo const* pending = sDungeonChallengePending->GetPending(leaderGuid);
        if (!pending)
            return;

        // Only process once: when the leader enters the instance
        if (player->GetGUID() != leaderGuid)
            return;

        uint32 instanceId = player->GetInstanceId();

        // Create the challenge run
        ChallengeRun* run = sDungeonChallengeMgr->CreateChallengeRun(
            instanceId, pending->mapId, pending->difficulty, player);

        if (!run)
        {
            sDungeonChallengePending->RemovePending(leaderGuid);
            return;
        }

        // Scale all creatures and assign affixes
        sDungeonChallengeMgr->AssignAffixesToCreatures(run, map);

        // Start the run
        sDungeonChallengeMgr->StartRun(run);

        // Notify all group members
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->Next())
        {
            if (Player* member = ref->GetSource())
            {
                DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(run->mapId);
                std::string dungeonName = info ? info->name : "Unbekannt";

                ChatHandler(member->GetSession()).PSendSysMessage(
                    "|cff00ff00[Dungeon Challenge]|r |cffff8000{}|r Stufe |cffff8000{}|r gestartet! "
                    "Timer: |cffffff00{} Minuten|r | Bosse: |cff00ff00{}|r | "
                    "~5%% der Mobs haben zufaellige Affixe!",
                    dungeonName, run->difficulty, run->timerDuration / 60, run->totalBosses);
            }
        }

        // Remove pending challenge
        sDungeonChallengePending->RemovePending(leaderGuid);
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

        // Add time penalty: +5 seconds per death
        run->timerDuration = std::max(0u, run->timerDuration - 5);

        // Notify group
        Group* group = player->GetGroup();
        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->Next())
            {
                if (Player* member = ref->GetSource())
                {
                    ChatHandler(member->GetSession()).PSendSysMessage(
                        "|cffff0000[Dungeon Challenge]|r |cff69ccf0{}|r ist gestorben! "
                        "(Tode: {}, -5s Timer-Strafe)",
                        player->GetName(), run->deathCount);
                }
            }
        }

        // Check if all players are dead -> fail
        bool allDead = true;
        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->Next())
            {
                if (Player* member = ref->GetSource())
                {
                    if (member->IsAlive() && member->GetMap() == map)
                    {
                        allDead = false;
                        break;
                    }
                }
            }
        }

        if (allDead)
        {
            // Don't fail immediately - give resurrection chance
            // Will fail when all leave the instance
        }
    }
};

// ============================================================================
// CreatureScript - Affix Behavior Hooks
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

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        // Update timer
        sDungeonChallengeMgr->UpdateRun(run, diff);

        // Handle RAGING affix: enrage below 30% HP
        auto affixIt = run->creatureAffixes.find(creature->GetGUID());
        if (affixIt != run->creatureAffixes.end())
        {
            if (affixIt->second == AFFIX_RAGING && creature->IsAlive())
            {
                bool isLowHp = creature->GetHealthPct() < 30.0f;
                bool hasEnrage = creature->HasAura(SPELL_ENRAGE_VISUAL);

                if (isLowHp && !hasEnrage)
                {
                    // Apply visual enrage
                    creature->AddAura(SPELL_ENRAGE_VISUAL, creature);

                    // Boost damage by 50%
                    float baseDmgMin = creature->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
                    float baseDmgMax = creature->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);
                    creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, baseDmgMin * 1.5f);
                    creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, baseDmgMax * 1.5f);
                    creature->UpdateDamagePhysical(BASE_ATTACK);

                    // Announce
                    Map::PlayerList const& players = map->GetPlayers();
                    for (auto const& ref : players)
                    {
                        if (Player* player = ref.GetSource())
                        {
                            ChatHandler(player->GetSession()).PSendSysMessage(
                                "|cffff8000[Affix: Raging]|r |cffff0000{}|r wird wuetend! (+50%% Schaden)",
                                creature->GetName());
                        }
                    }
                }
            }
        }
    }

    // Spell IDs used for visual effects
    static constexpr uint32 SPELL_ENRAGE_VISUAL = 8599; // Enrage (visual)

    void OnCreatureKill(Creature* killer, Player* /*victim*/) override
    {
        // Handled in PlayerScript::OnPlayerJustDied
    }
};

// ============================================================================
// AllCreatureScript - Death Hook for Affix Processing
// ============================================================================

class DungeonChallengeCreatureDeathScript : public AllCreatureScript
{
public:
    DungeonChallengeCreatureDeathScript() : AllCreatureScript("DungeonChallengeCreatureDeathScript") { }

    void OnAllCreatureJustDied(Creature* creature) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        Map* map = creature->GetMap();
        if (!map || !map->IsDungeon())
            return;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        // Check if this was a boss (rank >= 3)
        if (creature->GetCreatureTemplate()->rank >= 3 || creature->IsWorldBoss())
        {
            run->bossesKilled++;

            DungeonInfo const* info = sDungeonChallengeMgr->GetDungeonInfo(run->mapId);
            std::string bossName = creature->GetName();

            Map::PlayerList const& players = map->GetPlayers();
            for (auto const& ref : players)
            {
                if (Player* player = ref.GetSource())
                {
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cff00ff00[Dungeon Challenge]|r Boss |cffff8000{}|r besiegt! ({}/{})",
                        bossName, run->bossesKilled, run->totalBosses);
                }
            }

            // Check completion
            if (run->AllBossesKilled())
            {
                sDungeonChallengeMgr->CompleteRun(run);
            }
        }

        // Process affix on-death effects
        auto affixIt = run->creatureAffixes.find(creature->GetGUID());
        if (affixIt == run->creatureAffixes.end())
            return;

        DungeonChallengeAffix affix = affixIt->second;

        switch (affix)
        {
            case AFFIX_BOLSTERING:
                HandleBolsteringDeath(creature, run, map);
                break;
            case AFFIX_BURSTING:
                HandleBurstingDeath(creature, run, map);
                break;
            case AFFIX_SANGUINE:
                HandleSanguineDeath(creature, run, map);
                break;
            default:
                break;
        }
    }

private:
    void HandleBolsteringDeath(Creature* creature, ChallengeRun* run, Map* map)
    {
        // Buff nearby allies: +20% HP and damage
        float range = 15.0f;

        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* ally = pair.second;
            if (!ally || !ally->IsAlive() || ally == creature)
                continue;
            if (ally->GetDistance(creature) > range)
                continue;
            if (ally->IsPet() || ally->IsSummon())
                continue;
            if (ally->GetCreatureTemplate()->faction == 35) // friendly
                continue;

            ally->SetMaxHealth(ally->GetMaxHealth() * 1.2f);
            ally->SetFullHealth();
            ally->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE,
                ally->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE) * 1.2f);
            ally->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE,
                ally->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE) * 1.2f);
            ally->UpdateDamagePhysical(BASE_ATTACK);
        }

        Map::PlayerList const& players = map->GetPlayers();
        for (auto const& ref : players)
        {
            if (Player* player = ref.GetSource())
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff8000[Affix: Bolstering]|r Nahestehende Mobs wurden verstaerkt! (+20%%)");
            }
        }
    }

    void HandleBurstingDeath(Creature* creature, ChallengeRun* run, Map* map)
    {
        // AoE damage to all players: 5% of max HP
        Map::PlayerList const& players = map->GetPlayers();
        for (auto const& ref : players)
        {
            if (Player* player = ref.GetSource())
            {
                if (!player->IsAlive())
                    continue;

                int32 damage = player->GetMaxHealth() * 0.05f;
                player->EnvironmentalDamage(DAMAGE_FIRE, damage);

                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff8000[Affix: Bursting]|r AoE-Schaden: |cffff0000-{}|r HP!",
                    damage);
            }
        }
    }

    void HandleSanguineDeath(Creature* creature, ChallengeRun* run, Map* map)
    {
        // Heal nearby mobs for 5% of their max HP per second for 8 seconds
        // Simplified: instant heal of 20% to nearby mobs
        float range = 8.0f;

        for (auto const& pair : map->GetCreatureBySpawnIdStore())
        {
            Creature* ally = pair.second;
            if (!ally || !ally->IsAlive() || ally == creature)
                continue;
            if (ally->GetDistance(creature) > range)
                continue;
            if (ally->IsPet() || ally->IsSummon())
                continue;

            int32 healAmount = ally->GetMaxHealth() * 0.2f;
            ally->ModifyHealth(healAmount);
        }

        Map::PlayerList const& players = map->GetPlayers();
        for (auto const& ref : players)
        {
            if (Player* player = ref.GetSource())
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff8000[Affix: Sanguine]|r Heilende Zone! Nahestehende Mobs werden geheilt!");
            }
        }
    }
};

// ============================================================================
// Timer Display Script (periodic announcement)
// ============================================================================

class DungeonChallengeTimerScript : public AllMapScript
{
public:
    DungeonChallengeTimerScript() : AllMapScript("DungeonChallengeTimerScript") { }

    void OnMapUpdate(Map* map, uint32 /*diff*/) override
    {
        if (!sDungeonChallengeMgr->IsEnabled())
            return;

        if (!map || !map->IsDungeon())
            return;

        ChallengeRun* run = sDungeonChallengeMgr->GetChallengeRun(map->GetInstanceId());
        if (!run || run->state != CHALLENGE_STATE_RUNNING)
            return;

        // Update elapsed time
        sDungeonChallengeMgr->UpdateRun(run, 0);

        // Periodic timer announcements (every 60 seconds, and at 30s, 10s, 5s marks)
        static std::unordered_map<uint32, uint32> lastAnnounce;
        uint32 instanceId = map->GetInstanceId();
        uint32 remaining = run->timerDuration > run->elapsedTime ?
            run->timerDuration - run->elapsedTime : 0;

        uint32 lastTime = lastAnnounce[instanceId];
        bool shouldAnnounce = false;

        // Announce at specific remaining time milestones
        if (remaining <= 10 && lastTime > 10)
            shouldAnnounce = true;
        else if (remaining <= 30 && lastTime > 30)
            shouldAnnounce = true;
        else if (remaining <= 60 && lastTime > 60)
            shouldAnnounce = true;
        else if (remaining <= 300 && lastTime > 300) // 5 minutes
            shouldAnnounce = true;
        else if (lastTime > 0 && (lastTime / 60 != remaining / 60) && remaining % 60 == 0)
            shouldAnnounce = true; // Every minute change

        // Also announce when time runs out
        if (remaining == 0 && lastTime > 0)
        {
            Map::PlayerList const& players = map->GetPlayers();
            for (auto const& ref : players)
            {
                if (Player* player = ref.GetSource())
                {
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff0000[Dungeon Challenge]|r Die Zeit ist abgelaufen! "
                        "Ihr koennt den Dungeon noch abschliessen, aber ohne Zeitbonus.");
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
                        "|cff00ff00[Dungeon Challenge]|r Verbleibende Zeit: {}{}:{:02}|r | Bosse: {}/{} | Tode: {}",
                        color, remaining / 60, remaining % 60,
                        run->bossesKilled, run->totalBosses, run->deathCount);
                }
            }
        }

        lastAnnounce[instanceId] = remaining;
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
}
