# mod-dungeon-challenge

A **Mythic+ inspired dungeon challenge system** for AzerothCore (WoW 3.3.5a WotLK).

## Features

- **Difficulty Levels 1-20**: Progressively scales mob HP and damage
- **Timer System**: Each dungeon has an individual timer with time pressure
- **Random Affixes**: ~5% of all mobs in the dungeon receive special abilities
- **10 Different Affixes**: Bolstering, Raging, Sanguine, Necrotic, Bursting, Explosive, Fortified, Volcanic, Storming, Inspiring
- **Leaderboard**: Global ranking per dungeon and difficulty level
- **Gold Rewards**: Scales with difficulty, bonus for in-time completions
- **Death Penalty**: Each death costs time (configurable, default 15 seconds)
- **Keystone System**: Requires keystone item activation inside the dungeon
- **NPC-Based UI**: No addon required — everything via gossip menus

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
- **Deaths**: Each death adds a time penalty and is announced
- **Affixes**: ~5% of mobs have special abilities (marked in chat)
- **Cancel**: The run can be abandoned via the NPC or by the group leader

### Leaderboard

Via the NPC:
- **"Leaderboard"** → Select dungeon → Top 10 runs
- **"My Best Runs"** → Personal best scores

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `DungeonChallenge.Enable` | 1 | Enable/disable the module |
| `DungeonChallenge.AffixPercentage` | 5 | % of mobs with random affixes |
| `DungeonChallenge.MaxDifficulty` | 20 | Highest selectable level |
| `DungeonChallenge.HealthMultiplierPerLevel` | 15 | HP bonus per level in % |
| `DungeonChallenge.DamageMultiplierPerLevel` | 8 | Damage bonus per level in % |
| `DungeonChallenge.TimerBaseMinutes` | 30 | Fallback timer if no dungeon-specific timer |
| `DungeonChallenge.LootBonusPerLevel` | 50000 | Gold per level in copper (50000 = 5 gold) |
| `DungeonChallenge.NpcEntry` | 500000 | Creature entry ID of the NPC |
| `DungeonChallenge.AnnounceOnLogin` | 1 | Welcome message on login |
| `DungeonChallenge.DeathPenaltySeconds` | 15 | Time penalty per death in seconds |
| `DungeonChallenge.KeystoneEnabled` | 1 | Enable keystone item system |
| `DungeonChallenge.KeystoneBuyCooldownMinutes` | 1440 | Keystone purchase cooldown (minutes) |

## Difficulty Scaling

| Level | HP Multiplier | DMG Multiplier | Active Affixes |
|-------|--------------|----------------|----------------|
| 1 | 1.15x | 1.08x | 0 |
| 2 | 1.30x | 1.16x | 3 (Bolstering, Raging, Fortified) |
| 5 | 1.75x | 1.40x | 3 |
| 7 | 2.05x | 1.56x | 5 (+Bursting, Explosive) |
| 10 | 2.50x | 1.80x | 7 (+Sanguine, Necrotic) |
| 14 | 3.10x | 2.12x | 9 (+Volcanic, Storming) |
| 15 | 3.25x | 2.20x | 10 (+Inspiring) |
| 20 | 4.00x | 2.60x | 10 |

## Affix Descriptions

| Affix | Effect | Tip |
|-------|--------|-----|
| **Bolstering** | Mob death buffs nearby allies (+20% HP/DMG) | Kill mobs simultaneously |
| **Raging** | Enrage below 30% HP (+50% DMG) | Focus quickly |
| **Sanguine** | Leaves healing zone for other mobs | Pull mobs out of the zone |
| **Necrotic** | Stacking healing reduction | Tank must kite |
| **Bursting** | AoE damage on mob death | Don't kill too many at once |
| **Explosive** | Spawns explosive orbs | Destroy orbs immediately |
| **Fortified** | +40% HP, +20% DMG | Plan more time for trash |
| **Volcanic** | Fire zones under ranged players | Keep moving |
| **Storming** | Moving tornadoes | Dodge them |
| **Inspiring** | Allies immune to CC | Kill the inspiring mob first |

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
