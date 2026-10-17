# mod-dungeon-challenge

A **Mythic+ inspired dungeon challenge system** for AzerothCore (WoW 3.3.5a WotLK).

## Features

- **Difficulty levels 1-100**: mob health and damage grow per level; per-dungeon boss and trash factors on top
  (`dungeon_challenge_scale`)
- **Timer per dungeon** with a death penalty (configurable)
- **Affixes as data** (`dungeon_challenge_affix`): mob affixes on a share of the mobs (one more every 10 levels),
  run affixes that act on the whole instance, and a **weekly rotation** of the run affixes (Lightning Sphere,
  Entangling Roots, Enrage)
- **Rewards**: gold and Paragon XP by formula, plus items per level band (`dungeon_challenge_reward`); doubled in time
- **Records**: leaderboard, history, bests, per-boss snapshots with the **fight time** of each boss
- **Runs survive a restart**: a run in progress is saved (`dungeon_challenge_active`) and comes back when a
  participant enters its instance; the downtime counts against the timer
- **Low-level dungeons lifted to level 80** (`dungeon_challenge_scale.uplift_level`), e.g. the Turtle dungeons
- **UI**: the Dungeon Challenge Stone opens an AIO window (dungeon/level choice, leaderboard, records), a run
  tracker, a mob counter HUD and a run-end summary; the NPC 500000 offers the same through gossip menus

## Supported Dungeons

| Dungeon | Timer |
|---------|-------|
| Utgarde Keep | 25 min |
| Utgarde Pinnacle | 28 min |
| The Nexus | 28 min |
| The Oculus | 35 min |
| Culling of Stratholme | 30 min |
| Halls of Stone | 28 min |
| Drak'Tharon Keep | 25 min |
| Azjol-Nerub | 20 min |
| Halls of Lightning | 28 min |
| Gundrak | 28 min |
| Violet Hold | 25 min |
| Ahn'kahet: The Old Kingdom | 30 min |
| The Forge of Souls | 22 min |
| Trial of the Champion | 25 min |
| Pit of Saron | 28 min |
| Halls of Reflection | 25 min |

## Installation

### 1. Add Module to AzerothCore

```bash
cd azerothcore-wotlk/modules/
git clone https://github.com/Shoro2/mod-dungeon-challenge.git
```

Or as symlink:
```bash
ln -s /path/to/mod-dungeon-challenge azerothcore-wotlk/modules/mod-dungeon-challenge
```

### 2. Recompile the Server

```bash
cd azerothcore-wotlk/build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/azeroth-server \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DSCRIPTS=static -DMODULES=static
make -j$(nproc)
make install
```

### 3. Create Database Tables

SQL files are automatically executed on first startup if `include.sh` is correctly configured.

**Manual setup:**

```bash
# World database
mysql -u root acore_world < modules/mod-dungeon-challenge/data/sql/db-world/00_dungeon_challenge_world.sql

# Characters database
mysql -u root acore_characters < modules/mod-dungeon-challenge/data/sql/db-characters/00_dungeon_challenge_characters.sql
```

### 4. Adjust Configuration

Copy the configuration file:
```bash
cp modules/mod-dungeon-challenge/conf/mod_dungeon_challenge.conf.dist etc/mod_dungeon_challenge.conf
```

Adjust the values in `mod_dungeon_challenge.conf` as needed (see Configuration below).

### 5. Start the Server

```bash
./worldserver
```

On startup you should see:
```
>> mod-dungeon-challenge: Configuration loaded (Enabled: Yes, MaxDifficulty: 20, AffixPct: 5%)
>> mod-dungeon-challenge: Loaded 16 default dungeons.
>> mod-dungeon-challenge: Loaded 10 affixes.
```

## In-Game Guide

### Spawn the NPC

The Challenge NPC has entry ID **500000**. Spawn it in a capital city:

```
.npc add 500000
```

### Starting a Challenge

1. **Form a group** (2-5 players)
2. **Talk to the NPC** → Main menu opens
3. **Select "Start Dungeon Challenge"**
4. **Choose a dungeon** from the list
5. **Choose a difficulty level** (1-20)
   - Green (1-5): Entry level
   - Yellow (6-10): Medium
   - Orange (11-15): Hard
   - Red (16-20): Extreme
6. **Confirm** → Group is teleported into the dungeon
7. **Use the Keystone** → 10-second countdown → Timer starts → Defeat all bosses before time runs out!

### During the Run

- **Timer**: Remaining time is periodically displayed in chat
- **Boss Kills**: Progress is shown after each boss kill
- **Deaths**: Each death adds a time penalty and is announced. A **wipe**
  (all participants dead at once; solo = any death) **ends the run**
  (configurable to "any death" via `DeathEndsRunMode`).
- **Affixes**: ~15% of mobs have special abilities (marked in chat)
- **Mob Counters**: A top-center HUD shows **"Mobs pulled"** (hostile mobs
  currently in combat near the party — respawn copies, Lil' Bro splits and
  boss adds included) and **"Total mob spawns left: y/x"** (alive / all
  regular mob spawns of the instance). Toggle with `/dc counters`, drag to
  reposition.
- **Cancel**: The run can be abandoned via the NPC or by the group leader

### When a Run Ends

When a run ends — by **dungeon clear** or by **death/wipe** — a **Run Summary**
panel opens on screen instead of teleporting the player out immediately:

- **Boss split times** for every boss defeated, plus the **total time** and
  **deaths**.
- The **difference to your personal best** and the **global best** for that
  dungeon and difficulty (green = faster, red = slower, "new best!" on a record).
- A countdown: after `SummarySeconds` (default **30s**) you are teleported to
  your **hearthstone/home** and the instance is reset for a fresh re-run.
- A **"Leave Now"** button to exit early.

A dead player is kept in place during the summary (no instant graveyard
release). If the client add-on is unavailable, the server still returns the
player home and resets after `FallbackSeconds` (default 40s).

### Leaderboard

Via the NPC:
- **"Leaderboard"** → Select dungeon → Top 10 runs
- **"My Best Runs"** → Personal best scores

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `DungeonChallenge.Enable` | 1 | Enable/disable the module |
| `DungeonChallenge.AffixPercentage` | 15 | % of mobs that get all mob affixes of the level |
| `DungeonChallenge.MaxDifficulty` | 100 | Highest selectable level |
| `DungeonChallenge.HealthMultiplierPerLevel` | 15 | HP bonus per level in % |
| `DungeonChallenge.DamageMultiplierPerLevel` | 8 | Damage bonus per level in % |
| `DungeonChallenge.TimerBaseMinutes` | 30 | Fallback timer if no dungeon-specific timer |
| `DungeonChallenge.LootBonusPerLevel` | 50000 | Gold per level in copper (50000 = 5 gold) |
| `DungeonChallenge.AnnounceOnLogin` | 1 | Welcome message on login |
| `DungeonChallenge.DeathPenaltySeconds` | 15 | Time penalty per death in seconds |
| `DungeonChallenge.GameObjectEntry` | 500002 | The Dungeon Challenge Stone |
| `DungeonChallenge.ParagonXPPerLevel` | 10 | Paragon XP per level (doubled in time) |
| `DungeonChallenge.SummarySeconds` | 30 | Run-summary auto-leave countdown in seconds |
| `DungeonChallenge.DeathEndsRunMode` | 0 | 0 = wipe ends run, 1 = any death ends run |
| `DungeonChallenge.FallbackSeconds` | 40 | Server safety teleport-home/reset (no add-on) |
| `DungeonChallenge.RunCredit` | 402451 | Kill credit per completed run (call board quests), 0 = off |
| `DungeonChallenge.WeeklyAffixCount` | 2 | Weekly affixes drawn per week from the rotating pool |
| `DungeonChallenge.RestoreWindowMinutes` | 120 | A saved run older than this is not restored (0 = no limit) |
| `DungeonChallenge.Uplift.DiminishingThreshold` | 10000 | Uplifted spell damage: extra above this grows slower |
| `DungeonChallenge.Uplift.DiminishingExponent` | 0.975 | ... as (extra - threshold) ^ exponent |

## Tuning tables (world DB)

| Table | What it tunes |
|-------|---------------|
| `dungeon_challenge_affix` | each affix: on/off, minimum level, mob or run affix, weekly rotation, two numbers (`value1`/`value2`, see the seed SQL) |
| `dungeon_challenge_scale` | per dungeon: trash and boss health/damage factors, and the uplift level for low-level dungeons |
| `dungeon_challenge_reward` | items per level band for a completed run (count doubled in time) |
| `dungeon_challenge_boss_entry` | creatures that count as a boss below rank 3 |

`.dchallenge reload` reloads them; `.dchallenge affixes [level]` shows this week's rotation.
GM tools: `.dchallenge status` (the run and the selected creature's scaling), `.dchallenge scale` (live factors),
`.dchallenge week` (pin a rotation week), `.dchallenge force` (fire a run affix now), `.dchallenge start`,
`.dchallenge forget` (drop the run from memory as a restart does).

## Difficulty Scaling

Health x (1 + `HealthMultiplierPerLevel`% x level) and damage x (1 + `DamageMultiplierPerLevel`% x level), times
the dungeon's boss or trash factor. Mob heals between scaled mobs scale with the health factor.

## Affixes

| Id | Affix | Kind | From level | Effect |
|----|-------|------|-----------|--------|
| 1 | Call for Help | mob | 10 | Calls allies within 30 yd on pull |
| 2 | Speedy | mob | 20 | +100% movement, +10% attack speed |
| 3 | Big Boy | mob | 30 | +50% health, bigger |
| 4 | Immolation Aura | mob | 40 | Fire damage (level x 80) to players within 8 yd |
| 5 | CC Immunity | mob | 50 | Immune to crowd control |
| 6 | Heavy Hits | mob | 60 | +33% damage |
| 7 | Lil' Bro | mob | 70 | Splits 1 -> 2 -> 4 on death |
| 8 | Damage Reduce | mob | 80 | Allies within 30 yd take 25% less damage |
| 9 | Bigger Boy | mob | 90 | +50% health, bigger, +10% damage |
| 10 | Hell Touched | mob | 100 | +666 damage per hit, stat debuff |
| 11 | Lightning Sphere | run, weekly | 10 | Spheres appear and shock every player within 100 yd until killed |
| 12 | Entangling Roots | run, weekly | 10 | A nearby enemy now and then roots a player |
| 13 | Enrage | run, weekly | 10 | Enemies in combat now and then enrage |

## Customizing Dungeon Timers

Timers can be adjusted in the `dungeon_challenge_dungeons` table in the world DB:

```sql
UPDATE `dungeon_challenge_dungeons` SET `timer_minutes` = 35 WHERE `map_id` = 574;
```

## Adding Custom Dungeons

```sql
INSERT INTO `dungeon_challenge_dungeons`
(`map_id`, `name`, `entrance_x`, `entrance_y`, `entrance_z`, `entrance_o`, `timer_minutes`, `boss_count`)
VALUES (999, 'My Custom Dungeon', 100.0, 200.0, 50.0, 0.0, 30, 4);
```

## Troubleshooting

### Module Not Loading
- Check if `modules/mod-dungeon-challenge/` exists
- Check the build output for compile errors
- Check the worldserver logs for `mod-dungeon-challenge` entries

### NPC Shows No Menu
- Check if `creature_template` entry 500000 exists: `.lookup creature 500000`
- Check if ScriptName is correct: `npc_dungeon_challenge`

### Teleport Not Working
- Check the entrance coordinates in `dungeon_challenge_dungeons`
- Coordinates with `0,0,0` must be manually added
- Tip: Go to the dungeon entrance and use `.gps` for the coordinates

## License

This module is licensed under GNU GPL v2, compatible with AzerothCore.
