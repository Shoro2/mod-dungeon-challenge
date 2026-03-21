# CLAUDE.md — mod-dungeon-challenge

Technical documentation for AI assistants working on this module.

## Module Overview

**mod-dungeon-challenge** is an AzerothCore 3.3.5a module that implements a Mythic+-style dungeon challenge system. Players click a **Dungeon Challenge Stone** (GameObject) to select a dungeon and difficulty level (1-100) via a **Lua gossip UI**, are teleported (solo or group), and must defeat all bosses within a timer. ~15% of dungeon mobs receive ALL available affixes for the current difficulty level (every 10 levels adds +1 affix to the pool).

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
│   ├── dungeon_challenge_server.lua            # AIO server: data loading, handlers, GO gossip
│   ├── dungeon_challenge_ui.lua                # AIO client: WoW UI frames (sent to client via AIO)
│   └── dungeon_challenge_gameobject.lua        # [DEPRECATED] Old gossip-based UI
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
| `DungeonChallengeCreatureScript` | DungeonChallengeScripts.cpp | Creature update (Call for Help, Immolation, DataMap processing) |
| `DungeonChallengeCreatureDeathScript` | DungeonChallengeScripts.cpp | Creature death (boss kill, affixes, non-mythic lock, snapshots) |
| `DungeonChallengeUnitScript` | DungeonChallengeScripts.cpp | Damage modification via UnitScript hooks |
| `DungeonChallengeTimerScript` | DungeonChallengeScripts.cpp | Timer display (AllMapScript) |

### Lua Scripts (AIO-based UI)

| File | Side | Purpose |
|------|------|---------|
| `lua_scripts/dungeon_challenge_server.lua` | Server | AIO server script: loads dungeon data from DB, registers AIO handlers for client requests (leaderboard, my runs, snapshots, start challenge), sends initial data on login via `AIO.AddOnInit`, handles GameObject gossip → opens client UI, **tracks active runs** and sends real-time updates (RunStart, BossKilled, DeathUpdate, RunCompleted, RunEnd) to client tracker via Eluna hooks |
| `lua_scripts/dungeon_challenge_ui.lua` | Client | AIO client addon (sent to WoW client): creates a full UI frame with tabs (Dungeons, Leaderboard, My Runs, Records), dungeon/difficulty selection, confirm panel, **active run tracker** (timer, bosses, deaths, affixes, +2/+3 thresholds), slash commands `/dc` and `/dc tracker`. All UI is rendered as native WoW frames. |
| `lua_scripts/dungeon_challenge_gameobject.lua` | Server | **[DEPRECATED]** Old gossip-menu-based UI. Replaced by the AIO scripts above. |

**AIO Architecture**: The server script registers the client file via `AIO.AddAddon()`. On login, dungeon data, affix data, and config are sent to the client via `AIO.AddOnInit()`. The client creates WoW frames for the UI. Server-client communication uses `AIO.Msg():Add():Send()` pattern. The `dungeon_challenge_pending` DB table is still used for Lua→C++ challenge handoff.

### Design Patterns

1. **DataMap Pattern**: Custom data is attached to `Map` and `Creature` objects via `DataMap::Base`. `MapChallengeData` stores run reference and lock status. `CreatureChallengeData` stores processing status, original HP, damage multiplier, and affix.

2. **UnitScript Damage Hooks**: Damage scaling via `ModifyMeleeDamage()`, `ModifySpellDamageTaken()`, and `ModifyPeriodicDamageAurasTick()` hooks. The `extraDamageMultiplier` is stored in `CreatureChallengeData`.

3. **Spell Override System**: Per-spell damage tuning via DB table `dungeon_challenge_spell_override`. Map-specific or global overrides (modPct for direct, dotModPct for DoTs).

4. **Non-Mythic Lock**: If a creature dies before a challenge is active, the instance is locked as "non-challenge". Prevents exploits with partially cleared dungeons.

5. **Lua ↔ C++ Communication**: The Lua GameObject UI writes pending challenge data (player_guid, map_id, difficulty) to the `dungeon_challenge_pending` characters DB table. The C++ `OnPlayerMapChanged` hook reads and deletes this entry when the player enters the dungeon. Stale entries are cleaned up on server startup.

6. **Snapshot-Based Records**: Each boss kill generates a detailed snapshot record per participant in the `dungeon_challenge_snapshot` table.

### Data Flow

```
Server startup:
    AIO.AddAddon("dungeon_challenge_ui.lua") → client receives UI code
    AIO.AddOnInit() → on login, sends dungeon/affix/config data to client
    ↓
Player clicks Dungeon Challenge Stone (GameObject)
    ↓
Lua Server: OnGossipHello → GossipComplete → AIO.Handle(player, "ShowUI")
    ↓
Lua Client: ShowUI handler → MainFrame:Show() → ShowDungeonPanel()
    ↓
Client: Player selects dungeon + difficulty (all client-side, no server round-trips)
    ↓
Client: Clicks "START CHALLENGE" → AIO.Msg():Add("StartChallenge", mapId, diff):Send()
    ↓
Lua Server: StartChallenge handler
    ├─ Validation (group size ≤ 5, difficulty range)
    ├─ INSERT INTO dungeon_challenge_pending (player_guid, map_id, difficulty)
    ├─ AIO.Handle(member, "ChallengeStarted", ...) for each group member
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
    ├─ OnAllCreatureUpdate() → ProcessCreature() via DataMap + Call for Help + Immolation tick
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
| `DungeonChallenge.AffixPercentage` | 15 | % of mobs with ALL available affixes |
| `DungeonChallenge.MaxDifficulty` | 100 | Maximum difficulty level (every 10 levels +1 affix) |
| `DungeonChallenge.HealthMultiplierPerLevel` | 15 | +HP% per level |
| `DungeonChallenge.DamageMultiplierPerLevel` | 8 | +DMG% per level (via UnitScript hooks) |
| `DungeonChallenge.TimerBaseMinutes` | 30 | Base timer (fallback) |
| `DungeonChallenge.LootBonusPerLevel` | 50000 | Gold bonus per level (copper) |
| `DungeonChallenge.NpcEntry` | 500000 | Creature entry of the NPC (fallback) |
| `DungeonChallenge.AnnounceOnLogin` | 1 | Login message |
| `DungeonChallenge.DeathPenaltySeconds` | 15 | Time penalty per death (seconds) |
| `DungeonChallenge.GameObjectEntry` | 500002 | GameObject entry for the Dungeon Challenge Stone |
| `DungeonChallenge.ParagonXPPerLevel` | 10 | Paragon XP per difficulty level (doubled if in time) |

## Scaling Formulas

```
HP Multiplier = 1.0 + (HealthMultiplierPerLevel/100 × Level)
  Level 1:  1.15x  |  Level 10: 2.50x  |  Level 50: 8.50x  |  Level 100: 16.00x

DMG Multiplier = 1.0 + (DamageMultiplierPerLevel/100 × Level)
  Via UnitScript hooks (ModifyMeleeDamage, ModifySpellDamageTaken, ModifyPeriodicDamageAurasTick)
  Stored in CreatureChallengeData::extraDamageMultiplier
  Level 1:  1.08x  |  Level 10: 1.80x  |  Level 50: 5.00x  |  Level 100: 9.00x

Spell Override: dungeon_challenge_spell_override table
  modPct = Direct Damage Modifier (-1 = no override)
  dotModPct = DoT Damage Modifier (-1 = no override)
  Map-specific or global (map_id = 0)

Gold Reward = LootBonusPerLevel × Level × (inTime ? 2 : 1)
  Level 10 in time: 100g  |  Level 10 over time: 50g

Paragon XP = ParagonXPPerLevel × Level × (inTime ? 2 : 1)
  Level 50 in time: 10 × 50 × 2 = 1000 XP
  Level 100 in time: 10 × 100 × 2 = 2000 XP

Timer penalty per death: +DeathPenaltySeconds (Default: 15s)
  Effective time = elapsedTime + penaltyTime
```

## Affix System

### Available Affixes

Every 10 levels adds +1 affix to the pool. Selected mobs receive ALL available affixes.

| ID | Name | Effect | Min Level | DBC Spell |
|----|------|--------|-----------|-----------|
| 1 | Call for Help | Calls allies within 30y when entering combat | 10 | 900060 (visual) |
| 2 | Speedy | +100% move speed, +10% attack speed | 20 | 900050 (full DBC) |
| 3 | Big Boy | +50% HP, increased size | 30 | 900056 (size DBC, HP C++) |
| 4 | Immolation Aura | Periodic fire damage = Level × 80 to players within 8y | 40 | 900051 (visual, damage C++) |
| 5 | CC Immunity | Immune to all crowd control | 50 | 900052 (full DBC) |
| 6 | Heavy Hits | +33% damage | 60 | 900058 (full DBC) |
| 7 | Lil' Bro | Splits 1→2→4 on death, -90% HP each tier | 70 | 900059 (visual, logic C++) |
| 8 | Damage Reduce | Allies within 30y take -25% damage | 80 | 900055 (visual, logic C++) |
| 9 | Bigger Boy | +50% HP, increased size, +10% damage | 90 | 900057 (size+dmg DBC, HP C++) |
| 10 | Hell Touched | +666 fire+shadow dmg on hit, -10% stats debuff (10s, 10 stacks) | 100 | 900054 (visual) + 900053 (debuff) |

### DBC Spell IDs (created manually in Stoneharry spell editor)

| Spell ID | Name | Purpose |
|----------|------|---------|
| 900050 | Speedy | Aura on NPC: +100% move speed, +10% melee haste |
| 900051 | Immolation Aura | Aura on NPC: fire visual (damage in C++) |
| 900052 | CC Immunity | Aura on NPC: mechanic immunity (all CC) |
| 900053 | Hell Touched Aura | Debuff on player: -10% all stats, 10s, stackable to 10 |
| 900054 | Hell Touched | Aura on NPC: Hell Touched visual |
| 900055 | Damage Reduction Aura | Aura on NPC: Damage Reduction visual |
| 900056 | Big Boy | Aura on NPC: size increase |
| 900057 | Bigger Boy | Aura on NPC: size + damage increase |
| 900058 | Heavy Hits | Aura on NPC: +33% damage |
| 900059 | Lil Bro | Aura on NPC: Lil Bro visual |
| 900060 | Call for Help | Aura on NPC: Call for Help visual |

### Affix Assignment

1. Collect all living, non-boss, non-friendly creatures in the dungeon
2. Calculate count: `max(1, totalCreatures × affixPercentage / 100)` (default 10%)
3. Shuffle-and-pick: Random selection of which mobs get affixed
4. ALL affixes whose `minDifficulty ≤ current level` are assigned to each selected mob
5. Affixes are stored in `CreatureChallengeData::affixes` vector (DataMap)

### Affix Implementation Details

- **Call for Help**: C++ `OnAllCreatureUpdate()` → on enter combat, pull allies within 30y. DBC aura 900060 = visual only.
- **Speedy**: DBC aura 900050 handles everything (+100% speed, +10% haste). No C++.
- **Big Boy**: C++ `SetMaxHealth * 1.5`. DBC aura 900056 handles size increase.
- **Immolation**: C++ `OnAllCreatureUpdate()` → 2s tick, `EnvironmentalDamage(DAMAGE_FIRE, difficulty * 80)` within 8y. DBC aura 900051 = visual.
- **CC Immunity**: DBC aura 900052 handles all mechanic immunities. No C++.
- **Heavy Hits**: DBC aura 900058 handles +33% damage. No C++.
- **Lil' Bro**: C++ `OnPlayerCreatureKill()` → generation-based split (gen 0→2 copies gen 1, gen 1→2 copies gen 2, gen 2→stop). Total: 1+2+4=7 mobs. Only gen 0 drops loot. DBC aura 900059 = visual.
- **Damage Reduce**: C++ `ApplyAffixToCreature()` → sets incomingDamageReduction = 0.25 on allies within 30y; UnitScript reduces player→creature damage. DBC aura 900055 = visual.
- **Bigger Boy**: C++ `SetMaxHealth * 1.5`. DBC aura 900057 handles size + damage increase.
- **Hell Touched**: C++ UnitScript `ModifyMeleeDamage()`/`ModifySpellDamageTaken()` → EnvironmentalDamage(666) + CastSpell(900053 debuff). DBC aura 900054 = visual on NPC.

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
| Spells | 900050-900060 | Affix DBC spells (all affixes have a DBC aura) |
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

1. **DBC Spells**: Affix spells 900050-900054 must be created manually in Stoneharry spell editor
2. **Boss Detection**: Currently via `creature_template.rank >= 3` → some bosses have rank < 3
3. **Timer UI**: Active run tracker frame implemented via AIO (TrackerFrame). Shows timer, boss progress, deaths, affixes, +2/+3 thresholds. Toggle with `/dc tracker`
4. **Prepared Statements**: Currently format-string-based queries → should use prepared statements
5. **Creature Scaling**: `GetCreatureBySpawnIdStore()` must be verified against correct API
6. **Instance Reset**: No automatic instance reset after run completion
7. **Snapshot Reload**: Snapshots are loaded at startup but not periodically refreshed
8. **Lua CONFIG sync**: Config values are sent to client via AIO.AddOnInit on login (server-side config is authoritative)
9. **AIO Dependency**: Requires AIO framework (AIO.lua + dependencies) in server `lua_scripts/` folder
