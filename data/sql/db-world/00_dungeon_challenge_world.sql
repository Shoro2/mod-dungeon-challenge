-- ============================================================================
-- mod-dungeon-challenge: World Database Setup
-- ============================================================================

-- ============================================================================
-- Dungeon Challenge NPC (Entry: 500000)
-- ============================================================================

SET @NPC_ENTRY := 500000;

DELETE FROM `creature_template` WHERE `entry` = @NPC_ENTRY;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `unit_flags`, `type`, `type_flags`, `ScriptName`) VALUES
(@NPC_ENTRY, 'Dungeon Challenge', 'Challenge Master', 'Interact', 0, 80, 80, 35, 1, 1, 2, 7, 0, 'npc_dungeon_challenge');

-- Model: Use a fitting humanoid model (Khadgar-style)
DELETE FROM `creature_template_model` WHERE `CreatureID` = @NPC_ENTRY;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(@NPC_ENTRY, 0, 20925, 1.0, 1.0);

-- ============================================================================
-- Dungeon Challenge Stone (GameObject Entry: 500002)
-- Clickable object that opens the Lua gossip UI for dungeon selection.
-- displayId can be changed to any suitable model (6784 = portal stone).
-- ============================================================================

SET @GO_ENTRY := 500002;

DELETE FROM `gameobject_template` WHERE `entry` = @GO_ENTRY;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `size`)
VALUES (@GO_ENTRY, 2, 6784, 'Dungeon Challenge Stone', 'Interact', '', 1.5);

-- ============================================================================
-- Dungeon Challenge Dungeons Table
-- ============================================================================
-- No DROP TABLE: the DB updater re-applies this file whenever its hash
-- changes, and a DROP would wipe rows added later (e.g. FL custom dungeons).

CREATE TABLE IF NOT EXISTS `dungeon_challenge_dungeons` (
    `map_id` INT UNSIGNED NOT NULL,
    `name` VARCHAR(100) NOT NULL,
    `entrance_x` FLOAT NOT NULL DEFAULT 0,
    `entrance_y` FLOAT NOT NULL DEFAULT 0,
    `entrance_z` FLOAT NOT NULL DEFAULT 0,
    `entrance_o` FLOAT NOT NULL DEFAULT 0,
    `timer_minutes` INT UNSIGNED NOT NULL DEFAULT 30,
    `boss_count` INT UNSIGNED NOT NULL DEFAULT 3,
    `raid_difficulty` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'raid-type maps only: forced raid size (0 = 10n, 1 = 25n)',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Idempotent column add for tables created before `raid_difficulty` existed
-- (MySQL 8 has no ADD COLUMN IF NOT EXISTS; the updater re-applies this file
-- on every hash change).
SET @col := (SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
    WHERE `TABLE_SCHEMA` = DATABASE()
      AND `TABLE_NAME` = 'dungeon_challenge_dungeons'
      AND `COLUMN_NAME` = 'raid_difficulty');
SET @sql := IF(@col = 0,
    'ALTER TABLE `dungeon_challenge_dungeons` ADD COLUMN `raid_difficulty` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''raid-type maps only: forced raid size (0 = 10n, 1 = 25n)'' AFTER `boss_count`',
    'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Default WotLK dungeons. Entrance coordinates mirror the `areatrigger_teleport`
-- entrance rows (AT id in the trailing comment) — the same source the RDF
-- teleport uses via LFGMgr::LoadLFGDungeons -> GetMapEntranceTrigger.
DELETE FROM `dungeon_challenge_dungeons` WHERE `map_id` IN (574, 575, 576, 578, 595, 599, 600, 601, 602, 604, 608, 619, 632, 650, 658, 668);
INSERT INTO `dungeon_challenge_dungeons` (`map_id`, `name`, `entrance_x`, `entrance_y`, `entrance_z`, `entrance_o`, `timer_minutes`, `boss_count`) VALUES
(574, 'Utgarde Keep',                 153.789,  -86.548,   12.551,  0.304,    25, 3),  -- AT 4745
(575, 'Utgarde Pinnacle',             584.117,  -327.974, 110.138,  3.122,    28, 4),  -- AT 4747
(576, 'The Nexus',                    145.87,   -10.554,  -16.636,  1.528,    28, 4),  -- AT 4983
(578, 'The Oculus',                   1055.93,  986.85,   361.07,   5.745,    35, 4),  -- AT 5246
(595, 'Culling of Stratholme',        1431.1,   556.92,    36.69,   5.16,     30, 5),  -- AT 5150
(599, 'Halls of Stone',               1153.24,  806.164,  195.937,  4.715,    28, 3),  -- AT 5010
(600, 'Drak''Tharon Keep',            -517.343, -487.976,  11.01,   4.831,    25, 4),  -- AT 4998
(601, 'Azjol-Nerub',                  413.314,  795.968,  831.351,  5.5,      20, 3),  -- AT 5117
(602, 'Halls of Lightning',           1331.47,  259.619,   53.398,  4.772,    28, 4),  -- AT 5093
(604, 'Gundrak',                      1891.84,  832.169,  176.669,  2.109,    28, 4),  -- AT 5205 (south)
(608, 'Violet Hold',                  1808.82,  803.93,    44.364,  6.282,    25, 3),  -- AT 5209
(619, 'Ahn''kahet: The Old Kingdom',  333.351,  -1109.94,  69.772,  0.553,    30, 5),  -- AT 5215
(632, 'The Forge of Souls',           4922.86,  2175.63,  638.734,  2.00355,  22, 2),  -- AT 5635
(650, 'Trial of the Champion',        805.227,  618.038,  412.393,  3.1456,   25, 3),  -- AT 5505
(658, 'Pit of Saron',                 435.743,  212.413,  528.709,  6.25646,  28, 3),  -- AT 5637
(668, 'Halls of Reflection',          5239.01,  1932.64,  707.695,  0.800565, 25, 2);  -- AT 5636

-- ============================================================================
-- Forgotten Land custom dungeons (standalone maps 738-748)
-- ============================================================================
-- Entrances mirror the FL entrance areatriggers 8002-8010 (738 Nak'talim has
-- no entrance trigger — game_tele `flnaktalim`). Boss counts = curated rank-3
-- bosses visible per difficulty (see 31_fl_dungeon_challenge_bossranks.sql in
-- ForgottenLand2.0/output/sql). Heroic-capable dungeon maps (739/740/744/745/
-- 746/748) enter heroic via the auto-heroic hook — on 744/748 the bosses ONLY
-- exist in the heroic spawn set. 747 Akleia is single-difficulty. Raid-type
-- maps run at the forced `raid_difficulty`: 742 Conclave 10-player (bosses on
-- mask 1), 743 Hoto 25-player (its 10-player set is a broken low-level stub —
-- the real lvl-83 content is mask-2-only; its "Big shark" trash is demoted to
-- rank 1 via 32_fl_hoto_challenge_prep.sql so it stops feeding the boss
-- counter). Requires Instance.IgnoreRaid = 1 for partyless/solo raid entry.
-- Disabled: 738 Nak'talim (phase 3 — planned quest-chain unlock).
DELETE FROM `dungeon_challenge_dungeons` WHERE `map_id` IN (738, 739, 740, 742, 743, 744, 745, 746, 747, 748);
INSERT INTO `dungeon_challenge_dungeons` (`map_id`, `name`, `entrance_x`, `entrance_y`, `entrance_z`, `entrance_o`, `timer_minutes`, `boss_count`, `raid_difficulty`, `enabled`) VALUES
(738, 'FL: Nak''talim (Raid)',        16336,    15478,    295,      0,        60, 5, 0, 0),  -- game_tele flnaktalim
(739, 'FL: Xala',                     -351.96,  -799.3,   0.65,     0.007858, 45, 2, 0, 1),  -- AT 8004
(740, 'FL: Ak''Tazia',                -1639.62, 6738.93,  114.22,   0.91,     30, 2, 0, 1),  -- AT 8003 (Malachar trio = 1 grouped encounter + Ak'Tazia)
(742, 'FL: Conclave',                 -79.53,   -964.61,  41.14,    1.85,     20, 2, 0, 1),  -- AT 8008 (raid map, forced 10-player)
(743, 'FL: Hoto (Raid)',              1954.41,  1589.72,  80.83,    1.16,     30, 3, 1, 1),  -- AT 8010 (raid map, forced 25-player = the real content)
(744, 'FL: Genetic',                  2404.58,  767.17,   0,        4.74,     30, 4, 0, 1),  -- AT 8005
(745, 'FL: Murloc City',              1331.53,  849.33,   41.41,    6.2,      35, 3, 0, 1),  -- AT 8007
(746, 'FL: Trondam',                  178.77,   77.64,    143.7,    3.72,     35, 2, 0, 1),  -- AT 8006
(747, 'FL: Akleia',                   3446.02,  -3037.95, 175.23,   0.104,    20, 1, 0, 1),  -- AT 8002
(748, 'FL: Yelma',                    1565,     586.67,   98.22,    1.185,    25, 2, 0, 1);  -- AT 8009

-- ============================================================================
-- FL boss rank curation (canonical here; originally applied live via
-- ForgottenLand2.0/output/sql/31_fl_dungeon_challenge_bossranks.sql + 32)
-- ============================================================================
-- The challenge counts `rank` >= 3 kills as bosses. Promote the curated FL
-- site bosses (both normal/heroic twin entries, matching the FL author's own
-- pattern — Razen/Xameth/Azakian were already rank 3); demote the 30 "Big
-- shark" (9000000) spawns on Hoto to elite trash so they stop feeding the
-- boss counter. Idempotent; on installs without FL content these match 0 rows.
UPDATE `creature_template` SET `rank` = 3 WHERE `entry` IN
(81158, 9000006,
 84223, 80223, 80260, 80261, 80262, 84260, 84261, 84262,
 200163, 80183,
 80132, 80131, 80129,
 80210, 80209, 200183, 80205,
 81112, 80112, 81113, 80113, 81114, 80114,
 81202, 80060,
 81332,
 80139, 80136)
AND `rank` <> 3;

UPDATE `creature_template` SET `rank` = 1 WHERE `entry` = 9000000 AND `rank` <> 1;

-- ============================================================================
-- Boss Group Table (multi-mob encounters counted as ONE boss)
-- ============================================================================
-- All members of a `group_id` form one encounter: the boss counter increments
-- only when the LAST living member dies (C++ OnPlayerCreatureKill; the Lua
-- tracker ticks the group on its first member kill). Seeded with the Malachar
-- trio in FL Ak'Tazia (normal twins 80260-80262 / heroic twins 84260-84262 —
-- per difficulty only one set spawns).
CREATE TABLE IF NOT EXISTS `dungeon_challenge_boss_group` (
    `creature_entry` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `group_id` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELETE FROM `dungeon_challenge_boss_group` WHERE `creature_entry` IN (80260, 80261, 80262, 84260, 84261, 84262);
INSERT INTO `dungeon_challenge_boss_group` (`creature_entry`, `map_id`, `group_id`) VALUES
(80260, 740, 1),
(80261, 740, 1),
(80262, 740, 1),
(84260, 740, 1),
(84261, 740, 1),
(84262, 740, 1);

-- ============================================================================
-- Spell Override Table (per-spell damage tuning)
-- ============================================================================

-- No DROP TABLE: a re-apply of this file must not wipe live spell overrides.
CREATE TABLE IF NOT EXISTS `dungeon_challenge_spell_override` (
    `spell_id` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = applies to all maps',
    `mod_pct` FLOAT NOT NULL DEFAULT -1 COMMENT 'Direct damage modifier (-1 = no override)',
    `dot_mod_pct` FLOAT NOT NULL DEFAULT -1 COMMENT 'DoT damage modifier (-1 = no override)',
    PRIMARY KEY (`spell_id`, `map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Example spell overrides (uncomment and adjust as needed):
-- Some abilities may deal too much damage when scaled, so reduce them
-- INSERT INTO `dungeon_challenge_spell_override` (`spell_id`, `map_id`, `mod_pct`, `dot_mod_pct`) VALUES
-- (59038, 0, 0.6, -1),    -- Blizzard damage reduced globally
-- (52237, 574, 0.8, -1),   -- Specific spell in Utgarde Keep
-- (59842, 632, -1, 0.5);   -- DoT in Forge of Souls reduced
