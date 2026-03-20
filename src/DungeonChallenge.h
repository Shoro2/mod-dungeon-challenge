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

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <random>

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
// Challenge Run State
// ============================================================================

enum ChallengeState : uint8
{
    CHALLENGE_STATE_NONE        = 0,
    CHALLENGE_STATE_PREPARING   = 1,  // players selected difficulty, waiting to start
    CHALLENGE_STATE_RUNNING     = 2,  // timer ticking, dungeon in progress
    CHALLENGE_STATE_COMPLETED   = 3,  // all bosses killed, timer stopped
    CHALLENGE_STATE_FAILED      = 4,  // all players dead or left
    CHALLENGE_STATE_ABANDONED   = 5   // manually abandoned
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
    uint32 totalBosses;
    uint32 bossesKilled;
    ObjectGuid leaderGuid;
    std::unordered_set<ObjectGuid> participants;
    std::unordered_set<ObjectGuid> affixedCreatures;  // creatures that got affixes
    std::unordered_map<ObjectGuid, DungeonChallengeAffix> creatureAffixes;

    bool IsTimedOut() const
    {
        return elapsedTime >= timerDuration;
    }

    bool AllBossesKilled() const
    {
        return bossesKilled >= totalBosses;
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

    // Getters
    bool IsEnabled() const { return _enabled; }
    uint32 GetMaxDifficulty() const { return _maxDifficulty; }
    uint32 GetNpcEntry() const { return _npcEntry; }
    float GetHealthMultiplier(uint32 difficulty) const;
    float GetDamageMultiplier(uint32 difficulty) const;
    uint32 GetTimerForDungeon(uint32 mapId) const;

    // Dungeon Info
    DungeonInfo const* GetDungeonInfo(uint32 mapId) const;
    std::vector<DungeonInfo> const& GetAllDungeons() const { return _dungeons; }

    // Affix Info
    AffixInfo const* GetAffixInfo(DungeonChallengeAffix affix) const;
    std::vector<AffixInfo> const& GetAllAffixes() const { return _affixes; }
    std::vector<DungeonChallengeAffix> GetAffixesForDifficulty(uint32 difficulty) const;

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

    // Leaderboard
    void SaveRunToLeaderboard(ChallengeRun const* run);
    std::vector<LeaderboardEntry> GetLeaderboard(uint32 mapId, uint32 difficulty, uint32 limit = 10) const;
    std::vector<LeaderboardEntry> GetPlayerBestRuns(ObjectGuid playerGuid, uint32 limit = 10) const;

    // Rewards
    void DistributeRewards(ChallengeRun const* run);

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

    // Data
    std::vector<DungeonInfo> _dungeons;
    std::vector<AffixInfo> _affixes;
    std::unordered_map<uint32, ChallengeRun> _activeRuns; // instanceId -> run
    std::unordered_map<uint32, std::vector<LeaderboardEntry>> _leaderboard; // mapId -> entries

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
