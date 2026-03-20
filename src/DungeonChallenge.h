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
    AFFIX_NONE          = 0,
    AFFIX_BOLSTERING    = 1,  // On death: nearby allies gain +20% damage/health
    AFFIX_RAGING        = 2,  // Below 30% HP: +50% damage (enrage)
    AFFIX_SANGUINE      = 3,  // On death: leaves healing pool for other mobs
    AFFIX_NECROTIC      = 4,  // Melee attacks apply stacking healing reduction
    AFFIX_BURSTING      = 5,  // On death: AoE damage to all players (stacking)
    AFFIX_EXPLOSIVE     = 6,  // Periodically spawns explosive orb
    AFFIX_FORTIFIED     = 7,  // +40% HP, +20% damage (non-boss)
    AFFIX_VOLCANIC      = 8,  // Spawns fire patches under ranged players
    AFFIX_STORMING      = 9,  // Spawns moving tornado near mob
    AFFIX_INSPIRING     = 10, // Nearby allies cannot be interrupted/CC'd

    AFFIX_MAX
};

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
    bool hasEnraged = false;               // for RAGING affix tracking
    bool isCopy = false;                   // for MULTIPLE_ENEMIES affix

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
