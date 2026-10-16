#include "DungeonChallenge.h"
#include "ParagonUtils.h"
#include "Log.h"
#include "World.h"
#include "Group.h"
#include "MapMgr.h"
#include "GameTime.h"
#include "TemporarySummon.h"
#include "Mail.h"
#include "Item.h"
#include "CellImpl.h"
#include "GridNotifiers.h"

#include <algorithm>
#include <limits>
#include <list>
#include <sstream>
#include <cmath>

namespace
{
    // Candidate radius for the AFFIX_DAMAGE_REDUCE grid search. The gameplay range
    // stays 30y (enforced by the GetDistance() check at the use site); this padding
    // only has to be large enough that the grid candidates are a strict superset,
    // because GetDistance() subtracts both object sizes and scaled-up affix models
    // can be several yards across.
    constexpr float DAMAGE_REDUCE_SEARCH_RADIUS = 50.0f;
}

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

    , _announceOnLogin(true)
    , _deathPenaltySeconds(15)
    , _gameObjectEntry(CHALLENGE_GO_ENTRY)
    , _paragonXPPerLevel(10)
    , _summarySeconds(30)
    , _deathEndsRunMode(0)
    , _fallbackSeconds(40)
    , _runCredit(402451)
    , _weeklyAffixCount(2)
    , _restoreWindowMinutes(120)
    , _upliftDrThreshold(10000.0f)
    , _upliftDrExponent(0.975f)
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

    _announceOnLogin = sConfigMgr->GetOption<bool>("DungeonChallenge.AnnounceOnLogin", true);
    _deathPenaltySeconds = sConfigMgr->GetOption<uint32>("DungeonChallenge.DeathPenaltySeconds", 15);
    _gameObjectEntry = sConfigMgr->GetOption<uint32>("DungeonChallenge.GameObjectEntry", CHALLENGE_GO_ENTRY);
    _paragonXPPerLevel = sConfigMgr->GetOption<uint32>("DungeonChallenge.ParagonXPPerLevel", 10);
    _summarySeconds = sConfigMgr->GetOption<uint32>("DungeonChallenge.SummarySeconds", 30);
    _deathEndsRunMode = sConfigMgr->GetOption<uint32>("DungeonChallenge.DeathEndsRunMode", 0);
    _fallbackSeconds = sConfigMgr->GetOption<uint32>("DungeonChallenge.FallbackSeconds", 40);
    _runCredit = sConfigMgr->GetOption<uint32>("DungeonChallenge.RunCredit", 402451);
    _weeklyAffixCount = sConfigMgr->GetOption<uint32>("DungeonChallenge.WeeklyAffixCount", 2);
    _restoreWindowMinutes = sConfigMgr->GetOption<uint32>("DungeonChallenge.RestoreWindowMinutes", 120);
    _upliftDrThreshold = sConfigMgr->GetOption<float>("DungeonChallenge.Uplift.DiminishingThreshold", 10000.0f);
    _upliftDrExponent = sConfigMgr->GetOption<float>("DungeonChallenge.Uplift.DiminishingExponent", 0.975f);

    LOG_INFO("module", ">> mod-dungeon-challenge: Configuration loaded (Enabled: {}, MaxDifficulty: {}, AffixPct: {}%, ParagonXP/Level: {})",
        _enabled ? "Yes" : "No", _maxDifficulty, _affixPercentage, _paragonXPPerLevel);
}

void DungeonChallengeMgr::LoadDungeonData()
{
    _dungeons.clear();

    QueryResult result = WorldDatabase.Query("SELECT map_id, name, entrance_x, entrance_y, entrance_z, entrance_o, timer_minutes, boss_count, raid_difficulty FROM dungeon_challenge_dungeons WHERE enabled = 1");
    if (!result)
    {
        LOG_WARN("module", ">> mod-dungeon-challenge: No dungeon data found in dungeon_challenge_dungeons table. Using defaults.");

        // Default WotLK dungeons. Entrances mirror the areatrigger_teleport
        // entrance rows — the same source the RDF teleport resolves through
        // LFGMgr::LoadLFGDungeons -> GetMapEntranceTrigger.
        _dungeons = {
            { 574, "Utgarde Keep",              153.789f,  -86.548f,   12.551f,  0.304f,    25, 3 },
            { 575, "Utgarde Pinnacle",          584.117f,  -327.974f, 110.138f,  3.122f,    28, 4 },
            { 576, "The Nexus",                 145.87f,   -10.554f,  -16.636f,  1.528f,    28, 4 },
            { 578, "The Oculus",                1055.93f,  986.85f,   361.07f,   5.745f,    35, 4 },
            { 595, "Culling of Stratholme",     1431.1f,   556.92f,    36.69f,   5.16f,     30, 5 },
            { 599, "Halls of Stone",            1153.24f,  806.164f,  195.937f,  4.715f,    28, 3 },
            { 600, "Drak'Tharon Keep",          -517.343f, -487.976f,  11.01f,   4.831f,    25, 4 },
            { 601, "Azjol-Nerub",               413.314f,  795.968f,  831.351f,  5.5f,      20, 3 },
            { 602, "Halls of Lightning",        1331.47f,  259.619f,   53.398f,  4.772f,    28, 4 },
            { 604, "Gundrak",                   1891.84f,  832.169f,  176.669f,  2.109f,    28, 4 },
            { 608, "Violet Hold",               1808.82f,  803.93f,    44.364f,  6.282f,    25, 3 },
            { 619, "Ahn'kahet: The Old Kingdom", 333.351f, -1109.94f,  69.772f,  0.553f,    30, 5 },
            { 632, "The Forge of Souls",        4922.86f,  2175.63f,  638.734f,  2.00355f,  22, 2 },
            { 650, "Trial of the Champion",     805.227f,  618.038f,  412.393f,  3.1456f,   25, 3 },
            { 658, "Pit of Saron",              435.743f,  212.413f,  528.709f,  6.25646f,  28, 3 },
            { 668, "Halls of Reflection",       5239.01f,  1932.64f,  707.695f,  0.800565f, 25, 2 },
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
        info.raidDifficulty = fields[8].Get<uint8>();
        if (info.raidDifficulty > RAID_DIFFICULTY_25MAN_HEROIC)
            info.raidDifficulty = RAID_DIFFICULTY_10MAN_NORMAL;
        _dungeons.push_back(info);
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} dungeons from database.", _dungeons.size());
}

void DungeonChallengeMgr::LoadAffixData()
{
    _affixes.clear();

    // World `dungeon_challenge_affix` (01_dungeon_challenge_mythic_ideas.sql).
    // Rows with an unknown id are skipped: the behaviour of an affix is code.
    if (QueryResult result = WorldDatabase.Query(
        "SELECT affix_id, name, description, min_level, kind, weekly, enabled, value1, value2, spell_id "
        "FROM dungeon_challenge_affix ORDER BY affix_id"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 id = fields[0].Get<uint32>();
            if (id == AFFIX_NONE || id >= AFFIX_MAX)
            {
                LOG_ERROR("module", ">> mod-dungeon-challenge: dungeon_challenge_affix has unknown affix_id {}, skipped.", id);
                continue;
            }
            AffixInfo info;
            info.id            = DungeonChallengeAffix(id);
            info.name          = fields[1].Get<std::string>();
            info.description   = fields[2].Get<std::string>();
            info.minDifficulty = fields[3].Get<uint32>();
            info.kind          = fields[4].Get<uint8>() ? AFFIX_KIND_RUN : AFFIX_KIND_MOB;
            info.weekly        = fields[5].Get<bool>();
            info.enabled       = fields[6].Get<bool>();
            info.value1        = fields[7].Get<float>();
            info.value2        = fields[8].Get<float>();
            info.spellId       = fields[9].Get<uint32>();
            _affixes.push_back(info);
        } while (result->NextRow());

        LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} affixes from database.", _affixes.size());
        return;
    }

    // No table rows: the built-in set as it was before the table existed.
    // Every 10 levels adds +1 affix to the pool; selected mobs receive ALL of them.
    _affixes = {
        { AFFIX_CALL_FOR_HELP, "Call for Help", "Calls allies within 30y for help when entering combat.", 10, AFFIX_KIND_MOB, false, true, 30.0f, 0.0f, SPELL_AFFIX_CALL_FOR_HELP },
        { AFFIX_SPEEDY,        "Speedy",        "+100% movement speed and +10% attack speed.", 20, AFFIX_KIND_MOB, false, true, 0.0f, 0.0f, SPELL_AFFIX_SPEEDY },
        { AFFIX_BIG_BOY,       "Big Boy",       "+50% HP and increased size.", 30, AFFIX_KIND_MOB, false, true, 1.5f, 0.0f, SPELL_AFFIX_BIG_BOY },
        { AFFIX_IMMOLATION,    "Immolation Aura", "Deals periodic fire damage (Level x 80) to nearby players.", 40, AFFIX_KIND_MOB, false, true, 80.0f, 8.0f, SPELL_AFFIX_IMMOLATION },
        { AFFIX_CC_IMMUNITY,   "CC Immunity",   "Immune to all crowd control effects.", 50, AFFIX_KIND_MOB, false, true, 0.0f, 0.0f, SPELL_AFFIX_CC_IMMUNITY },
        { AFFIX_HEAVY_HITS,    "Heavy Hits",    "+33% damage dealt.", 60, AFFIX_KIND_MOB, false, true, 0.0f, 0.0f, SPELL_AFFIX_HEAVY_HITS },
        { AFFIX_LIL_BRO,       "Lil' Bro",      "Splits into 2 copies on death (1->2->4), each with -90% HP.", 70, AFFIX_KIND_MOB, false, true, 0.1f, 0.0f, SPELL_AFFIX_LIL_BRO },
        { AFFIX_DAMAGE_REDUCE, "Damage Reduce", "Allies within 30y take 25% less damage.", 80, AFFIX_KIND_MOB, false, true, 0.25f, 30.0f, SPELL_AFFIX_DAMAGE_REDUCE },
        { AFFIX_BIGGER_BOY,    "Bigger Boy",    "Additional +50% HP, increased size and +10% damage.", 90, AFFIX_KIND_MOB, false, true, 1.5f, 0.0f, SPELL_AFFIX_BIGGER_BOY },
        { AFFIX_HELL_TOUCHED,  "Hell Touched",  "Deals 666 hellfire damage on hit. Reduces stats by 10% (10s, stacks 10).", 100, AFFIX_KIND_MOB, false, true, 666.0f, 0.0f, SPELL_AFFIX_HELL_TOUCHED },
    };

    LOG_INFO("module", ">> mod-dungeon-challenge: dungeon_challenge_affix is empty, loaded the {} built-in affixes.", _affixes.size());
}

void DungeonChallengeMgr::LoadScales()
{
    _scales.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT map_id, trash_health, trash_damage, boss_health, boss_damage, uplift_level FROM dungeon_challenge_scale");
    if (!result)
    {
        LOG_INFO("module", ">> mod-dungeon-challenge: No dungeon scale factors defined.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        DungeonScale scale;
        scale.trashHealth = std::max(0.01f, fields[1].Get<float>());
        scale.trashDamage = std::max(0.0f, fields[2].Get<float>());
        scale.bossHealth  = std::max(0.01f, fields[3].Get<float>());
        scale.bossDamage  = std::max(0.0f, fields[4].Get<float>());
        scale.upliftLevel = fields[5].Get<uint8>();
        _scales[fields[0].Get<uint32>()] = scale;
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} dungeon scale rows.", _scales.size());
}

void DungeonChallengeMgr::LoadRewards()
{
    _rewards.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT min_level, max_level, item_entry, item_count FROM dungeon_challenge_reward ORDER BY min_level, item_entry");
    if (!result)
    {
        LOG_INFO("module", ">> mod-dungeon-challenge: No item rewards defined.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        RewardEntry entry;
        entry.minLevel  = fields[0].Get<uint32>();
        entry.maxLevel  = fields[1].Get<uint32>();
        entry.itemEntry = fields[2].Get<uint32>();
        entry.itemCount = fields[3].Get<uint32>();
        if (!sObjectMgr->GetItemTemplate(entry.itemEntry) || !entry.itemCount)
        {
            LOG_ERROR("module", ">> mod-dungeon-challenge: dungeon_challenge_reward item {} (levels {}-{}) does not exist or has count 0, skipped.",
                entry.itemEntry, entry.minLevel, entry.maxLevel);
            continue;
        }
        _rewards.push_back(entry);
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} item reward rows.", _rewards.size());
}

void DungeonChallengeMgr::LoadBossEntries()
{
    _bossEntries.clear();

    if (QueryResult result = WorldDatabase.Query("SELECT creature_entry FROM dungeon_challenge_boss_entry"))
    {
        do
        {
            _bossEntries.insert(result->Fetch()[0].Get<uint32>());
        } while (result->NextRow());
    }

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} extra boss entries.", _bossEntries.size());
}

void DungeonChallengeMgr::LoadTuningTables()
{
    LoadAffixData();
    LoadScales();
    LoadRewards();
    LoadBossEntries();
    LoadBossGroups();
}

void DungeonChallengeMgr::LoadBossGroups()
{
    _bossGroupByEntry.clear();

    QueryResult result = WorldDatabase.Query("SELECT creature_entry, group_id FROM dungeon_challenge_boss_group");
    if (!result)
    {
        LOG_INFO("module", ">> mod-dungeon-challenge: No boss groups defined.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        _bossGroupByEntry[fields[0].Get<uint32>()] = fields[1].Get<uint32>();
    } while (result->NextRow());

    LOG_INFO("module", ">> mod-dungeon-challenge: Loaded {} boss group members.", _bossGroupByEntry.size());
}

uint32 DungeonChallengeMgr::GetBossGroupId(uint32 creatureEntry) const
{
    auto it = _bossGroupByEntry.find(creatureEntry);
    return it != _bossGroupByEntry.end() ? it->second : 0;
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
        "player_name, player_guid, combat_time FROM dungeon_challenge_snapshot "
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
        snap.combatTime     = fields[15].Get<uint32>();
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

uint32 DungeonChallengeMgr::GetCurrentWeek() const
{
    if (_weekPin)
        return _weekPin;
    int64 week = (int64(GameTime::GetGameTime().count()) - int64(ROTATION_EPOCH_OFFSET)) / int64(SECONDS_PER_WEEK);
    return uint32(std::max<int64>(0, week));
}

// The weekly pool (enabled weekly affixes, by id) turns like a wheel: week w
// takes `count` affixes starting at w x count. No randomness, so the Lua UI
// computes the same set from the same table.
std::vector<DungeonChallengeAffix> DungeonChallengeMgr::GetWeeklyAffixes(uint32 week) const
{
    std::vector<DungeonChallengeAffix> pool;
    for (auto const& a : _affixes)
        if (a.enabled && a.weekly)
            pool.push_back(a.id);
    std::sort(pool.begin(), pool.end());

    std::vector<DungeonChallengeAffix> result;
    if (pool.empty() || !_weeklyAffixCount)
        return result;

    uint32 count = std::min<uint32>(_weeklyAffixCount, pool.size());
    uint32 start = uint32((uint64(week) * count) % pool.size());
    for (uint32 i = 0; i < count; ++i)
        result.push_back(pool[(start + i) % pool.size()]);
    return result;
}

std::vector<DungeonChallengeAffix> DungeonChallengeMgr::GetAffixesForDifficulty(uint32 difficulty) const
{
    return GetAffixesForDifficulty(difficulty, GetCurrentWeek());
}

std::vector<DungeonChallengeAffix> DungeonChallengeMgr::GetAffixesForDifficulty(uint32 difficulty, uint32 week) const
{
    std::vector<DungeonChallengeAffix> weekly = GetWeeklyAffixes(week);
    std::vector<DungeonChallengeAffix> result;
    for (auto const& a : _affixes)
    {
        if (!a.enabled || difficulty < a.minDifficulty)
            continue;
        if (a.weekly && std::find(weekly.begin(), weekly.end(), a.id) == weekly.end())
            continue;
        result.push_back(a.id);
    }
    return result;
}

float DungeonChallengeMgr::GetAffixValue(DungeonChallengeAffix affix, uint8 index, float fallback) const
{
    AffixInfo const* info = GetAffixInfo(affix);
    if (!info)
        return fallback;
    float value = index == 1 ? info->value1 : info->value2;
    return value > 0.0f ? value : fallback;
}

// ============================================================================
// Per-dungeon factors and boss detection
// ============================================================================

DungeonScale const& DungeonChallengeMgr::GetScale(uint32 mapId) const
{
    static DungeonScale const defaultScale;
    auto it = _scales.find(mapId);
    return it != _scales.end() ? it->second : defaultScale;
}

bool DungeonChallengeMgr::IsChallengeBoss(Creature const* creature) const
{
    return creature->GetCreatureTemplate()->rank >= 3
        || creature->isWorldBoss()
        || creature->IsDungeonBoss()
        || _bossEntries.count(creature->GetEntry());
}

float DungeonChallengeMgr::GetCreatureHealthMultiplier(Creature const* creature, uint32 difficulty) const
{
    DungeonScale const& scale = GetScale(creature->GetMapId());
    return GetHealthMultiplier(difficulty) * (IsChallengeBoss(creature) ? scale.bossHealth : scale.trashHealth);
}

float DungeonChallengeMgr::GetCreatureDamageMultiplier(Creature const* creature, uint32 difficulty) const
{
    DungeonScale const& scale = GetScale(creature->GetMapId());
    return GetDamageMultiplier(difficulty) * (IsChallengeBoss(creature) ? scale.bossDamage : scale.trashDamage);
}

std::vector<RewardEntry> DungeonChallengeMgr::GetRewardsForLevel(uint32 level) const
{
    std::vector<RewardEntry> result;
    for (auto const& r : _rewards)
        if (level >= r.minLevel && level <= r.maxLevel)
            result.push_back(r);
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
    run.affixWeek    = GetCurrentWeek();
    run.affixes      = GetAffixesForDifficulty(difficulty, run.affixWeek);

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
    auto it = _activeRuns.find(instanceId);
    if (it == _activeRuns.end())
        return;

    // CreateChallengeRun caches &_activeRuns[instanceId] on the map's DataMap, and
    // ProcessCreature() dereferences that pointer. Erasing the container node
    // without clearing it left a dangling pointer behind and every later
    // ProcessCreature() call on that map read freed memory -- the null check there
    // cannot help, because the pointer is non-null and stale.
    //
    // Clear it FIRST, then erase. If the instance map is already gone the cached
    // data died with it, so there is nothing to clear.
    if (Map* map = sMapMgr->FindMap(it->second.mapId, instanceId))
        if (auto* mapData = map->CustomData.Get<MapChallengeData>("mod-dungeon-challenge"))
            mapData->run = nullptr;

    _activeRuns.erase(it);
}

void DungeonChallengeMgr::StartRun(ChallengeRun* run)
{
    if (!run || (run->state != CHALLENGE_STATE_PREPARING && run->state != CHALLENGE_STATE_COUNTDOWN))
        return;

    run->state = CHALLENGE_STATE_RUNNING;
    run->startTime = GameTime::GetGameTime().count();
    run->elapsedTime = 0;
    SaveActiveRun(run);

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
    DeleteActiveRun(run->instanceId);

    // Persist leaderboard/history/rewards while still in COMPLETED state (their guards require it).
    SaveRunToLeaderboard(run);
    DistributeRewards(run);

    // The call board's run quests (daily and weekly, mod-hero-call-board) count one kill credit per completed run,
    // in time or not, for every participant still in the run's instance.
    if (_runCredit)
    {
        for (auto const& guid : run->participants)
        {
            Player* player = ObjectAccessor::FindPlayer(guid);
            if (player && player->IsInWorld() && player->GetInstanceId() == run->instanceId)
                player->KilledMonsterCredit(_runCredit);
        }
    }

    LOG_INFO("module", ">> mod-dungeon-challenge: Run completed! Instance: {}, Time: {}s, Deaths: {}, Penalty: {}s",
        run->instanceId, run->elapsedTime, run->deathCount, run->penaltyTime);

    // Enter the summary window: the on-screen recap is shown and the player is
    // teleported out after a delay (or via the Leave button) — see OnMapUpdate.
    run->endedByDeath = false;
    run->summaryElapsedMs = 0;
    run->state = CHALLENGE_STATE_SUMMARY;
    WriteRunEndSignal(run, true);
}

void DungeonChallengeMgr::FailRun(ChallengeRun* run)
{
    if (!run)
        return;

    run->state = CHALLENGE_STATE_FAILED;
    DeleteActiveRun(run->instanceId);
    LOG_INFO("module", ">> mod-dungeon-challenge: Run failed for instance {}.", run->instanceId);
}

void DungeonChallengeMgr::EndRunByDeath(ChallengeRun* run)
{
    if (!run || run->state != CHALLENGE_STATE_RUNNING)
        return;

    run->elapsedTime = GameTime::GetGameTime().count() - run->startTime;
    run->endedByDeath = true;
    run->summaryElapsedMs = 0;
    run->state = CHALLENGE_STATE_SUMMARY;
    DeleteActiveRun(run->instanceId);

    // Record an incomplete run in each participant's history (completion_time = 0 => failed).
    for (auto const& guid : run->participants)
    {
        CharacterDatabase.Execute(
            "INSERT INTO `dungeon_challenge_history` "
            "(`player_guid`, `map_id`, `difficulty`, `completion_time`, `death_count`, `in_time`) "
            "VALUES ({}, {}, {}, 0, {}, 0)",
            guid.GetCounter(), run->mapId, run->difficulty, run->deathCount);
    }

    WriteRunEndSignal(run, false);

    LOG_INFO("module", ">> mod-dungeon-challenge: Run ended by death (instance {}, time: {}s, deaths: {}).",
        run->instanceId, run->elapsedTime, run->deathCount);
}

void DungeonChallengeMgr::WriteRunEndSignal(ChallengeRun const* run, bool clear)
{
    if (!run)
        return;

    uint32 totalTime = run->elapsedTime;
    uint32 effectiveTime = run->GetEffectiveElapsed();
    uint32 inTime = (clear && !run->IsTimedOut()) ? 1 : 0;
    uint32 outcome = clear ? 1 : 0;

    // One row per participant. The client pulls this via the Lua RequestRunEnd handler
    // to render the summary, and uses the home coordinates to teleport the player out.
    for (auto const& guid : run->participants)
    {
        Player* p = ObjectAccessor::FindPlayer(guid);
        if (!p)
            continue;

        CharacterDatabase.Execute(
            "REPLACE INTO `dungeon_challenge_runend` "
            "(`player_guid`, `instance_id`, `map_id`, `difficulty`, `outcome`, "
            "`total_time`, `effective_time`, `in_time`, `deaths`, "
            "`home_map`, `home_x`, `home_y`, `home_z`) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
            guid.GetCounter(), run->instanceId, run->mapId, run->difficulty, outcome,
            totalTime, effectiveTime, inTime, run->deathCount,
            p->m_homebindMapId, p->m_homebindX, p->m_homebindY, p->m_homebindZ);
    }
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

    // Skip critters, friendly mobs and everything on the players' side. NPC-owned
    // summons - scripted boss adds, NPC totems - are scaled like trash: skipping
    // every summon left boss adds at 1x while trash reached 16x HP (Mythic+ survey
    // 2026-10-10 §4.1). The DC's own copies (respawn copies, Lil' Bro splits) are
    // marked processed when they are created, so they never reach this point.
    if (creature->GetCreatureTemplate()->type == CREATURE_TYPE_CRITTER
        || IsPlayerSideCreature(creature)
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

    std::vector<DungeonChallengeAffix> availableAffixes;
    for (DungeonChallengeAffix affix : run->affixes)
    {
        AffixInfo const* info = GetAffixInfo(affix);
        if (info && info->kind == AFFIX_KIND_MOB)
            availableAffixes.push_back(affix);
    }
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
        // Skip bosses (world bosses, dungeon bosses, rank 3+, dungeon_challenge_boss_entry)
        if (IsChallengeBoss(creature))
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
            // +50% HP (value1 = factor); size handled by DBC aura
            SetCreatureBaseHealth(creature, static_cast<uint32>(creature->GetCreateHealth() * GetAffixValue(AFFIX_BIG_BOY, 1, 1.5f)));
            creature->CastSpell(creature, SPELL_AFFIX_BIG_BOY, true);
            break;

        case AFFIX_CC_IMMUNITY:
            creature->CastSpell(creature, SPELL_AFFIX_CC_IMMUNITY, true);
            break;

        case AFFIX_HEAVY_HITS:
            creature->CastSpell(creature, SPELL_AFFIX_HEAVY_HITS, true);
            break;

        case AFFIX_BIGGER_BOY:
            // +50% HP (value1 = factor); size and damage handled by DBC aura
            SetCreatureBaseHealth(creature, static_cast<uint32>(creature->GetCreateHealth() * GetAffixValue(AFFIX_BIGGER_BOY, 1, 1.5f)));
            creature->CastSpell(creature, SPELL_AFFIX_BIGGER_BOY, true);
            break;

        case AFFIX_DAMAGE_REDUCE:
        {
            // Visual aura on the creature itself
            creature->CastSpell(creature, SPELL_AFFIX_DAMAGE_REDUCE, true);

            // Apply -25% incoming damage to all allies within 30y.
            //
            // Grid-local search instead of walking every DB spawn on the map:
            // AssignAffixesToCreatures() calls ApplyAffixToCreature() for every
            // affix of every affixed creature, so with AffixPercentage=40 on a
            // 960-mob pull the previous full-map walk ran ~384 times in a single
            // burst at run start -- and again on every affix reapplication.
            //
            // The set is preserved exactly:
            //  * the search radius is padded far beyond 30y, so the candidates are
            //    a strict superset of anything GetDistance() could accept
            //    (GetDistance subtracts BOTH object sizes, and Big Boy/Bigger Boy
            //    scale models up to 2.25x, so the slack has to be generous);
            //  * the original 30y GetDistance() cut is kept verbatim below;
            //  * the alive / pet / summon filters are unchanged, which is what
            //    keeps respawn copies and Lil' Bro splits out (both are summons)
            //    even though a grid search sees them and the spawn store did not;
            //  * self is still included, exactly as before -- the old loop had no
            //    `ally == creature` check either.
            //
            // NOTE on the third argument: AllDeadCreaturesInRange's `reqAlive` flag
            // is inverted from how it reads -- true REJECTS living units. `false`
            // means "no aliveness filter", i.e. every creature in range, which is
            // what we want before applying our own IsAlive() check.
            float const reduction = std::min(0.95f, GetAffixValue(AFFIX_DAMAGE_REDUCE, 1, 0.25f));
            float const range = std::min(DAMAGE_REDUCE_SEARCH_RADIUS - 10.0f, GetAffixValue(AFFIX_DAMAGE_REDUCE, 2, 30.0f));
            std::list<Creature*> nearby;
            creature->GetDeadCreatureListInGrid(nearby, DAMAGE_REDUCE_SEARCH_RADIUS, false);

            for (Creature* ally : nearby)
            {
                if (!ally || !ally->IsAlive())
                    continue;
                if (ally->IsPet() || ally->IsSummon())
                    continue;
                if (ally->GetDistance(creature) > range)
                    continue;
                auto* allyData = ally->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
                allyData->incomingDamageReduction = std::max(allyData->incomingDamageReduction, reduction);
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

    // Level multipliers times the dungeon's boss or trash factor (dungeon_challenge_scale).
    float hpMult = GetCreatureHealthMultiplier(creature, difficulty);
    float dmgMult = GetCreatureDamageMultiplier(creature, difficulty);

    // Store original data and damage multiplier in DataMap
    auto* creatureData = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");

    // Low-level dungeons (dungeon_challenge_scale.uplift_level) first lift
    // every creature to the target level with WotLK base stats (idea 9); the
    // level multipliers then apply on top, as in a level-80 dungeon. Once per
    // life: OnCreatureSelectLevel clears originalLevel when the core rebuilds
    // the stats.
    if (uint8 uplift = GetScale(creature->GetMapId()).upliftLevel)
    {
        if (!creatureData->originalLevel && !IsPlayerSideCreature(creature))
        {
            uint8 rank = uint8(creature->GetCreatureTemplate()->rank);
            uint8 target = uplift + (IsChallengeBoss(creature) ? 2 : (rank == CREATURE_ELITE_ELITE || rank == CREATURE_ELITE_RAREELITE ? 1 : 0));
            if (creature->GetLevel() < target)
            {
                UpliftCreature(creature, target);
                creatureData->baseCreateHealth = creature->GetCreateHealth();
            }
        }
    }

    if (creatureData->originalHealth == 0)
        creatureData->originalHealth = creature->GetMaxHealth();
    // Unscaled create health of this life. Taken once: this function runs again
    // after every evade of an affixed mob, and Big Boy has raised the create
    // health by then. OnCreatureSelectLevel clears it when the core rebuilds the
    // stats (respawn, UpdateEntry).
    if (creatureData->baseCreateHealth == 0)
        creatureData->baseCreateHealth = creature->GetCreateHealth();
    // Assigned, not multiplied: a repeated call used to compound the factor
    // (9x, 81x, ...) on every evade of an affixed mob.
    creatureData->extraDamageMultiplier = dmgMult;
    creatureData->healthMultiplier = hpMult;
    creatureData->processed = true;
    creatureData->scaled = true;

    // HP through the create health and UNIT_MOD_HEALTH, not SetMaxHealth alone:
    // Creature::UpdateMaxHealth recomputes the maximum from UNIT_MOD_HEALTH on
    // every health aura change and used to drop the mob back to 1x.
    SetCreatureBaseHealth(creature, static_cast<uint32>(std::min<double>(
        double(creatureData->baseCreateHealth) * hpMult, double(std::numeric_limits<uint32>::max()))));

    // Damage scaling is now handled via UnitScript hooks (ModifyMeleeDamage, ModifySpellDamageTaken)
    // instead of modifying base weapon damage directly.
    // The extraDamageMultiplier stored above is read by the UnitScript.
}

void DungeonChallengeMgr::SetCreatureBaseHealth(Creature* creature, uint32 health)
{
    health = std::max(1u, health);
    creature->SetCreateHealth(health);
    creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
    creature->UpdateMaxHealth();
    creature->SetFullHealth();
}

bool DungeonChallengeMgr::IsPlayerSideCreature(Creature* creature)
{
    if (creature->IsPet() || creature->IsControlledByPlayer() || creature->IsCharmedOwnedByPlayerOrPlayer())
        return true;

    // A summon carries no owner when its script summoned it without one, so ask
    // who summoned it: a player directly, or a unit a player controls (a pet).
    if (TempSummon* summon = creature->ToTempSummon())
    {
        if (summon->GetSummonerGUID().IsPlayer())
            return true;
        if (Unit* summoner = summon->GetSummonerUnit())
            return summoner->IsControlledByPlayer() || summoner->IsCharmedOwnedByPlayerOrPlayer();
    }
    return false;
}

// ============================================================================
// Snapshots
// ============================================================================

void DungeonChallengeMgr::SaveBossKillSnapshot(ChallengeRun const* run, Creature* boss, bool isFinalBoss, bool rewarded, uint32 combatTime)
{
    if (!run || !boss)
        return;

    std::string bossName = boss->GetName();
    CharacterDatabase.EscapeString(bossName);

    // Save one snapshot row per participant
    for (auto const& guid : run->participants)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        std::string playerName = player ? player->GetName() : "Unknown";
        CharacterDatabase.EscapeString(playerName);
        uint32 playerGuidVal = guid.GetCounter();

        CharacterDatabase.Execute(
            "INSERT INTO `dungeon_challenge_snapshot` "
            "(`instance_id`, `map_id`, `difficulty`, `start_time`, `snap_time`, `timer_limit`, "
            "`creature_entry`, `creature_name`, `is_final_boss`, `rewarded`, `deaths`, `penalty_time`, "
            "`player_name`, `player_guid`, `combat_time`) VALUES ({}, {}, {}, {}, {}, {}, {}, '{}', {}, {}, {}, {}, '{}', {}, {})",
            run->instanceId, run->mapId, run->difficulty, run->startTime,
            run->elapsedTime, run->timerDuration,
            boss->GetEntry(), bossName,
            isFinalBoss ? 1 : 0, rewarded ? 1 : 0,
            run->deathCount, run->penaltyTime,
            playerName, playerGuidVal, combatTime);
    }

    LOG_INFO("module", ">> mod-dungeon-challenge: Queued boss kill snapshots for {} (entry: {}, final: {}, participants: {})",
        boss->GetName(), boss->GetEntry(), isFinalBoss ? "yes" : "no", run->participants.size());
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

    // Items of the level's band (dungeon_challenge_reward); the count doubles in time.
    std::vector<RewardEntry> const items = GetRewardsForLevel(run->difficulty);

    for (auto const& guid : run->participants)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player)
            continue;

        // Gold reward
        player->ModifyMoney(goldReward);

        for (RewardEntry const& reward : items)
        {
            uint32 count = reward.itemCount * (inTime ? 2 : 1);
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(reward.itemEntry);
            if (!proto)
                continue;

            if (player->AddItem(reward.itemEntry, count))
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cff00ff00[Dungeon Challenge]|r Reward: |cffffffff{}x {}|r", count, proto->Name1);
                continue;
            }

            // Bags full: the reward comes by mail, in stacks the item allows.
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            MailDraft draft("Dungeon Challenge reward", "Your bags were full when you finished the challenge. Here is your reward.");
            uint32 left = count;
            uint32 const stack = std::max<uint32>(1, proto->GetMaxStackSize());
            while (left > 0)
            {
                uint32 n = std::min(left, stack);
                if (Item* item = Item::CreateItem(reward.itemEntry, n, player))
                {
                    item->SaveToDB(trans);
                    draft.AddItem(item);
                }
                left -= n;
            }
            draft.SendMailTo(trans, MailReceiver(player), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
            CharacterDatabase.CommitTransaction(trans);
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00ff00[Dungeon Challenge]|r Reward: |cffffffff{}x {}|r (sent by mail, your bags are full)", count, proto->Name1);
        }

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

// ============================================================================
// Run affixes (Mythic+ idea 6): they act on the whole instance, not on the
// affixed mobs. Numbers come from dungeon_challenge_affix value1/value2.
// ============================================================================

void DungeonChallengeMgr::UpdateRunAffixes(ChallengeRun* run, Map* map, uint32 diff)
{
    if (!run || !map || run->state != CHALLENGE_STATE_RUNNING)
        return;

    auto participantsInMap = [&]()
    {
        std::vector<Player*> players;
        for (auto const& ref : map->GetPlayers())
            if (Player* p = ref.GetSource())
                if (p->IsAlive() && !p->IsGameMaster() && run->participants.count(p->GetGUID()))
                    players.push_back(p);
        return players;
    };

    // Lightning Sphere: every value1 seconds, a value2 % chance that a sphere
    // appears next to a random participant (npc_dc_lightning_sphere pulses).
    // Summoned by the map, not a player, so the run scales it like trash.
    if (run->HasAffix(AFFIX_LIGHTNING_SPHERE))
    {
        bool const forced = run->forcedAffix == AFFIX_LIGHTNING_SPHERE;
        run->sphereTimerMs += diff;
        uint32 interval = uint32(GetAffixValue(AFFIX_LIGHTNING_SPHERE, 1, 25.0f) * IN_MILLISECONDS);
        if (forced || run->sphereTimerMs >= interval)
        {
            run->sphereTimerMs = 0;
            if (forced)
                run->forcedAffix = AFFIX_NONE;
            std::vector<Player*> players = participantsInMap();
            if (!players.empty() && (forced || roll_chance_f(GetAffixValue(AFFIX_LIGHTNING_SPHERE, 2, 65.0f))))
            {
                Player* target = Acore::Containers::SelectRandomContainerElement(players);
                Position pos = target->GetRandomNearPosition(6.0f);
                if (map->SummonCreature(NPC_LIGHTNING_SPHERE, pos, nullptr, 60 * IN_MILLISECONDS))
                    for (Player* p : players)
                        ChatHandler(p->GetSession()).SendSysMessage(
                            "|cffff8000[Affix: Lightning Sphere]|r A Lightning Sphere appears - kill it before it overloads everyone!");
            }
        }
    }

    // Entangling Roots: every value1 seconds per participant, a value2 % chance
    // that a random hostile creature within 40 yd roots them.
    if (run->HasAffix(AFFIX_ENTANGLING_ROOTS))
    {
        uint32 interval = uint32(GetAffixValue(AFFIX_ENTANGLING_ROOTS, 1, 10.0f) * IN_MILLISECONDS);
        float chance = GetAffixValue(AFFIX_ENTANGLING_ROOTS, 2, 15.0f);
        bool const forced = run->forcedAffix == AFFIX_ENTANGLING_ROOTS;
        if (forced)
            run->forcedAffix = AFFIX_NONE;
        for (Player* player : participantsInMap())
        {
            uint32& timer = run->rootTimersMs[player->GetGUID()];
            timer += diff;
            if (!forced && timer < interval)
                continue;
            timer = 0;
            if ((!forced && !roll_chance_f(chance)) || player->HasAura(SPELL_ENTANGLING_ROOTS))
                continue;

            std::list<Unit*> units;
            Acore::AnyUnfriendlyUnitInObjectRangeCheck check(player, player, 40.0f);
            Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(player, units, check);
            Cell::VisitObjects(player, searcher, 40.0f);

            std::vector<Creature*> casters;
            for (Unit* unit : units)
            {
                Creature* c = unit->ToCreature();
                if (!c || !c->IsAlive() || c->GetEntry() == NPC_LIGHTNING_SPHERE
                    || c->GetCreatureTemplate()->type == CREATURE_TYPE_CRITTER || IsPlayerSideCreature(c)
                    || !c->IsValidAttackTarget(player) || !c->IsWithinLOSInMap(player))
                    continue;
                casters.push_back(c);
            }
            if (!casters.empty())
                Acore::Containers::SelectRandomContainerElement(casters)->CastSpell(player, SPELL_ENTANGLING_ROOTS, true);
        }
    }
}

void DungeonChallengeMgr::UpdateCreatureRunAffixes(ChallengeRun* run, Creature* creature, uint32 diff)
{
    // Enrage: every value1 seconds a value2 % chance that a creature fighting a
    // player enrages (bosses included).
    if (!run || !run->HasAffix(AFFIX_ENRAGE) || !creature->IsAlive() || !creature->IsInCombat())
        return;
    if (creature->GetEntry() == NPC_LIGHTNING_SPHERE || creature->GetCreatureTemplate()->type == CREATURE_TYPE_CRITTER)
        return;
    Unit* victim = creature->GetVictim();
    if (!victim || !victim->GetCharmerOrOwnerPlayerOrPlayerItself() || IsPlayerSideCreature(creature))
        return;

    auto* data = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
    if (run->forcedAffix == AFFIX_ENRAGE && !creature->HasAura(SPELL_ENRAGE))
    {
        run->forcedAffix = AFFIX_NONE;
        data->enrageTimerMs = 0;
        creature->CastSpell(creature, SPELL_ENRAGE, true);
        return;
    }
    data->enrageTimerMs += diff;
    if (data->enrageTimerMs < uint32(GetAffixValue(AFFIX_ENRAGE, 1, 2.0f) * IN_MILLISECONDS))
        return;
    data->enrageTimerMs = 0;
    if (!creature->HasAura(SPELL_ENRAGE) && roll_chance_f(GetAffixValue(AFFIX_ENRAGE, 2, 6.5f)))
        creature->CastSpell(creature, SPELL_ENRAGE, true);
}

// ============================================================================
// Run persistence (Mythic+ idea 10): a running run is one row of characters
// `dungeon_challenge_active`, written at its start, after each boss kill and
// death, and deleted when it ends. A participant entering the same instance
// after a restart or an unload gets the run back; the clock kept running
// (start time is absolute), so the downtime counts against the timer.
// ============================================================================

namespace
{
    template <class T>
    std::string JoinIds(T const& ids)
    {
        std::ostringstream ss;
        bool first = true;
        for (auto const& id : ids)
        {
            if (!first)
                ss << ',';
            ss << uint32(id);
            first = false;
        }
        return ss.str();
    }

    std::vector<uint32> SplitIds(std::string const& text)
    {
        std::vector<uint32> ids;
        std::stringstream ss(text);
        std::string token;
        while (std::getline(ss, token, ','))
            if (!token.empty())
                ids.push_back(uint32(std::strtoul(token.c_str(), nullptr, 10)));
        return ids;
    }
}

void DungeonChallengeMgr::SaveActiveRun(ChallengeRun const* run)
{
    if (!run || run->state != CHALLENGE_STATE_RUNNING)
        return;

    std::vector<uint32> participants;
    for (auto const& guid : run->participants)
        participants.push_back(guid.GetCounter());

    CharacterDatabase.Execute(
        "REPLACE INTO `dungeon_challenge_active` "
        "(`instance_id`, `map_id`, `difficulty`, `start_time`, `timer_duration`, `death_count`, `penalty_time`, "
        "`bosses_killed`, `leader_guid`, `participants`, `affixes`, `affix_week`) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, '{}', '{}', {})",
        run->instanceId, run->mapId, run->difficulty, run->startTime, run->timerDuration,
        run->deathCount, run->penaltyTime, run->bossesKilled, run->leaderGuid.GetCounter(),
        JoinIds(participants), JoinIds(run->affixes), run->affixWeek);
}

void DungeonChallengeMgr::DeleteActiveRun(uint32 instanceId)
{
    CharacterDatabase.Execute("DELETE FROM `dungeon_challenge_active` WHERE `instance_id` = {}", instanceId);
}

void DungeonChallengeMgr::CleanupActiveRuns()
{
    // Instance ids are reused once their save is gone: a row whose instance no
    // longer exists must not restore into a stranger's new instance.
    CharacterDatabase.DirectExecute(
        "DELETE a FROM `dungeon_challenge_active` a LEFT JOIN `instance` i ON i.`id` = a.`instance_id` WHERE i.`id` IS NULL");
    if (_restoreWindowMinutes)
        CharacterDatabase.DirectExecute("DELETE FROM `dungeon_challenge_active` WHERE `start_time` < {}",
            uint32(GameTime::GetGameTime().count()) - _restoreWindowMinutes * MINUTE);
}

ChallengeRun* DungeonChallengeMgr::TryRestoreRun(Player* player, Map* map)
{
    if (!player || !map || !map->IsDungeon() || !IsDungeonCapable(map->GetId()))
        return nullptr;

    uint32 const instanceId = map->GetInstanceId();
    if (GetChallengeRun(instanceId))
        return nullptr;

    QueryResult result = CharacterDatabase.Query(
        "SELECT `map_id`, `difficulty`, `start_time`, `timer_duration`, `death_count`, `penalty_time`, "
        "`bosses_killed`, `leader_guid`, `participants`, `affixes`, `affix_week` "
        "FROM `dungeon_challenge_active` WHERE `instance_id` = {}", instanceId);
    if (!result)
        return nullptr;

    Field* fields = result->Fetch();
    uint32 const now = uint32(GameTime::GetGameTime().count());
    uint32 const startTime = fields[2].Get<uint32>();
    if (fields[0].Get<uint32>() != map->GetId()
        || (_restoreWindowMinutes && now > startTime + _restoreWindowMinutes * MINUTE))
    {
        DeleteActiveRun(instanceId);
        return nullptr;
    }

    std::unordered_set<ObjectGuid> participants;
    for (uint32 low : SplitIds(fields[8].Get<std::string>()))
        participants.insert(ObjectGuid::Create<HighGuid::Player>(low));
    if (!participants.count(player->GetGUID()))
        return nullptr; // only a participant brings the run back

    ChallengeRun run;
    run.instanceId     = instanceId;
    run.mapId          = map->GetId();
    run.difficulty     = fields[1].Get<uint32>();
    run.state          = CHALLENGE_STATE_RUNNING;
    run.startTime      = startTime;
    run.timerDuration  = fields[3].Get<uint32>();
    run.elapsedTime    = now > startTime ? now - startTime : 0;
    run.deathCount     = fields[4].Get<uint32>();
    run.penaltyTime    = fields[5].Get<uint32>();
    run.bossesKilled   = fields[6].Get<uint32>();
    run.leaderGuid     = ObjectGuid::Create<HighGuid::Player>(fields[7].Get<uint32>());
    run.countdownTimer = 0;
    run.participants   = participants;
    run.affixWeek      = fields[10].Get<uint32>();
    run.restored       = true;
    for (uint32 id : SplitIds(fields[9].Get<std::string>()))
        if (id > AFFIX_NONE && id < AFFIX_MAX)
            run.affixes.push_back(DungeonChallengeAffix(id));

    DungeonInfo const* info = GetDungeonInfo(run.mapId);
    run.totalBosses = info ? info->totalCreatures : 3;

    _activeRuns[instanceId] = run;
    ChallengeRun* restored = &_activeRuns[instanceId];
    map->CustomData.GetDefault<MapChallengeData>("mod-dungeon-challenge")->run = restored;

    // Creature data died with the unloaded map: the mob affixes are drawn again
    // on the living spawns; scaling follows through ProcessCreature.
    AssignAffixesToCreatures(restored, map);

    LOG_INFO("module", ">> mod-dungeon-challenge: Restored run for instance {} (map {}, level {}, {}s elapsed, {}/{} bosses).",
        instanceId, run.mapId, run.difficulty, run.elapsedTime, run.bossesKilled, run.totalBosses);
    return restored;
}

// ============================================================================
// Level uplift (Mythic+ idea 9): a dungeon below level 80 runs at the uplift
// level. Creatures get the base stats of the new level from
// creature_classlevelstats (WotLK column) times their template modifiers;
// their spell damage, which the core does not derive from level, grows with
// the expected player health between the two levels, with diminishing
// returns on the extra above DungeonChallenge.Uplift.DiminishingThreshold.
// The idea follows araxiaonline's mod-mythic-plus; the code and the health
// curve are our own.
// ============================================================================

void DungeonChallengeMgr::LoadExpectedPlayerHealth()
{
    _expectedPlayerHealth.clear();

    // Naked health of the average stock class at each level (base health plus
    // 10 per stamina point above 20), times a gear factor that grows from 1.5
    // at level 0 to 3.0 at level 80.
    QueryResult result = WorldDatabase.Query(
        "SELECT `Level`, AVG(`BaseHP`), AVG(`Stamina`) FROM `player_class_stats` WHERE `Class` <= 11 GROUP BY `Level`");
    if (!result)
    {
        LOG_WARN("module", ">> mod-dungeon-challenge: player_class_stats is empty; uplifted spell damage stays unscaled.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint8 level = fields[0].Get<uint8>();
        double baseHp = fields[1].Get<double>();
        double stamina = fields[2].Get<double>();
        double naked = baseHp + 20.0 + std::max(0.0, stamina - 20.0) * 10.0;
        double gear = 1.5 + 1.5 * std::min(1.0, double(level) / 80.0);
        _expectedPlayerHealth[level] = uint32(naked * gear);
    } while (result->NextRow());
}

uint32 DungeonChallengeMgr::GetExpectedPlayerHealth(uint8 level) const
{
    auto it = _expectedPlayerHealth.find(level);
    return it != _expectedPlayerHealth.end() ? it->second : 0;
}

void DungeonChallengeMgr::UpliftCreature(Creature* creature, uint8 level)
{
    CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
    CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(level, cInfo->unit_class);
    if (!stats)
        return;

    auto* data = creature->CustomData.GetDefault<CreatureChallengeData>("mod-dungeon-challenge");
    data->originalLevel = creature->GetLevel();

    // Creature::_GetHealthMod, which is not public.
    float healthMod;
    switch (creature->IsPet() ? 0 : cInfo->rank)
    {
        case CREATURE_ELITE_NORMAL:     healthMod = sWorld->getRate(RATE_CREATURE_NORMAL_HP); break;
        case CREATURE_ELITE_RAREELITE:  healthMod = sWorld->getRate(RATE_CREATURE_ELITE_RAREELITE_HP); break;
        case CREATURE_ELITE_WORLDBOSS:  healthMod = sWorld->getRate(RATE_CREATURE_ELITE_WORLDBOSS_HP); break;
        case CREATURE_ELITE_RARE:       healthMod = sWorld->getRate(RATE_CREATURE_ELITE_RARE_HP); break;
        default:                        healthMod = sWorld->getRate(RATE_CREATURE_ELITE_ELITE_HP); break;
    }

    creature->SetLevel(level);

    uint32 health = std::max<uint32>(1, uint32(std::ceil(stats->BaseHealth[EXPANSION_WRATH_OF_THE_LICH_KING] * cInfo->ModHealth * healthMod)));
    SetCreatureBaseHealth(creature, health);

    uint32 mana = stats->GenerateMana(cInfo);
    creature->SetCreateMana(mana);
    creature->SetStatFlatModifier(UNIT_MOD_MANA, BASE_VALUE, float(mana));

    float baseDamage = stats->BaseDamage[EXPANSION_WRATH_OF_THE_LICH_KING];
    for (WeaponAttackType attack : { BASE_ATTACK, OFF_ATTACK, RANGED_ATTACK })
    {
        creature->SetBaseWeaponDamage(attack, MINDAMAGE, baseDamage);
        creature->SetBaseWeaponDamage(attack, MAXDAMAGE, baseDamage * 1.5f);
    }
    creature->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER, BASE_VALUE, float(stats->AttackPower));
    creature->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER_RANGED, BASE_VALUE, float(stats->RangedAttackPower));
    creature->SetStatFlatModifier(UNIT_MOD_ARMOR, BASE_VALUE, stats->GenerateArmor(cInfo));

    creature->UpdateAllStats();
    creature->SetFullHealth();
    creature->SetPower(POWER_MANA, creature->GetMaxPower(POWER_MANA));
}

int32 DungeonChallengeMgr::ScaleUpliftedSpellDamage(Creature const* creature, int32 damage) const
{
    if (damage <= 0)
        return damage;

    auto const* data = creature->CustomData.Get<CreatureChallengeData>("mod-dungeon-challenge");
    if (!data || !data->originalLevel || data->originalLevel >= creature->GetLevel())
        return damage;

    uint32 from = GetExpectedPlayerHealth(data->originalLevel);
    uint32 to = GetExpectedPlayerHealth(creature->GetLevel());
    if (!from || !to)
        return damage;

    double extra = double(damage) * double(to) / double(from) - double(damage);
    if (extra > _upliftDrThreshold)
        extra = _upliftDrThreshold + std::pow(extra - _upliftDrThreshold, double(_upliftDrExponent));
    return int32(std::min<double>(double(damage) + extra, double(std::numeric_limits<int32>::max())));
}
