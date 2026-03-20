# CLAUDE.md — mod-dungeon-challenge

Technical documentation for AI assistants working on this module.

## Module Overview

**mod-dungeon-challenge** is an AzerothCore 3.3.5a module that implements a Mythic+-style dungeon challenge system. Players click a **Dungeon Challenge Stone** (GameObject) to select a dungeon and difficulty level (1-20) via a **Lua gossip UI**, are teleported (solo or group), and must defeat all bosses within a timer. ~5% of dungeon mobs receive random affixes (special abilities/modifiers).

**Key design decisions:**
- **Solo entry supported** — no group required, 1-5 players
- **GameObject + Lua UI** — interaction via clickable stone, gossip menus in Eluna Lua
- **No keystone item** — challenges start immediately on dungeon entry
- **Lua ↔ C++ communication** — via `dungeon_challenge_pending` DB table

## Architecture

### Directory Structure

```
mod-dungeon-challenge/
├── CMakeLists.txt                              # AzerothCore build integration
├── include.sh                                  # SQL path registration
├── conf/
│   └── mod_dungeon_challenge.conf.dist         # Configuration template
├── data/sql/
│   ├── db-world/
│   │   └── 00_dungeon_challenge_world.sql      # NPC, GameObject, dungeon table, spell overrides
│   └── db-characters/
│       └── 00_dungeon_challenge_characters.sql  # Leaderboard, history, snapshots, pending
├── lua_scripts/
│   └── dungeon_challenge_gameobject.lua        # Eluna Lua UI for GameObject gossip
├── src/
│   ├── DungeonChallenge.h                      # Header: All data structures + manager + DataMap
│   ├── DungeonChallenge.cpp                    # Singleton manager implementation
│   ├── DungeonChallengeNpc.cpp                 # NPC gossip menus (CreatureScript, fallback)
│   ├── DungeonChallengeScripts.cpp             # Hooks: WorldScript, PlayerScript, CreatureScript, UnitScript
│   └── mod_dungeon_challenge_loader.cpp        # Entry point
├── CLAUDE.md                                   # This document
└── README.md                                   # User guide
```

### Core Classes

| Class/Struct | File | Purpose |
|--------------|------|---------|
| `DungeonChallengeMgr` | DungeonChallenge.h/cpp | Singleton manager: config, runs, affixes, leaderboard, snapshots, spell overrides |
| `ChallengeRun` | DungeonChallenge.h | State of an active run (timer, bosses, deaths, penalty) |
| `MapChallengeData` | DungeonChallenge.h | DataMap::Base for Map instances (run reference, non-mythic lock) |
| `CreatureChallengeData` | DungeonChallenge.h | DataMap::Base for Creatures (processed, originalHealth, damageMultiplier, affix) |
| `SpellOverrideEntry` | DungeonChallenge.h | Per-spell damage tuning via DB |
| `BossKillSnapshot` | DungeonChallenge.h | Detailed boss kill record for leaderboards |
| `DungeonInfo` | DungeonChallenge.h | Dungeon metadata (map, entrance, timer, boss count) |
| `AffixInfo` | DungeonChallenge.h | Affix definition (name, description, min difficulty) |
| `LeaderboardEntry` | DungeonChallenge.h | Leaderboard entry |
| `DungeonChallengePendingStore` | DungeonChallenge.h | In-memory pending challenges (NPC fallback path) |
| `npc_dungeon_challenge` | DungeonChallengeNpc.cpp | CreatureScript for the gossip NPC (fallback) |
| `DungeonChallengeWorldScript` | DungeonChallengeScripts.cpp | Config loading, startup, pending cleanup |
| `DungeonChallengePlayerScript` | DungeonChallengeScripts.cpp | Login, map change (solo+group), death |
| `DungeonChallengeCreatureScript` | DungeonChallengeScripts.cpp | Creature update (Raging affix, DataMap processing) |
| `DungeonChallengeCreatureDeathScript` | DungeonChallengeScripts.cpp | Creature death (boss kill, affixes, non-mythic lock, snapshots) |
| `DungeonChallengeUnitScript` | DungeonChallengeScripts.cpp | Damage modification via UnitScript hooks |
| `DungeonChallengeTimerScript` | DungeonChallengeScripts.cpp | Timer display (AllMapScript) |

### Lua Script

| File | Purpose |
|------|---------|
| `lua_scripts/dungeon_challenge_gameobject.lua` | Eluna script: GameObject gossip UI for dungeon/difficulty selection, leaderboard, snapshots. Communicates with C++ via `dungeon_challenge_pending` DB table. |

### Design Patterns

1. **DataMap Pattern**: Custom data is attached to `Map` and `Creature` objects via `DataMap::Base`. `MapChallengeData` stores run reference and lock status. `CreatureChallengeData` stores processing status, original HP, damage multiplier, and affix.

2. **UnitScript Damage Hooks**: Damage scaling via `ModifyMeleeDamage()`, `ModifySpellDamageTaken()`, and `ModifyPeriodicDamageAurasTick()` hooks. The `extraDamageMultiplier` is stored in `CreatureChallengeData`.

3. **Spell Override System**: Per-spell damage tuning via DB table `dungeon_challenge_spell_override`. Map-specific or global overrides (modPct for direct, dotModPct for DoTs).

4. **Non-Mythic Lock**: If a creature dies before a challenge is active, the instance is locked as "non-challenge". Prevents exploits with partially cleared dungeons.

5. **Lua ↔ C++ Communication**: The Lua GameObject UI writes pending challenge data (player_guid, map_id, difficulty) to the `dungeon_challenge_pending` characters DB table. The C++ `OnPlayerMapChanged` hook reads and deletes this entry when the player enters the dungeon. Stale entries are cleaned up on server startup.

6. **Snapshot-Based Records**: Each boss kill generates a detailed snapshot record per participant in the `dungeon_challenge_snapshot` table.

### Data Flow

```
Player clicks Dungeon Challenge Stone (GameObject)
    ↓
Lua: OnGossipHello → ShowMainMenu → ShowDungeonMenu → ShowDifficultyMenu → ShowConfirmMenu
    ↓
Lua: StartChallengeRun()
    ├─ Validation (group size ≤ 5, optional)
    ├─ INSERT INTO dungeon_challenge_pending (player_guid, map_id, difficulty)
    └─ Teleport (solo or all group members)
    ↓
C++: OnPlayerMapChanged() (PlayerScript hook)
    ├─ Check in-memory pending (NPC fallback) OR DB pending (Lua path)
    ├─ Check non-mythic lock (IsInstanceLocked)
    ├─ CreateChallengeRun() → ChallengeRun object + MapChallengeData link
    ├─ AssignAffixesToCreatures() → ~5% mobs get affixes + DataMap
    └─ StartRun() immediately (State = RUNNING)
    ↓
Dungeon running:
    ├─ OnAllCreatureUpdate() → ProcessCreature() via DataMap + Raging check
    ├─ ModifyMeleeDamage/ModifySpellDamageTaken → Damage via extraDamageMultiplier
    ├─ OnPlayerCreatureKill() → Boss kill + snapshot + affix on-death + non-mythic lock
    ├─ OnPlayerJustDied() → Death counter + penalty (solo or group notification)
    └─ OnMapUpdate() → Timer display with penalty
    ↓
AllBossesKilled()
    ├─ CompleteRun()
    ├─ SaveBossKillSnapshot() (final boss record)
    ├─ SaveRunToLeaderboard() + SaveHistory()
    ├─ DistributeRewards() (gold)
    └─ OnDestroyInstance() → Cleanup
```

## Configuration

| Config Key | Default | Description |
|------------|---------|-------------|
| `DungeonChallenge.Enable` | 1 | Module on/off |
| `DungeonChallenge.AffixPercentage` | 5 | % of mobs with affixes |
| `DungeonChallenge.MaxDifficulty` | 20 | Maximum difficulty level |
| `DungeonChallenge.HealthMultiplierPerLevel` | 15 | +HP% per level |
| `DungeonChallenge.DamageMultiplierPerLevel` | 8 | +DMG% per level (via UnitScript hooks) |
| `DungeonChallenge.TimerBaseMinutes` | 30 | Base timer (fallback) |
| `DungeonChallenge.LootBonusPerLevel` | 50000 | Gold bonus per level (copper) |
| `DungeonChallenge.NpcEntry` | 500000 | Creature entry of the NPC (fallback) |
| `DungeonChallenge.AnnounceOnLogin` | 1 | Login message |
| `DungeonChallenge.DeathPenaltySeconds` | 15 | Time penalty per death (seconds) |
| `DungeonChallenge.GameObjectEntry` | 500002 | GameObject entry for the Dungeon Challenge Stone |

## Scaling Formulas

```
HP Multiplier = 1.0 + (HealthMultiplierPerLevel/100 × Level)
  Level 1:  1.15x  |  Level 5:  1.75x  |  Level 10: 2.50x  |  Level 20: 4.00x

DMG Multiplier = 1.0 + (DamageMultiplierPerLevel/100 × Level)
  Via UnitScript hooks (ModifyMeleeDamage, ModifySpellDamageTaken, ModifyPeriodicDamageAurasTick)
  Stored in CreatureChallengeData::extraDamageMultiplier
  Level 1:  1.08x  |  Level 5:  1.40x  |  Level 10: 1.80x  |  Level 20: 2.60x

Spell Override: dungeon_challenge_spell_override table
  modPct = Direct Damage Modifier (-1 = no override)
  dotModPct = DoT Damage Modifier (-1 = no override)
  Map-specific or global (map_id = 0)

Gold Reward = LootBonusPerLevel × Level × (inTime ? 2 : 1)
  Level 10 in time: 100g  |  Level 10 over time: 50g

Timer penalty per death: +DeathPenaltySeconds (Default: 15s)
  Effective time = elapsedTime + penaltyTime
```

## Affix System

### Available Affixes

| ID | Name | Effect | Min Level |
|----|------|--------|-----------|
| 1 | Bolstering | Death: Nearby allies gain +20% HP/DMG (via DataMap) | 2 |
| 2 | Raging | Below 30% HP: +50% DMG (via extraDamageMultiplier) | 2 |
| 3 | Sanguine | Death: Heals nearby mobs by 20% | 4 |
| 4 | Necrotic | Melee: Stacking healing reduction | 4 |
| 5 | Bursting | Death: 5% MaxHP AoE to all players | 7 |
| 6 | Explosive | Spawns explosive orbs (periodic) | 7 |
| 7 | Fortified | +40% HP, +20% DMG (via extraDamageMultiplier) | 2 |
| 8 | Volcanic | Fire zones under ranged players | 10 |
| 9 | Storming | Moving tornadoes | 10 |
| 10 | Inspiring | Allies immune to CC/interrupt | 14 |

### Affix Assignment

1. Collect all living, non-boss, non-friendly creatures in the dungeon
2. Calculate count: `max(1, totalCreatures × affixPercentage / 100)`
3. Shuffle-and-pick: Random selection + random affix from available pool
4. Affix is only chosen from affixes whose `minDifficulty ≤ current level`
5. Affix is stored in `CreatureChallengeData::affix` (DataMap)

### Implemented Affix Effects

- **Fortified**: On assignment: +40% HP directly, +20% via extraDamageMultiplier (UnitScript)
- **Raging**: Per-tick check in `OnAllCreatureUpdate()` → hasEnraged flag + extraDamageMultiplier *= 1.5
- **Bolstering**: In `OnPlayerCreatureKill()` → +20% HP + extraDamageMultiplier *= 1.2 via DataMap
- **Bursting**: In `OnPlayerCreatureKill()` → `EnvironmentalDamage()` to all players
- **Sanguine**: In `OnPlayerCreatureKill()` → `ModifyHealth()` on nearby mobs

### Not Yet Implemented Affixes (TODO)

- **Necrotic**: Requires SpellScript for healing reduction debuff
- **Explosive**: Requires TempSummon for explosive orb creature
- **Volcanic**: Requires periodic spell + position calculation
- **Storming**: Requires creature movement with tornado visual
- **Inspiring**: Requires aura mechanic for CC/interrupt immunity

## Database Tables

### World DB

| Table | Purpose |
|-------|---------|
| `dungeon_challenge_dungeons` | Dungeon definitions (MapID, entrance, timer, bosses) |
| `dungeon_challenge_spell_override` | Per-spell damage tuning (spellId, mapId, modPct, dotModPct) |
| `creature_template` (Entry 500000) | Challenge NPC (fallback) |
| `gameobject_template` (Entry 500002) | Dungeon Challenge Stone (primary interaction) |

### Characters DB

| Table | Purpose |
|-------|---------|
| `dungeon_challenge_leaderboard` | Leaderboard (top runs per dungeon/level) |
| `dungeon_challenge_history` | All runs of a player |
| `dungeon_challenge_best` | Aggregated best scores |
| `dungeon_challenge_snapshot` | Boss kill records (per boss, per participant) |
| `dungeon_challenge_pending` | Temporary: Lua→C++ challenge data transfer |

## IDs and Ranges

| Type | ID/Range | Usage |
|------|----------|-------|
| NPC Entry | 500000 | Dungeon Challenge NPC (fallback) |
| GameObject Entry | 500002 | Dungeon Challenge Stone (primary) |
| Gossip Actions | 1000-5999 | Menu navigation (Lua + C++) |
| Spell | 8599 | Enrage Visual (Raging affix) |
| Maps | 574-668 | WotLK 5-man dungeons |

## Code Conventions

- AzerothCore C++ standard (see share-public/CLAUDE.md)
- 4-space indentation, no tabs
- `UpperCamelCase` for classes and methods
- `_lowerCamelCase` for private members
- `UPPER_SNAKE_CASE` for enums/constants
- Lua: `local` variables, `camelCase` functions, `UPPER_SNAKE_CASE` constants
- Commit messages: Conventional Commits (feat/fix/docs)
- Language: English for all code, comments, and user-facing strings

## Known Limitations / TODOs

1. **Affix Implementation**: Necrotic, Explosive, Volcanic, Storming, Inspiring are prepared as hooks but not implemented
2. **Boss Detection**: Currently via `creature_template.rank >= 3` → some bosses have rank < 3
3. **Timer UI**: Currently only chat messages → could use WorldState-based UI
4. **Prepared Statements**: Currently format-string-based queries → should use prepared statements
5. **Creature Scaling**: `GetCreatureBySpawnIdStore()` must be verified against correct API
6. **Instance Reset**: No automatic instance reset after run completion
7. **Snapshot Reload**: Snapshots are loaded at startup but not periodically refreshed
8. **Lua CONFIG sync**: Config values in Lua script must be manually kept in sync with worldserver.conf
