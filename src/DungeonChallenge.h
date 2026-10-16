#ifndef MOD_DUNGEON_CHALLENGE_H
#define MOD_DUNGEON_CHALLENGE_H

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "InstanceScript.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ObjectMgr.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "DataMap.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <random>
#include <algorithm>

// ============================================================================
// Constants
// ============================================================================

constexpr uint32 CHALLENGE_GO_ENTRY          = 500002;  // default GameObject entry
constexpr uint32 SNAPSHOT_RELOAD_INTERVAL    = 600000;  // 10 minutes in ms
constexpr uint32 NPC_LIGHTNING_SPHERE        = 500100;  // run affix Lightning Sphere (06-custom-ids)
constexpr uint32 SECONDS_PER_WEEK            = 604800;
// The affix rotation turns on Wednesday 00:00 UTC: the Unix epoch was a
// Thursday, so a week index counted from six days later starts on a Wednesday.
constexpr uint32 ROTATION_EPOCH_OFFSET       = 6 * 86400;

// ============================================================================
// Affix Definitions
// ============================================================================

enum DungeonChallengeAffix : uint32
{
    AFFIX_NONE              = 0,
    AFFIX_CALL_FOR_HELP     = 1,  // Call allies within 30y when entering combat
    AFFIX_SPEEDY            = 2,  // +100% movement speed, +10% attack speed (DBC)
    AFFIX_BIG_BOY           = 3,  // +50% HP, size via DBC
    AFFIX_IMMOLATION        = 4,  // Periodic fire damage = difficulty * 80
    AFFIX_CC_IMMUNITY       = 5,  // Immune to all crowd control (DBC)
    AFFIX_HEAVY_HITS        = 6,  // +33% damage (DBC)
    AFFIX_LIL_BRO           = 7,  // On death: split 1->2->4 with -90% HP each tier
    AFFIX_DAMAGE_REDUCE     = 8,  // Allies within 30y take -25% damage
    AFFIX_BIGGER_BOY        = 9,  // Additional +50% HP, size/damage via DBC
    AFFIX_HELL_TOUCHED      = 10, // Extra 666 fire+shadow dmg, -2% stats debuff per stack (10s, 10 stacks)
    AFFIX_LIGHTNING_SPHERE  = 11, // run affix: spheres appear that pulse damage to every player until killed
    AFFIX_ENTANGLING_ROOTS  = 12, // run affix: a nearby enemy roots a player now and then
    AFFIX_ENRAGE            = 13, // run affix: enemies in combat enrage now and then

    AFFIX_MAX
};

// Mob affixes go on the affixed share of the mobs; run affixes act on the
// whole instance (dungeon_challenge_affix.kind).
enum DungeonChallengeAffixKind : uint8
{
    AFFIX_KIND_MOB = 0,
    AFFIX_KIND_RUN = 1,
};

// ============================================================================
// Affix DBC Spell IDs (created manually in spell editor)
// ============================================================================

constexpr uint32 SPELL_AFFIX_SPEEDY              = 900050;  // Aura on NPC: +100% move speed, +10% melee haste
constexpr uint32 SPELL_AFFIX_IMMOLATION          = 900051;  // Aura on NPC: fire visual (damage in C++)
constexpr uint32 SPELL_AFFIX_CC_IMMUNITY         = 900052;  // Aura on NPC: mechanic immunity (all CC)
constexpr uint32 SPELL_AFFIX_HELL_TOUCHED_DEBUFF = 900053;  // Debuff on player: -2% all stats per stack, 10s, stack 10
constexpr uint32 SPELL_AFFIX_HELL_TOUCHED        = 900054;  // Aura on NPC: Hell Touched visual
constexpr uint32 SPELL_AFFIX_DAMAGE_REDUCE       = 900055;  // Aura on NPC: Damage Reduction visual
constexpr uint32 SPELL_AFFIX_BIG_BOY             = 900056;  // Aura on NPC: size increase
constexpr uint32 SPELL_AFFIX_BIGGER_BOY          = 900057;  // Aura on NPC: size + damage increase
constexpr uint32 SPELL_AFFIX_HEAVY_HITS          = 900058;  // Aura on NPC: +33% damage
constexpr uint32 SPELL_AFFIX_LIL_BRO             = 900059;  // Aura on NPC: Lil Bro visual
constexpr uint32 SPELL_AFFIX_CALL_FOR_HELP       = 900060;  // Aura on NPC: Call for Help visual
constexpr uint32 SPELL_ENTANGLING_ROOTS          = 57095;   // stock: the Entangling Roots run affix
constexpr uint32 SPELL_ENRAGE                    = 55285;   // stock: the Enrage run affix
constexpr uint32 SPELL_LIGHTNING_VISUAL          = 61585;   // stock: the Lightning Sphere's pulse visual

// One row of world `dungeon_challenge_affix` (or the built-in default when the
// table is empty). The behaviour stays in code; the row decides whether and
// from which level an affix appears, whether it rotates weekly, and its numbers.
struct AffixInfo
{
    DungeonChallengeAffix id;
    std::string name;
    std::string description;
    uint32 minDifficulty;      // minimum difficulty level to appear
    DungeonChallengeAffixKind kind = AFFIX_KIND_MOB;
    bool weekly = false;       // part of the weekly rotation instead of always on
    bool enabled = true;
    float value1 = 0.0f;       // affix-specific number, see the seed SQL
    float value2 = 0.0f;
    uint32 spellId = 0;        // shown as a spell link in the UI
};

// One row of world `dungeon_challenge_scale`: per-dungeon factors on top of the
// level multipliers, and the level-uplift target for low-level dungeons.
struct DungeonScale
{
    float trashHealth = 1.0f;
    float trashDamage = 1.0f;
    float bossHealth  = 1.0f;
    float bossDamage  = 1.0f;
    uint8 upliftLevel = 0;     // 0 = no uplift; else trash at this level, elites +1, bosses +2
};

// One row of world `dungeon_challenge_reward`: an item paid for a completed run
// whose level lies in [minLevel, maxLevel]; the count doubles in time.
struct RewardEntry
{
    uint32 minLevel;
    uint32 maxLevel;
    uint32 itemEntry;
    uint32 itemCount;
};

// ============================================================================
// Dungeon Data
// ============================================================================

struct DungeonInfo
{
    uint32 mapId;
    std::string name;
    float entranceX, entranceY, entranceZ, entranceO;
    uint32 timerMinutes;       // override for base timer
    uint32 totalCreatures;     // approximate creature count (cached)
    uint32 raidDifficulty = 0; // raid-type maps only: forced raid size (0 = 10n, 1 = 25n)
};

// ============================================================================
// Spell Override (per-spell damage tuning via DB)
// ============================================================================

struct SpellOverrideEntry
{
    uint32 spellId;
    uint32 mapId;       // 0 = all maps
    float modPct;       // direct damage modifier, -1 = no override
    float dotModPct;    // DoT damage modifier, -1 = no override
};

// ============================================================================
// Boss Kill Snapshot (detailed per-boss record)
// ============================================================================

struct BossKillSnapshot
{
    uint32 id;
    uint32 instanceId;
    uint32 mapId;
    uint32 difficulty;
    uint32 startTime;
    uint32 snapTime;        // time when boss was killed (elapsed seconds)
    uint32 timerLimit;
    uint32 creatureEntry;
    std::string creatureName;
    bool isFinalBoss;
    bool rewarded;
    uint32 deaths;
    uint32 penaltyTime;     // accumulated death penalty in seconds
    std::string playerName;
    uint32 playerGuid;
    uint32 combatTime = 0;  // seconds from the first player hit to the kill
};

// ============================================================================
// Challenge Run State
// ============================================================================

enum ChallengeState : uint8
{
    CHALLENGE_STATE_NONE        = 0,
    CHALLENGE_STATE_PREPARING   = 1,  // players selected difficulty, waiting to start
    CHALLENGE_STATE_COUNTDOWN   = 2,  // keystone used, countdown active
    CHALLENGE_STATE_RUNNING     = 3,  // timer ticking, dungeon in progress
    CHALLENGE_STATE_COMPLETED   = 4,  // all bosses killed, timer stopped
    CHALLENGE_STATE_FAILED      = 5,  // all players dead or left
    CHALLENGE_STATE_ABANDONED   = 6,  // manually abandoned
    CHALLENGE_STATE_SUMMARY     = 7   // run ended (clear or death); summary shown, repop blocked, awaiting exit
};

struct ChallengeRun
{
    uint32 instanceId;
    uint32 mapId;
    uint32 difficulty;
    ChallengeState state;
    uint32 startTime;          // server time when started
    uint32 timerDuration;      // total allowed time in seconds
    uint32 elapsedTime;        // current elapsed seconds
    uint32 deathCount;
    uint32 penaltyTime;        // accumulated death penalty in seconds
    uint32 totalBosses;
    uint32 bossesKilled;
    uint32 countdownTimer;     // seconds remaining in countdown
    ObjectGuid leaderGuid;
    std::unordered_set<ObjectGuid> participants;
    std::unordered_set<ObjectGuid> affixedCreatures;  // creatures that got affixes
    std::unordered_map<ObjectGuid, std::vector<DungeonChallengeAffix>> creatureAffixes;
    std::unordered_map<ObjectGuid, WorldLocation> participantOrigins;  // pre-teleport positions
    std::vector<DungeonChallengeAffix> affixes;        // every affix of this run (mob and run affixes), fixed at its start
    uint32 affixWeek = 0;                  // rotation week the affixes were drawn from
    uint32 sphereTimerMs = 0;              // AFFIX_LIGHTNING_SPHERE spawn clock
    std::unordered_map<ObjectGuid, uint32> rootTimersMs;  // AFFIX_ENTANGLING_ROOTS clock per player
    bool restored = false;                 // rebuilt from dungeon_challenge_active after a restart or unload
    DungeonChallengeAffix forcedAffix = AFFIX_NONE; // .dchallenge force: the run affix's next roll fires now
    std::string lastBossName;              // the last boss credited, and its fight (.dchallenge status)
    uint32 lastBossCombatTime = 0;
    uint32 completionDelayMs = 0;          // countdown for post-completion teleport (ms)
    uint32 summaryElapsedMs = 0;           // ms elapsed in SUMMARY state (drives the C++ safety-fallback teleport)
    bool   endedByDeath     = false;       // true if the run ended by death/wipe (vs. a clear)

    bool IsTimedOut() const
    {
        return (elapsedTime + penaltyTime) >= timerDuration;
    }

    bool AllBossesKilled() const
    {
        return bossesKilled >= totalBosses;
    }

    uint32 GetEffectiveElapsed() const
    {
        return elapsedTime + penaltyTime;
    }

    bool HasAffix(DungeonChallengeAffix a) const
    {
        return std::find(affixes.begin(), affixes.end(), a) != affixes.end();
    }
};

// ============================================================================
// DataMap: Custom data attached to Map instances
// ============================================================================

struct MapChallengeData : public DataMap::Base
{
    ChallengeRun* run = nullptr;          // active challenge run (owned by DungeonChallengeMgr)
    bool isLockedNonChallenge = false;     // true if a creature was killed without a challenge active
};

// ============================================================================
// DataMap: Custom data attached to Creature instances
// ============================================================================

struct CreatureChallengeData : public DataMap::Base
{
    bool processed = false;                // already scaled for difficulty
    uint32 originalHealth = 0;             // health before scaling
    uint32 baseCreateHealth = 0;           // unscaled create health of this life (0 = take it at the next scaling)
    bool scaled = false;                   // difficulty scaling is applied (heals on it are scaled too)
    float extraDamageMultiplier = 1.0f;    // stored damage multiplier for UnitScript hooks
    float healthMultiplier = 1.0f;         // health factor applied (level x dungeon factor); mob heals on it scale with it
    uint64 engageTimeMs = 0;               // bosses: game time of the first player hit (0 = not engaged)
    uint32 enrageTimerMs = 0;              // AFFIX_ENRAGE clock
    uint8 originalLevel = 0;               // level before an uplift (0 = not uplifted)
    std::vector<DungeonChallengeAffix> affixes;  // all assigned affixes
    uint8 lilBroGeneration = 0;            // 0 = original, 1 = first split, 2 = second split (no more)
    bool noLoot = false;                   // for LIL_BRO non-lootable copy
    bool hasCalled = false;                // for CALL_FOR_HELP (already triggered?)
    bool affixesApplied = false;           // true when affix auras/HP are actively applied
    uint32 immolationTimer = 0;            // for IMMOLATION periodic tick (ms)
    float incomingDamageReduction = 0.0f;  // for DAMAGE_REDUCE aura on nearby allies

    // Mob Respawn mechanic: non-boss, non-affix mobs respawn at their original
    // position when pulled, up to 2 times with a 3-second delay each.
    bool combatTracked = false;            // has combat entry been detected?
    bool isRespawnCopy = false;            // true for spawned copies (no further respawns)
    bool copyWasInCombat = false;          // tracks if a respawn copy has entered combat (for evade despawn)
    uint8 respawnsTriggered = 0;           // how many respawns have been triggered (max 2)
    uint32 respawnTimer = 0;               // countdown timer in ms (3000ms = 3s)
    float homeX = 0.0f;                    // original spawn position
    float homeY = 0.0f;
    float homeZ = 0.0f;
    float homeO = 0.0f;

    bool HasAffix(DungeonChallengeAffix a) const
    {
        for (auto const& af : affixes)
            if (af == a)
                return true;
        return false;
    }
};

// ============================================================================
// Leaderboard Entry
// ============================================================================

struct LeaderboardEntry
{
    uint32 rank;
    uint32 mapId;
    uint32 difficulty;
    uint32 completionTime;     // seconds
    uint32 deathCount;
    std::string leaderName;
    std::string dateCompleted;
    std::vector<std::string> participants;
};

// ============================================================================
// DungeonChallengeMgr - Singleton Manager
// ============================================================================

class DungeonChallengeMgr
{
public:
    static DungeonChallengeMgr* Instance();

    // Configuration
    void LoadConfig(bool reload);
    void LoadDungeonData();
    void LoadAffixData();
    void LoadBossGroups();
    void LoadLeaderboard();
    void LoadSpellOverrides();
    void LoadSnapshots();
    void LoadScales();
    void LoadRewards();
    void LoadBossEntries();
    void LoadExpectedPlayerHealth();
    void LoadTuningTables();   // affixes, scales, rewards, boss entries (.dchallenge reload)

    // Getters
    bool IsEnabled() const { return _enabled; }
    uint32 GetMaxDifficulty() const { return _maxDifficulty; }

    float GetHealthMultiplier(uint32 difficulty) const;
    float GetDamageMultiplier(uint32 difficulty) const;
    uint32 GetTimerForDungeon(uint32 mapId) const;
    uint32 GetDeathPenalty() const { return _deathPenaltySeconds; }
    uint32 GetGameObjectEntry() const { return _gameObjectEntry; }
    uint32 GetSummarySeconds() const { return _summarySeconds; }       // visible auto-leave countdown (s)
    uint32 GetDeathEndsRunMode() const { return _deathEndsRunMode; }   // 0 = wipe, 1 = any death
    uint32 GetFallbackSeconds() const { return _fallbackSeconds; }     // C++ safety teleport threshold (s)

    // Dungeon Info
    DungeonInfo const* GetDungeonInfo(uint32 mapId) const;
    std::vector<DungeonInfo> const& GetAllDungeons() const { return _dungeons; }
    bool IsDungeonCapable(uint32 mapId) const;

    // Affix Info
    AffixInfo const* GetAffixInfo(DungeonChallengeAffix affix) const;
    std::vector<AffixInfo> const& GetAllAffixes() const { return _affixes; }
    // Every affix a run at this level gets in the current rotation week
    // (always-on affixes from their level, plus this week's weekly ones).
    std::vector<DungeonChallengeAffix> GetAffixesForDifficulty(uint32 difficulty) const;
    std::vector<DungeonChallengeAffix> GetAffixesForDifficulty(uint32 difficulty, uint32 week) const;
    std::vector<DungeonChallengeAffix> GetWeeklyAffixes(uint32 week) const;
    uint32 GetCurrentWeek() const;
    void SetWeekPin(uint32 week) { _weekPin = week; }   // 0 = follow the real week
    uint32 GetWeekPin() const { return _weekPin; }
    uint32 GetWeeklyAffixCount() const { return _weeklyAffixCount; }
    float GetAffixValue(DungeonChallengeAffix affix, uint8 index, float fallback) const;

    // Per-dungeon factors (idea 4) and boss detection
    DungeonScale const& GetScale(uint32 mapId) const;
    void SetScale(uint32 mapId, DungeonScale const& scale) { _scales[mapId] = scale; }
    bool IsChallengeBoss(Creature const* creature) const;
    float GetCreatureHealthMultiplier(Creature const* creature, uint32 difficulty) const;
    float GetCreatureDamageMultiplier(Creature const* creature, uint32 difficulty) const;

    // Level uplift for low-level dungeons (idea 9)
    void UpliftCreature(Creature* creature, uint8 level);
    int32 ScaleUpliftedSpellDamage(Creature const* creature, int32 damage) const;
    uint32 GetExpectedPlayerHealth(uint8 level) const;

    // Rewards table (idea 7)
    std::vector<RewardEntry> GetRewardsForLevel(uint32 level) const;

    // Run persistence across restarts and unloads (idea 10)
    void SaveActiveRun(ChallengeRun const* run);
    void DeleteActiveRun(uint32 instanceId);
    ChallengeRun* TryRestoreRun(Player* player, Map* map);
    void CleanupActiveRuns();

    // Run affixes that act on the whole instance (idea 6)
    void UpdateRunAffixes(ChallengeRun* run, Map* map, uint32 diff);
    void UpdateCreatureRunAffixes(ChallengeRun* run, Creature* creature, uint32 diff);

    // Boss Groups (multi-mob encounters counted as one boss)
    uint32 GetBossGroupId(uint32 creatureEntry) const;

    // Spell Override
    SpellOverrideEntry const* GetSpellOverride(uint32 spellId, uint32 mapId) const;

    // Challenge Run Management
    ChallengeRun* GetChallengeRun(uint32 instanceId);
    ChallengeRun* GetChallengeRunByParticipant(ObjectGuid playerGuid);
    ChallengeRun* CreateChallengeRun(uint32 instanceId, uint32 mapId, uint32 difficulty, Player* leader);
    void RemoveChallengeRun(uint32 instanceId);
    void StartRun(ChallengeRun* run);
    void UpdateRun(ChallengeRun* run, uint32 diff);
    void CompleteRun(ChallengeRun* run);
    void FailRun(ChallengeRun* run);
    void EndRunByDeath(ChallengeRun* run);                          // run ended by death/wipe -> SUMMARY state
    void WriteRunEndSignal(ChallengeRun const* run, bool clear);    // C++ -> Lua run-end signal (DB row per participant)

    // Affix Assignment
    void AssignAffixesToCreatures(ChallengeRun* run, Map* map);
    void ApplyAffixToCreature(Creature* creature, DungeonChallengeAffix affix, uint32 difficulty);
    void ScaleCreatureForDifficulty(Creature* creature, uint32 difficulty);
    void ProcessCreature(Creature* creature, Map* map);

    // Sets a creature's health through the carriers Creature::SelectLevel uses
    // (create health + UNIT_MOD_HEALTH base value), so Creature::UpdateMaxHealth,
    // which runs whenever a health aura is applied or removed, keeps it.
    static void SetCreatureBaseHealth(Creature* creature, uint32 health);

    // Pets, guardians, player totems and vehicles, and anything a player or a
    // player's pet summoned. These are never scaled, never respawn-copied and
    // never get their heals scaled.
    static bool IsPlayerSideCreature(Creature* creature);

    // Snapshots
    void SaveBossKillSnapshot(ChallengeRun const* run, Creature* boss, bool isFinalBoss, bool rewarded, uint32 combatTime);
    std::vector<BossKillSnapshot> GetSnapshotsForDungeon(uint32 mapId, uint32 difficulty, uint32 limit = 20) const;

    // Leaderboard
    void SaveRunToLeaderboard(ChallengeRun const* run);
    std::vector<LeaderboardEntry> GetLeaderboard(uint32 mapId, uint32 difficulty, uint32 limit = 10) const;
    std::vector<LeaderboardEntry> GetPlayerBestRuns(ObjectGuid playerGuid, uint32 limit = 10) const;

    // Rewards
    void DistributeRewards(ChallengeRun const* run);

    // Non-Mythic Lock
    void LockInstanceAsNonChallenge(Map* map);
    bool IsInstanceLocked(Map* map) const;

private:
    DungeonChallengeMgr();
    ~DungeonChallengeMgr() = default;

    // Config values
    bool _enabled;
    uint32 _affixPercentage;
    uint32 _maxDifficulty;
    float _healthMultPerLevel;
    float _damageMultPerLevel;
    uint32 _timerBaseMinutes;
    uint32 _lootBonusPerLevel;

    bool _announceOnLogin;
    uint32 _deathPenaltySeconds;
    uint32 _gameObjectEntry;
    uint32 _paragonXPPerLevel;
    uint32 _summarySeconds;     // visible auto-leave countdown (default 30)
    uint32 _deathEndsRunMode;   // 0 = end on WIPE, 1 = end on ANY death (default 0)
    uint32 _fallbackSeconds;    // C++ safety teleport threshold, should be > _summarySeconds (default 40)
    uint32 _runCredit;          // kill credit per completed run for the call board's run quests (default 402451, 0 = off)
    uint32 _weeklyAffixCount;   // weekly affixes drawn per rotation week (default 2)
    uint32 _weekPin = 0;        // GM preview/test: a fixed rotation week (.dchallenge week), 0 = the real one; never saved
    uint32 _restoreWindowMinutes; // a saved run older than this is not restored (default 120)
    float _upliftDrThreshold;   // uplifted spell damage: extra damage above this grows with diminishing returns (default 10000)
    float _upliftDrExponent;    // ... with this exponent (default 0.975)

    // Data
    std::vector<DungeonInfo> _dungeons;
    std::vector<AffixInfo> _affixes;
    std::unordered_map<uint32, uint32> _bossGroupByEntry; // creature entry -> boss group id
    std::unordered_map<uint32, ChallengeRun> _activeRuns; // instanceId -> run
    std::unordered_map<uint32, std::vector<LeaderboardEntry>> _leaderboard; // mapId -> entries
    std::vector<SpellOverrideEntry> _spellOverrides;
    std::unordered_map<uint32, DungeonScale> _scales;      // mapId -> factors
    std::vector<RewardEntry> _rewards;
    std::unordered_set<uint32> _bossEntries;               // creature entries that count as bosses below rank 3
    std::unordered_map<uint8, uint32> _expectedPlayerHealth; // level -> expected player health (uplift spell rule)
    // Snapshots: mapId -> vector of snapshots
    std::unordered_map<uint32, std::vector<BossKillSnapshot>> _snapshots;

    // Random
    std::mt19937 _rng;
};

#define sDungeonChallengeMgr DungeonChallengeMgr::Instance()

// ============================================================================
// Pending Challenge Data (shared between NPC and scripts)
// ============================================================================

struct PendingChallengeInfo
{
    uint32 mapId;
    uint32 difficulty;
};

class DungeonChallengePendingStore
{
public:
    static DungeonChallengePendingStore* Instance()
    {
        static DungeonChallengePendingStore instance;
        return &instance;
    }

    void AddPending(ObjectGuid leaderGuid, uint32 mapId, uint32 difficulty)
    {
        _pending[leaderGuid] = { mapId, difficulty };
    }

    PendingChallengeInfo const* GetPending(ObjectGuid leaderGuid) const
    {
        auto it = _pending.find(leaderGuid);
        if (it != _pending.end())
            return &it->second;
        return nullptr;
    }

    void RemovePending(ObjectGuid leaderGuid)
    {
        _pending.erase(leaderGuid);
    }

private:
    std::unordered_map<ObjectGuid, PendingChallengeInfo> _pending;
};

#define sDungeonChallengePending DungeonChallengePendingStore::Instance()

#endif // MOD_DUNGEON_CHALLENGE_H
