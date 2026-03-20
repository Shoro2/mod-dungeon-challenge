# CLAUDE.md — mod-dungeon-challenge

Technical documentation for AI assistants working on this module.

## Module Overview

**mod-dungeon-challenge** is an AzerothCore 3.3.5a module that implements a Mythic+-style dungeon challenge system. Players select a dungeon and difficulty level (1-20) via NPC gossip, are teleported, and must defeat all bosses within a timer. ~5% of dungeon mobs receive random affixes (special abilities/modifiers).

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
│   │   └── 00_dungeon_challenge_world.sql      # NPC, dungeon table, keystone, spell overrides
│   └── db-characters/
│       └── 00_dungeon_challenge_characters.sql  # Leaderboard, history, snapshots
├── src/
│   ├── DungeonChallenge.h                      # Header: All data structures + manager + DataMap
│   ├── DungeonChallenge.cpp                    # Singleton manager implementation
│   ├── DungeonChallengeNpc.cpp                 # NPC gossip menus (CreatureScript)
│   ├── DungeonChallengeScripts.cpp             # Hooks: WorldScript, PlayerScript, CreatureScript, UnitScript, ItemScript
│   └── mod_dungeon_challenge_loader.cpp        # Entry point
├── CLAUDE.md                                   # This document
└── README.md                                   # User guide
```

### Core Classes

| Class/Struct | File | Purpose |
|--------------|------|---------|
| `DungeonChallengeMgr` | DungeonChallenge.h/cpp | Singleton manager: config, runs, affixes, leaderboard, snapshots, spell overrides |
| `ChallengeRun` | DungeonChallenge.h | State of an active run (timer, bosses, deaths, penalty) |
| `MapChallengeData` | DungeonChallenge.h | DataMap::Base for Map instances (run reference, non-mythic lock, keystone status) |
| `CreatureChallengeData` | DungeonChallenge.h | DataMap::Base for Creatures (processed, originalHealth, damageMultiplier, affix) |
| `SpellOverrideEntry` | DungeonChallenge.h | Per-spell damage tuning via DB |
| `BossKillSnapshot` | DungeonChallenge.h | Detailed boss kill record for leaderboards |
| `DungeonInfo` | DungeonChallenge.h | Dungeon metadata (map, entrance, timer, boss count) |
| `AffixInfo` | DungeonChallenge.h | Affix definition (name, description, min difficulty) |
| `LeaderboardEntry` | DungeonChallenge.h | Leaderboard entry |
| `DungeonChallengePendingStore` | DungeonChallenge.h | Stores pending challenges between NPC selection and teleport |
| `npc_dungeon_challenge` | DungeonChallengeNpc.cpp | CreatureScript for the gossip NPC |
| `DungeonChallengeWorldScript` | DungeonChallengeScripts.cpp | Config loading, startup |
| `DungeonChallengePlayerScript` | DungeonChallengeScripts.cpp | Login, map change, death |
| `DungeonChallengeCreatureScript` | DungeonChallengeScripts.cpp | Creature update (Raging affix, DataMap processing) |
| `DungeonChallengeCreatureDeathScript` | DungeonChallengeScripts.cpp | Creature death (boss kill, affixes, non-mythic lock, snapshots) |
| `DungeonChallengeUnitScript` | DungeonChallengeScripts.cpp | Damage modification via UnitScript hooks |
| `DungeonChallengeKeystoneScript` | DungeonChallengeScripts.cpp | ItemScript for keystone activation |
| `DungeonChallengeTimerScript` | DungeonChallengeScripts.cpp | Timer display + keystone countdown (AllMapScript) |

### Design Patterns

1. **DataMap Pattern**: Custom data is attached to `Map` and `Creature` objects via `DataMap::Base`. `MapChallengeData` stores run reference and lock status. `CreatureChallengeData` stores processing status, original HP, damage multiplier, and affix.

2. **UnitScript Damage Hooks**: Damage is no longer scaled by directly modifying base weapon damage, but via `ModifyMeleeDamage()`, `ModifySpellDamageTaken()`, and `ModifyPeriodicDamageAurasTick()` hooks. The `extraDamageMultiplier` is stored in `CreatureChallengeData`.

3. **Spell Override System**: Per-spell damage tuning via DB table `dungeon_challenge_spell_override`. Map-specific or global overrides (modPct for direct, dotModPct for DoTs).

4. **Non-Mythic Lock**: If a creature dies before a keystone is activated, the instance is locked as "non-challenge". Prevents exploits with partially cleared dungeons.

5. **Keystone Item System**: Players buy a keystone from the NPC. After NPC selection and teleport, the keystone must be used inside the dungeon to start the 10-second countdown and then the timer.

6. **Snapshot-Based Records**: Each boss kill generates a detailed snapshot record per participant in the `dungeon_challenge_snapshot` table.

### Data Flow

```
Player talks to NPC
    ↓
ShowMainMenu → ShowDungeonMenu → ShowDifficultyMenu → ShowConfirmMenu
    ↓
StartChallengeRun()
    ├─ Validation (group, leader, size)
    ├─ sDungeonChallengePending->AddPending(leaderGuid, mapId, difficulty)
    └─ TeleportTo() for all group members
    ↓
OnPlayerMapChanged() (PlayerScript hook)
    ├─ Check non-mythic lock (IsInstanceLocked)
    ├─ Check sDungeonChallengePending
    ├─ CreateChallengeRun() → ChallengeRun object + MapChallengeData link
    └─ (Keystone enabled) State = PREPARING, waiting for keystone
    ↓
Keystone used (ItemScript)
    ├─ Validation (leader, group, no combat, not locked)
    ├─ State = COUNTDOWN (10 seconds)
    ├─ AssignAffixesToCreatures() → ~5% mobs get affixes + DataMap
    └─ OnMapUpdate() counts down → StartRun()
    ↓
Dungeon running:
    ├─ OnAllCreatureUpdate() → ProcessCreature() via DataMap + Raging check
    ├─ ModifyMeleeDamage/ModifySpellDamageTaken → Damage via extraDamageMultiplier
    ├─ OnAllCreatureJustDied() → Boss kill + snapshot + affix on-death + non-mythic lock
    ├─ OnPlayerJustDied() → Death counter + penalty (configurable)
    └─ OnMapUpdate() → Timer display with penalty
    ↓
AllBossesKilled()
    ├─ CompleteRun()
    ├─ SaveBossKillSnapshot() (final boss record)
    ├─ SaveRunToLeaderboard() + SaveHistory()
    ├─ DistributeRewards() (gold + new keystone)
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
| `DungeonChallenge.NpcEntry` | 500000 | Creature entry of the NPC |
| `DungeonChallenge.AnnounceOnLogin` | 1 | Login message |
| `DungeonChallenge.DeathPenaltySeconds` | 15 | Time penalty per death (seconds) |
| `DungeonChallenge.KeystoneEnabled` | 1 | Keystone system on/off |
| `DungeonChallenge.KeystoneBuyCooldownMinutes` | 1440 | Purchase cooldown (minutes) |

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
- **Bolstering**: In `OnAllCreatureJustDied()` → +20% HP + extraDamageMultiplier *= 1.2 via DataMap
- **Bursting**: In `OnAllCreatureJustDied()` → `EnvironmentalDamage()` to all players
- **Sanguine**: In `OnAllCreatureJustDied()` → `ModifyHealth()` on nearby mobs

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
| `creature_template` (Entry 500000) | Challenge NPC |
| `item_template` (Entry 500001) | Keystone item |

### Characters DB

| Table | Purpose |
|-------|---------|
| `dungeon_challenge_leaderboard` | Leaderboard (top runs per dungeon/level) |
| `dungeon_challenge_history` | All runs of a player |
| `dungeon_challenge_best` | Aggregated best scores |
| `dungeon_challenge_snapshot` | Boss kill records (per boss, per participant) |

## IDs and Ranges

| Type | ID/Range | Usage |
|------|----------|-------|
| NPC Entry | 500000 | Dungeon Challenge NPC |
| Item Entry | 500001 | Dungeon Challenge Keystone |
| Gossip Actions | 1000-5999 | Menu navigation |
| Spell | 8599 | Enrage Visual (Raging affix) |
| Maps | 574-668 | WotLK 5-man dungeons |

## Code Conventions

- AzerothCore C++ standard (see share-public/CLAUDE.md)
- 4-space indentation, no tabs
- `UpperCamelCase` for classes and methods
- `_lowerCamelCase` for private members
- `UPPER_SNAKE_CASE` for enums/constants
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
