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

// ============================================================================
// Constants
// ============================================================================

constexpr uint32 CHALLENGE_GO_ENTRY          = 500002;  // default GameObject entry
constexpr uint32 SNAPSHOT_RELOAD_INTERVAL    = 600000;  // 10 minutes in ms

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
    AFFIX_HELL_TOUCHED      = 10, // Extra 666 fire+shadow dmg, -10% stats debuff (10s, 10 stacks)

    AFFIX_MAX
};

// ============================================================================
// Affix DBC Spell IDs (created manually in spell editor)
// ============================================================================

constexpr uint32 SPELL_AFFIX_SPEEDY              = 900050;  // Aura on NPC: +100% move speed, +10% melee haste
constexpr uint32 SPELL_AFFIX_IMMOLATION          = 900051;  // Aura on NPC: fire visual (damage in C++)
constexpr uint32 SPELL_AFFIX_CC_IMMUNITY         = 900052;  // Aura on NPC: mechanic immunity (all CC)
constexpr uint32 SPELL_AFFIX_HELL_TOUCHED_DEBUFF = 900053;  // Debuff on player: -10% all stats, 10s, stack 10
constexpr uint32 SPELL_AFFIX_HELL_TOUCHED        = 900054;  // Aura on NPC: Hell Touched visual
constexpr uint32 SPELL_AFFIX_DAMAGE_REDUCE       = 900055;  // Aura on NPC: Damage Reduction visual
constexpr uint32 SPELL_AFFIX_BIG_BOY             = 900056;  // Aura on NPC: size increase
constexpr uint32 SPELL_AFFIX_BIGGER_BOY          = 900057;  // Aura on NPC: size + damage increase
constexpr uint32 SPELL_AFFIX_HEAVY_HITS          = 900058;  // Aura on NPC: +33% damage
constexpr uint32 SPELL_AFFIX_LIL_BRO             = 900059;  // Aura on NPC: Lil Bro visual
constexpr uint32 SPELL_AFFIX_CALL_FOR_HELP       = 900060;  // Aura on NPC: Call for Help visual

struct AffixInfo
{
    DungeonChallengeAffix id;
    std::string name;
    std::string description;
    uint32 minDifficulty;      // minimum difficulty level to appear
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
    CHALLENGE_STATE_ABANDONED   = 6   // manually abandoned
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
    float extraDamageMultiplier = 1.0f;    // stored damage multiplier for UnitScript hooks
    std::vector<DungeonChallengeAffix> affixes;  // all assigned affixes
    uint8 lilBroGeneration = 0;            // 0 = original, 1 = first split, 2 = second split (no more)
    bool noLoot = false;                   // for LIL_BRO non-lootable copy
    bool hasCalled = false;                // for CALL_FOR_HELP (already triggered?)
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
    void LoadLeaderboard();
    void LoadSpellOverrides();
    void LoadSnapshots();

    // Getters
    bool IsEnabled() const { return _enabled; }
    uint32 GetMaxDifficulty() const { return _maxDifficulty; }
    uint32 GetNpcEntry() const { return _npcEntry; }
    float GetHealthMultiplier(uint32 difficulty) const;
    float GetDamageMultiplier(uint32 difficulty) const;
    uint32 GetTimerForDungeon(uint32 mapId) const;
    uint32 GetDeathPenalty() const { return _deathPenaltySeconds; }
    uint32 GetGameObjectEntry() const { return _gameObjectEntry; }

    // Dungeon Info
    DungeonInfo const* GetDungeonInfo(uint32 mapId) const;
    std::vector<DungeonInfo> const& GetAllDungeons() const { return _dungeons; }
    bool IsDungeonCapable(uint32 mapId) const;

    // Affix Info
    AffixInfo const* GetAffixInfo(DungeonChallengeAffix affix) const;
    std::vector<AffixInfo> const& GetAllAffixes() const { return _affixes; }
    std::vector<DungeonChallengeAffix> GetAffixesForDifficulty(uint32 difficulty) const;

    // Spell Override
    SpellOverrideEntry const* GetSpellOverride(uint32 spellId, uint32 mapId) const;

    // Challenge Run Management
    ChallengeRun* GetChallengeRun(uint32 instanceId);
    ChallengeRun* CreateChallengeRun(uint32 instanceId, uint32 mapId, uint32 difficulty, Player* leader);
    void RemoveChallengeRun(uint32 instanceId);
    void StartRun(ChallengeRun* run);
    void UpdateRun(ChallengeRun* run, uint32 diff);
    void CompleteRun(ChallengeRun* run);
    void FailRun(ChallengeRun* run);

    // Affix Assignment
    void AssignAffixesToCreatures(ChallengeRun* run, Map* map);
    void ApplyAffixToCreature(Creature* creature, DungeonChallengeAffix affix, uint32 difficulty);
    void ScaleCreatureForDifficulty(Creature* creature, uint32 difficulty);
    void ProcessCreature(Creature* creature, Map* map);

    // Snapshots
    void SaveBossKillSnapshot(ChallengeRun const* run, Creature* boss, bool isFinalBoss, bool rewarded);
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
    uint32 _npcEntry;
    bool _announceOnLogin;
    uint32 _deathPenaltySeconds;
    uint32 _gameObjectEntry;
    uint32 _paragonXPPerLevel;

    // Data
    std::vector<DungeonInfo> _dungeons;
    std::vector<AffixInfo> _affixes;
    std::unordered_map<uint32, ChallengeRun> _activeRuns; // instanceId -> run
    std::unordered_map<uint32, std::vector<LeaderboardEntry>> _leaderboard; // mapId -> entries
    std::vector<SpellOverrideEntry> _spellOverrides;
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
