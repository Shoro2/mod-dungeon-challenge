#include "DungeonChallenge.h"
#include "ParagonUtils.h"
#include "Log.h"
#include "World.h"
#include "Group.h"
#include "MapMgr.h"
#include "GameTime.h"

// ============================================================================
// Singleton
// ============================================================================

DungeonChallengeMgr* DungeonChallengeMgr::Instance()
{
    static DungeonChallengeMgr instance;
    return &instance;
}

DungeonChallengeMgr::DungeonChallengeMgr()
    : _enabled(true)
    , _affixPercentage(15)
    , _maxDifficulty(100)
    , _healthMultPerLevel(0.15f)
    , _damageMultPerLevel(0.08f)
    , _timerBaseMinutes(30)
    , _lootBonusPerLevel(50000)
    , _npcEntry(500000)
    , _announceOnLogin(true)
    , _deathPenaltySeconds(15)
    , _gameObjectEntry(CHALLENGE_GO_ENTRY)
    , _paragonXPPerLevel(10)
    , _rng(std::random_device{}())
{
}

// ============================================================================
// Configuration
// ============================================================================

void DungeonChallengeMgr::LoadConfig(bool /*reload*/)
{
    _enabled = sConfigMgr->GetOption<bool>("DungeonChallenge.Enable", true);
    _affixPercentage = sConfigMgr->GetOption<uint32>("DungeonChallenge.AffixPercentage", 15);
    _maxDifficulty = sConfigMgr->GetOption<uint32>("DungeonChallenge.MaxDifficulty", 100);
    _healthMultPerLevel = sConfigMgr->GetOption<float>("DungeonChallenge.HealthMultiplierPerLevel", 15.0f) / 100.0f;
    _damageMultPerLevel = sConfigMgr->GetOption<float>("DungeonChallenge.DamageMultiplierPerLevel", 8.0f) / 100.0f;
    _timerBaseMinutes = sConfigMgr->GetOption<uint32>("DungeonChallenge.TimerBaseMinutes", 30);
    _lootBonusPerLevel = sConfigMgr->GetOption<uint32>("DungeonChallenge.LootBonusPerLevel", 50000);
    _npcEntry = sConfigMgr->GetOption<uint32>("DungeonChallenge.NpcEntry", 500000);
    _announceOnLogin = sConfigMgr->GetOption<bool>("DungeonChallenge.AnnounceOnLogin", true);
    _deathPenaltySeconds = sConfigMgr->GetOption<uint32>("DungeonChallenge.DeathPenaltySeconds", 15);
    _gameObjectEntry = sConfigMgr->GetOption<uint32>("DungeonChallenge.GameObjectEntry", CHALLENGE_GO_ENTRY);
    _paragonXPPerLevel = sConfigMgr->GetOption<uint32>("DungeonChallenge.ParagonXPPerLevel", 10);

    LOG_INFO("module", ">> mod-dungeon-challenge: Configuration loaded (Enabled: {}, MaxDifficulty: {}, AffixPct: {}%, ParagonXP/Level: {})",
        _enabled ? "Yes" : "No", _maxDifficulty, _affixPercentage, _paragonXPPerLevel);
}

void DungeonChallengeMgr::LoadDungeonData()
{
    _dungeons.clear();

    QueryResult result = WorldDatabase.Query("SELECT map_id, name, entrance_x, entrance_y, entrance_z, entrance_o, timer_minutes, boss_count FROM dungeon_challenge_dungeons");
    if (!result)
    {
        LOG_WARN("module", ">> mod-dungeon-challenge: No dungeon data found in dungeon_challenge_dungeons table. Using defaults.");

        // Default WotLK dungeons
        _dungeons = {
            { 574, "Utgarde Keep",              1.31f,  -16.57f, 15.21f, 0.0f, 25, 3 },
            { 575, "Utgarde Pinnacle",          0.0f,   0.0f,    0.0f,   0.0f, 28, 4 },
            { 576, "The Nexus",                 0.0f,   0.0f,    0.0f,   0.0f, 28, 4 },
            { 578, "The Oculus",                0.0f,   0.0f,    0.0f,   0.0f, 35, 4 },
            { 595, "Culling of Stratholme",     0.0f,   0.0f,    0.0f,   0.0f, 30, 5 },
            { 599, "Halls of Stone",            0.0f,   0.0f,    0.0f,   0.0f, 28, 3 },
            { 600, "Drak'Tharon Keep",          0.0f,   0.0f,    0.0f,   0.0f, 25, 4 },
            { 601, "Azjol-Nerub",               0.0f,   0.0f,    0.0f,   0.0f, 20, 3 },
            { 602, "Halls of Lightning",        0.0f,   0.0f,    0.0f,   0.0f, 28, 4 },
            { 604, "Gundrak",                   0.0f,   0.0f,    0.0f,   0.0f, 28, 4 },
            { 608, "Violet Hold",               0.0f,   0.0f,    0.0f,   0.0f, 25, 3 },
            { 619, "Ahn'kahet: The Old Kingdom", 0.0f,  0.0f,    0.0f,   0.0f, 30, 5 },
            { 632, "The Forge of Souls",        0.0f,   0.0f,    0.0f,   0.0f, 22, 2 },
            { 650, "Trial of the Champion",     0.0f,   0.0f,    0.0f,   0.0f, 25, 3 },
            { 658, "Pit of Saron",              0.0f,   0.0f,    0.0f,   0.0f, 28, 3 },
            { 668, "Halls of Reflection",       0.0f,   0.0f,    0.0f,   0.0f, 25, 2 },
        };

        LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} default dungeons.", _dungeons.size());
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        DungeonInfo info;
        info.mapId         = fields[0].Get<uint32>();
        info.name          = fields[1].Get<std::string>();
        info.entranceX     = fields[2].Get<float>();
        info.entranceY     = fields[3].Get<float>();
        info.entranceZ     = fields[4].Get<float>();
        info.entranceO     = fields[5].Get<float>();
        info.timerMinutes  = fields[6].Get<uint32>();
        info.totalCreatures = fields[7].Get<uint32>();
        _dungeons.push_back(info);
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} dungeons from database.", _dungeons.size());
}

void DungeonChallengeMgr::LoadAffixData()
{
    _affixes.clear();
    // Every 10 levels adds +1 affix to the pool.
    // Selected mobs receive ALL available affixes for the current difficulty.
    _affixes = {
        { AFFIX_CALL_FOR_HELP, "Call for Help", "Calls allies within 30y for help when entering combat.", 10 },
        { AFFIX_SPEEDY,        "Speedy",        "+100% movement speed and +10% attack speed.", 20 },
        { AFFIX_BIG_BOY,       "Big Boy",       "+50% HP and increased size.", 30 },
        { AFFIX_IMMOLATION,    "Immolation Aura", "Deals periodic fire damage (Level x 80) to nearby players.", 40 },
        { AFFIX_CC_IMMUNITY,   "CC Immunity",   "Immune to all crowd control effects.", 50 },
        { AFFIX_HEAVY_HITS,    "Heavy Hits",    "+33% damage dealt.", 60 },
        { AFFIX_LIL_BRO,       "Lil' Bro",      "Splits into 2 copies on death (1->2->4), each with -90% HP.", 70 },
        { AFFIX_DAMAGE_REDUCE, "Damage Reduce", "Allies within 30y take 25% less damage.", 80 },
        { AFFIX_BIGGER_BOY,    "Bigger Boy",    "Additional +50% HP, increased size and +10% damage.", 90 },
        { AFFIX_HELL_TOUCHED,  "Hell Touched",  "Deals 666 hellfire damage on hit. Reduces stats by 10% (10s, stacks 10).", 100 },
    };

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} affixes.", _affixes.size());
}

void DungeonChallengeMgr::LoadLeaderboard()
{
    _leaderboard.clear();

    QueryResult result = CharacterDatabase.Query(
        "SELECT id, map_id, difficulty, completion_time, death_count, leader_name, "
        "date_completed, participants FROM dungeon_challenge_leaderboard "
        "ORDER BY map_id, difficulty, completion_time ASC");

    if (!result)
    {
        LOG_INFO("module", ">> mod-dungeon-challenge: No leaderboard entries found.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        LeaderboardEntry entry;
        entry.rank           = 0; // calculated later
        entry.mapId          = fields[1].Get<uint32>();
        entry.difficulty     = fields[2].Get<uint32>();
        entry.completionTime = fields[3].Get<uint32>();
        entry.deathCount     = fields[4].Get<uint32>();
        entry.leaderName     = fields[5].Get<std::string>();
        entry.dateCompleted  = fields[6].Get<std::string>();

        // Parse participants (comma-separated)
        std::string participants = fields[7].Get<std::string>();
        std::stringstream ss(participants);
        std::string name;
        while (std::getline(ss, name, ','))
            if (!name.empty())
                entry.participants.push_back(name);

        _leaderboard[entry.mapId].push_back(entry);
        ++count;
    } while (result->NextRow());

    // Assign ranks per dungeon
    for (auto& [mapId, entries] : _leaderboard)
    {
        uint32 rank = 1;
        for (auto& entry : entries)
            entry.rank = rank++;
    }

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} leaderboard entries.", count);
}

void DungeonChallengeMgr::LoadSpellOverrides()
{
    _spellOverrides.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT spell_id, map_id, mod_pct, dot_mod_pct FROM dungeon_challenge_spell_override");

    if (!result)
    {
        LOG_INFO("module", ">> mod-dungeon-challenge: No spell overrides found.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        SpellOverrideEntry entry;
        entry.spellId   = fields[0].Get<uint32>();
        entry.mapId     = fields[1].Get<uint32>();
        entry.modPct    = fields[2].Get<float>();
        entry.dotModPct = fields[3].Get<float>();
        _spellOverrides.push_back(entry);
        ++count;
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} spell overrides.", count);
}

void DungeonChallengeMgr::LoadSnapshots()
{
    _snapshots.clear();

    QueryResult result = CharacterDatabase.Query(
        "SELECT id, instance_id, map_id, difficulty, start_time, snap_time, timer_limit, "
        "creature_entry, creature_name, is_final_boss, rewarded, deaths, penalty_time, "
        "player_name, player_guid FROM dungeon_challenge_snapshot "
        "ORDER BY map_id, difficulty, snap_time ASC");

    if (!result)
    {
        LOG_INFO("module", ">> mod-dungeon-challenge: No snapshots found.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        BossKillSnapshot snap;
        snap.id             = fields[0].Get<uint32>();
        snap.instanceId     = fields[1].Get<uint32>();
        snap.mapId          = fields[2].Get<uint32>();
        snap.difficulty     = fields[3].Get<uint32>();
        snap.startTime      = fields[4].Get<uint32>();
        snap.snapTime       = fields[5].Get<uint32>();
        snap.timerLimit     = fields[6].Get<uint32>();
        snap.creatureEntry  = fields[7].Get<uint32>();
        snap.creatureName   = fields[8].Get<std::string>();
        snap.isFinalBoss    = fields[9].Get<bool>();
        snap.rewarded       = fields[10].Get<bool>();
        snap.deaths         = fields[11].Get<uint32>();
        snap.penaltyTime    = fields[12].Get<uint32>();
        snap.playerName     = fields[13].Get<std::string>();
        snap.playerGuid     = fields[14].Get<uint32>();
        _snapshots[snap.mapId].push_back(snap);
        ++count;
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} boss kill snapshots.", count);
}

// ============================================================================
// Getters
// ============================================================================

float DungeonChallengeMgr::GetHealthMultiplier(uint32 difficulty) const
{
    return 1.0f + (_healthMultPerLevel * difficulty);
}

float DungeonChallengeMgr::GetDamageMultiplier(uint32 difficulty) const
{
    return 1.0f + (_damageMultPerLevel * difficulty);
}

uint32 DungeonChallengeMgr::GetTimerForDungeon(uint32 mapId) const
{
    DungeonInfo const* info = GetDungeonInfo(mapId);
    if (info && info->timerMinutes > 0)
        return info->timerMinutes * 60;
    return _timerBaseMinutes * 60;
}

DungeonInfo const* DungeonChallengeMgr::GetDungeonInfo(uint32 mapId) const
{
    for (auto const& d : _dungeons)
        if (d.mapId == mapId)
            return &d;
    return nullptr;
}

bool DungeonChallengeMgr::IsDungeonCapable(uint32 mapId) const
{
    return GetDungeonInfo(mapId) != nullptr;
}

AffixInfo const* DungeonChallengeMgr::GetAffixInfo(DungeonChallengeAffix affix) const
{
    for (auto const& a : _affixes)
        if (a.id == affix)
            return &a;
    return nullptr;
}

std::vector<DungeonChallengeAffix> DungeonChallengeMgr::GetAffixesForDifficulty(uint32 difficulty) const
{
    std::vector<DungeonChallengeAffix> result;
    for (auto const& a : _affixes)
        if (difficulty >= a.minDifficulty)
            result.push_back(a.id);
    return result;
}

SpellOverrideEntry const* DungeonChallengeMgr::GetSpellOverride(uint32 spellId, uint32 mapId) const
{
    // First try map-specific override
    for (auto const& entry : _spellOverrides)
        if (entry.spellId == spellId && entry.mapId == mapId)
            return &entry;
    // Then try global override (mapId = 0)
    for (auto const& entry : _spellOverrides)
        if (entry.spellId == spellId && entry.mapId == 0)
            return &entry;
    return nullptr;
}

// ============================================================================
// Challenge Run Management
// ============================================================================

ChallengeRun* DungeonChallengeMgr::GetChallengeRun(uint32 instanceId)
{
    auto it = _activeRuns.find(instanceId);
    if (it != _activeRuns.end())
        return &it->second;
    return nullptr;
}

ChallengeRun* DungeonChallengeMgr::GetChallengeRunByParticipant(ObjectGuid playerGuid)
{
    for (auto& pair : _activeRuns)
    {
        if (pair.second.participants.count(playerGuid))
            return &pair.second;
    }
    return nullptr;
}

ChallengeRun* DungeonChallengeMgr::CreateChallengeRun(uint32 instanceId, uint32 mapId, uint32 difficulty, Player* leader)
{
    ChallengeRun run;
    run.instanceId   = instanceId;
    run.mapId        = mapId;
    run.difficulty   = difficulty;
    run.state        = CHALLENGE_STATE_PREPARING;
    run.startTime    = 0;
    run.timerDuration = GetTimerForDungeon(mapId);
    run.elapsedTime  = 0;
    run.deathCount   = 0;
    run.penaltyTime  = 0;
    run.leaderGuid   = leader->GetGUID();
    run.countdownTimer = 0;

    DungeonInfo const* info = GetDungeonInfo(mapId);
    run.totalBosses  = info ? info->totalCreatures : 3; // totalCreatures here stores boss count
    run.bossesKilled = 0;

    // Add leader and group members
    run.participants.insert(leader->GetGUID());
    if (Group* group = leader->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                run.participants.insert(member->GetGUID());
    }

    _activeRuns[instanceId] = run;

    // Link run to MapChallengeData via DataMap
    Map* map = leader->GetMap();
    if (map)
    {
        auto* mapData = map->CustomData.GetDefault<MapChallengeData>("mod-dungeon-challenge");
        mapData->run = &_activeRuns[instanceId];
    }

    LOG_INFO("module", ">> mod-dungeon-challenge: Created challenge run for instance {} (map: {}, difficulty: {})",
        instanceId, mapId, difficulty);

    return &_activeRuns[instanceId];
}

void DungeonChallengeMgr::RemoveChallengeRun(uint32 instanceId)
{
    _activeRuns.erase(instanceId);
}

void DungeonChallengeMgr::StartRun(ChallengeRun* run)
{
    if (!run || (run->state != CHALLENGE_STATE_PREPARING && run->state != CHALLENGE_STATE_COUNTDOWN))
        return;

    run->state = CHALLENGE_STATE_RUNNING;
    run->startTime = GameTime::GetGameTime().count();
    run->elapsedTime = 0;

    LOG_INFO("module", ">> mod-dungeon-challenge: Run started for instance {} (difficulty: {}, timer: {}s)",
        run->instanceId, run->difficulty, run->timerDuration);
}

void DungeonChallengeMgr::UpdateRun(ChallengeRun* run, uint32 diff)
{
    if (!run || run->state != CHALLENGE_STATE_RUNNING)
        return;

    run->elapsedTime = GameTime::GetGameTime().count() - run->startTime;
}

void DungeonChallengeMgr::CompleteRun(ChallengeRun* run)
{
    if (!run || run->state != CHALLENGE_STATE_RUNNING)
        return;

    run->state = CHALLENGE_STATE_COMPLETED;
    run->elapsedTime = GameTime::GetGameTime().count() - run->startTime;

    SaveRunToLeaderboard(run);
    DistributeRewards(run);

    LOG_INFO("module", ">> mod-dungeon-challenge: Run completed! Instance: {}, Time: {}s, Deaths: {}, Penalty: {}s",
        run->instanceId, run->elapsedTime, run->deathCount, run->penaltyTime);
}

void DungeonChallengeMgr::FailRun(ChallengeRun* run)
{
    if (!run)
        return;

    run->state = CHALLENGE_STATE_FAILED;
    LOG_INFO("module", ">> mod-dungeon-challenge: Run failed for instance {}.", run->instanceId);
}

// ============================================================================
// Creature Processing (DataMap-based)
// ============================================================================

void DungeonChallengeMgr::ProcessCreature(Creature* creature, Map* map)
{
    if (!creature || !map)
        return;

    auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
    if (creatureData->processed)
        return;

    auto* mapData = map->CustomData.GetDefault<MapChallengeData>("mod-dungeon-challenge");
    if (!mapData->run || mapData->run->state != CHALLENGE_STATE_RUNNING)
        return;

    // Skip critters, pets, summons, friendly mobs — don't scale them
    if (creature->GetCreatureTemplate()->type == CREATURE_TYPE_CRITTER
        || creature->IsPet() || creature->IsSummon() || creature->IsTotem()
        || creature->GetCreatureTemplate()->faction == 35)
    {
        creatureData->processed = true;
        return;
    }

    // Store original health before scaling
    creatureData->originalHealth = creature->GetMaxHealth();

    // Scale creature
    ScaleCreatureForDifficulty(creature, mapData->run->difficulty);

    // Mark as processed
    creatureData->processed = true;
}

// ============================================================================
// Affix Assignment
// ============================================================================

void DungeonChallengeMgr::AssignAffixesToCreatures(ChallengeRun* run, Map* map)
{
    if (!run || !map)
        return;

    std::vector<DungeonChallengeAffix> availableAffixes = GetAffixesForDifficulty(run->difficulty);
    if (availableAffixes.empty())
        return;

    // Collect all alive non-boss creatures in the instance
    std::vector<Creature*> candidates;
    for (auto const& pair : map->GetCreatureBySpawnIdStore())
    {
        Creature* creature = pair.second;
        if (!creature || !creature->IsAlive())
            continue;
        if (creature->IsPet() || creature->IsSummon() || creature->IsTotem())
            continue;
        // Skip bosses (world bosses, dungeon bosses, rank 3+)
        if (creature->isWorldBoss() || creature->IsDungeonBoss() || creature->GetCreatureTemplate()->rank >= 3)
            continue;
        // Skip critters (type 8), non-combat NPCs, and friendly mobs
        if (creature->GetCreatureTemplate()->type == CREATURE_TYPE_CRITTER)
            continue;
        if (creature->GetCreatureTemplate()->unit_class == 0)
            continue;
        if (creature->GetCreatureTemplate()->faction == 35) // friendly
            continue;
        candidates.push_back(creature);
    }

    if (candidates.empty())
        return;

    // Select ~affixPercentage% of mobs
    uint32 affixCount = std::max(1u, (uint32)(candidates.size() * _affixPercentage / 100));

    // Shuffle and pick
    std::shuffle(candidates.begin(), candidates.end(), _rng);

    for (uint32 i = 0; i < affixCount && i < candidates.size(); ++i)
    {
        Creature* creature = candidates[i];

        run->affixedCreatures.insert(creature->GetGUID());
        run->creatureAffixes[creature->GetGUID()] = availableAffixes;

        // Store ALL available affixes in creature DataMap
        auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
        creatureData->affixes = availableAffixes;

        // Scale creature stats for difficulty FIRST (base HP scaling)
        ScaleCreatureForDifficulty(creature, run->difficulty);

        // Apply each affix AFTER scaling (so Big Boy/Bigger Boy +50% HP
        // is applied on top of the already-scaled health)
        for (auto const& affix : availableAffixes)
            ApplyAffixToCreature(creature, affix, run->difficulty);
    }

    LOG_INFO("module", ">> mod-dungeon-challenge: Assigned affixes to {}/{} creatures in instance {}.",
        affixCount, candidates.size(), run->instanceId);
}

void DungeonChallengeMgr::ApplyAffixToCreature(Creature* creature, DungeonChallengeAffix affix, uint32 difficulty)
{
    if (!creature)
        return;

    AffixInfo const* info = GetAffixInfo(affix);
    auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");

    switch (affix)
    {
        case AFFIX_SPEEDY:
            creature->CastSpell(creature, SPELL_AFFIX_SPEEDY, true);
            break;

        case AFFIX_BIG_BOY:
            // +50% HP; size handled by DBC aura
            creature->SetMaxHealth(static_cast<uint32>(creature->GetMaxHealth() * 1.5f));
            creature->SetFullHealth();
            creature->CastSpell(creature, SPELL_AFFIX_BIG_BOY, true);
            break;

        case AFFIX_CC_IMMUNITY:
            creature->CastSpell(creature, SPELL_AFFIX_CC_IMMUNITY, true);
            break;

        case AFFIX_HEAVY_HITS:
            creature->CastSpell(creature, SPELL_AFFIX_HEAVY_HITS, true);
            break;

        case AFFIX_BIGGER_BOY:
            // +50% HP; size and damage handled by DBC aura
            creature->SetMaxHealth(static_cast<uint32>(creature->GetMaxHealth() * 1.5f));
            creature->SetFullHealth();
            creature->CastSpell(creature, SPELL_AFFIX_BIGGER_BOY, true);
            break;

        case AFFIX_DAMAGE_REDUCE:
        {
            // Visual aura on the creature itself
            creature->CastSpell(creature, SPELL_AFFIX_DAMAGE_REDUCE, true);
            // Apply -25% incoming damage to all allies within 30y
            Map* map = creature->GetMap();
            if (map)
            {
                for (auto const& pair : map->GetCreatureBySpawnIdStore())
                {
                    Creature* ally = pair.second;
                    if (!ally || !ally->IsAlive())
                        continue;
                    if (ally->IsPet() || ally->IsSummon())
                        continue;
                    if (ally->GetDistance(creature) > 30.0f)
                        continue;
                    auto* allyData = ally->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
                    allyData->incomingDamageReduction = std::max(allyData->incomingDamageReduction, 0.25f);
                }
            }
            break;
        }

        case AFFIX_IMMOLATION:
            creature->CastSpell(creature, SPELL_AFFIX_IMMOLATION, true);
            break;

        case AFFIX_HELL_TOUCHED:
            creature->CastSpell(creature, SPELL_AFFIX_HELL_TOUCHED, true);
            break;

        case AFFIX_CALL_FOR_HELP:
            creature->CastSpell(creature, SPELL_AFFIX_CALL_FOR_HELP, true);
            break;

        case AFFIX_LIL_BRO:
            creature->CastSpell(creature, SPELL_AFFIX_LIL_BRO, true);
            break;

        default:
            break;
    }

    // NOTE: ScaleCreatureForDifficulty is called separately in AssignAffixesToCreatures
    // BEFORE affixes, so affix HP bonuses stack on top of difficulty scaling.

    creatureData->affixesApplied = true;

    if (info)
    {
        LOG_DEBUG("module", ">> mod-dungeon-challenge: Applied affix '{}' to creature {} (entry: {})",
            info->name, creature->GetGUID().ToString(), creature->GetEntry());
    }
}

void DungeonChallengeMgr::ScaleCreatureForDifficulty(Creature* creature, uint32 difficulty)
{
    if (!creature || difficulty == 0)
        return;

    float hpMult = GetHealthMultiplier(difficulty);
    float dmgMult = GetDamageMultiplier(difficulty);

    // Store original data and damage multiplier in DataMap
    auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
    if (creatureData->originalHealth == 0)
        creatureData->originalHealth = creature->GetMaxHealth();
    creatureData->extraDamageMultiplier *= dmgMult;
    creatureData->processed = true;

    // Apply HP scaling directly
    creature->SetMaxHealth(creature->GetCreateHealth() * hpMult);
    creature->SetFullHealth();

    // Damage scaling is now handled via UnitScript hooks (ModifyMeleeDamage, ModifySpellDamageTaken)
    // instead of modifying base weapon damage directly.
    // The extraDamageMultiplier stored above is read by the UnitScript.
}

// ============================================================================
// Snapshots
// ============================================================================

void DungeonChallengeMgr::SaveBossKillSnapshot(ChallengeRun const* run, Creature* boss, bool isFinalBoss, bool rewarded)
{
    if (!run || !boss)
        return;

    // Save one snapshot row per participant
    for (auto const& guid : run->participants)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        std::string playerName = player ? player->GetName() : "Unknown";
        uint32 playerGuidVal = guid.GetCounter();

        CharacterDatabase.Execute(
            "INSERT INTO `dungeon_challenge_snapshot` "
            "(`instance_id`, `map_id`, `difficulty`, `start_time`, `snap_time`, `timer_limit`, "
            "`creature_entry`, `creature_name`, `is_final_boss`, `rewarded`, `deaths`, `penalty_time`, "
            "`player_name`, `player_guid`) VALUES ({}, {}, {}, {}, {}, {}, {}, '{}', {}, {}, {}, {}, '{}', {})",
            run->instanceId, run->mapId, run->difficulty, run->startTime,
            run->elapsedTime, run->timerDuration,
            boss->GetEntry(), boss->GetName(),
            isFinalBoss ? 1 : 0, rewarded ? 1 : 0,
            run->deathCount, run->penaltyTime,
            playerName, playerGuidVal);
    }

    LOG_INFO("module", ">> mod-dungeon-challenge: Saved boss kill snapshot for {} (entry: {}, final: {})",
        boss->GetName(), boss->GetEntry(), isFinalBoss ? "yes" : "no");
}

std::vector<BossKillSnapshot> DungeonChallengeMgr::GetSnapshotsForDungeon(uint32 mapId, uint32 difficulty, uint32 limit) const
{
    std::vector<BossKillSnapshot> result;
    auto it = _snapshots.find(mapId);
    if (it == _snapshots.end())
        return result;

    uint32 count = 0;
    for (auto const& snap : it->second)
    {
        if (difficulty > 0 && snap.difficulty != difficulty)
            continue;
        result.push_back(snap);
        if (++count >= limit)
            break;
    }
    return result;
}

// ============================================================================
// Leaderboard
// ============================================================================

void DungeonChallengeMgr::SaveRunToLeaderboard(ChallengeRun const* run)
{
    if (!run || run->state != CHALLENGE_STATE_COMPLETED)
        return;

    // Build participant names
    std::string participantStr;
    for (auto const& guid : run->participants)
    {
        if (Player* player = ObjectAccessor::FindPlayer(guid))
        {
            if (!participantStr.empty())
                participantStr += ",";
            participantStr += player->GetName();
        }
    }

    std::string leaderName;
    if (Player* leader = ObjectAccessor::FindPlayer(run->leaderGuid))
        leaderName = leader->GetName();

    CharacterDatabase.Execute(
        "INSERT INTO `dungeon_challenge_leaderboard` "
        "(`map_id`, `difficulty`, `completion_time`, `death_count`, `leader_name`, `leader_guid`, "
        "`date_completed`, `participants`) VALUES ({}, {}, {}, {}, '{}', {}, NOW(), '{}')",
        run->mapId, run->difficulty, run->GetEffectiveElapsed(), run->deathCount,
        leaderName, run->leaderGuid.GetCounter(), participantStr);

    // Also save to individual player history
    bool inTime = !run->IsTimedOut();
    for (auto const& guid : run->participants)
    {
        CharacterDatabase.Execute(
            "INSERT INTO `dungeon_challenge_history` "
            "(`player_guid`, `map_id`, `difficulty`, `completion_time`, `death_count`, `in_time`) "
            "VALUES ({}, {}, {}, {}, {}, {})",
            guid.GetCounter(), run->mapId, run->difficulty,
            run->GetEffectiveElapsed(), run->deathCount, inTime ? 1 : 0);
    }
}

std::vector<LeaderboardEntry> DungeonChallengeMgr::GetLeaderboard(uint32 mapId, uint32 difficulty, uint32 limit) const
{
    std::vector<LeaderboardEntry> result;

    auto it = _leaderboard.find(mapId);
    if (it == _leaderboard.end())
        return result;

    uint32 count = 0;
    for (auto const& entry : it->second)
    {
        if (difficulty > 0 && entry.difficulty != difficulty)
            continue;
        result.push_back(entry);
        if (++count >= limit)
            break;
    }

    return result;
}

std::vector<LeaderboardEntry> DungeonChallengeMgr::GetPlayerBestRuns(ObjectGuid playerGuid, uint32 limit) const
{
    std::vector<LeaderboardEntry> result;

    QueryResult qr = CharacterDatabase.Query(
        "SELECT map_id, difficulty, completion_time, death_count, leader_name, date_completed, participants "
        "FROM dungeon_challenge_leaderboard WHERE leader_guid = {} ORDER BY completion_time ASC LIMIT {}",
        playerGuid.GetCounter(), limit);

    if (!qr)
        return result;

    do
    {
        Field* fields = qr->Fetch();
        LeaderboardEntry entry;
        entry.mapId          = fields[0].Get<uint32>();
        entry.difficulty     = fields[1].Get<uint32>();
        entry.completionTime = fields[2].Get<uint32>();
        entry.deathCount     = fields[3].Get<uint32>();
        entry.leaderName     = fields[4].Get<std::string>();
        entry.dateCompleted  = fields[5].Get<std::string>();
        result.push_back(entry);
    } while (qr->NextRow());

    return result;
}

// ============================================================================
// Rewards
// ============================================================================

void DungeonChallengeMgr::DistributeRewards(ChallengeRun const* run)
{
    if (!run)
        return;

    bool inTime = !run->IsTimedOut();
    uint32 goldReward = _lootBonusPerLevel * run->difficulty;
    uint32 paragonXP = _paragonXPPerLevel * run->difficulty;

    // Bonus for completing in time
    if (inTime)
    {
        goldReward *= 2;
        paragonXP *= 2;
    }

    for (auto const& guid : run->participants)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player)
            continue;

        // Gold reward
        player->ModifyMoney(goldReward);

        // Paragon XP reward
        if (paragonXP > 0)
            IncreaseParagonXP(player, paragonXP);

        // Announce to player
        uint32 totalTime = run->GetEffectiveElapsed();
        uint32 minutes = totalTime / 60;
        uint32 seconds = totalTime % 60;

        if (inTime)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00ff00[Dungeon Challenge]|r Challenge completed! "
                "Level: |cffff8000{}|r | Time: |cff00ff00{}:{:02}|r (Penalty: +{}s) | Deaths: {} | "
                "Reward: |cffffcc00{} Gold|r | |cff00ccff+{} Paragon XP|r",
                run->difficulty, minutes, seconds, run->penaltyTime, run->deathCount,
                goldReward / 10000, paragonXP);
        }
        else
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff0000[Dungeon Challenge]|r Challenge completed over time! "
                "Level: |cffff8000{}|r | Time: |cffff0000{}:{:02}|r (Penalty: +{}s) | Deaths: {} | "
                "Reward: |cffffcc00{} Gold|r | |cff00ccff+{} Paragon XP|r",
                run->difficulty, minutes, seconds, run->penaltyTime, run->deathCount,
                goldReward / 10000, paragonXP);
        }
    }
}

// ============================================================================
// Non-Mythic Lock
// ============================================================================

void DungeonChallengeMgr::LockInstanceAsNonChallenge(Map* map)
{
    if (!map)
        return;

    auto* mapData = map->CustomData.GetDefault<MapChallengeData>("mod-dungeon-challenge");
    mapData->isLockedNonChallenge = true;

    LOG_DEBUG("module", ">> mod-dungeon-challenge: Instance {} locked as non-challenge (creature killed before keystone)",
        map->GetInstanceId());
}

bool DungeonChallengeMgr::IsInstanceLocked(Map* map) const
{
    if (!map)
        return false;

    auto* mapData = map->CustomData.GetDefault<MapChallengeData>("mod-dungeon-challenge");
    return mapData->isLockedNonChallenge;
}
