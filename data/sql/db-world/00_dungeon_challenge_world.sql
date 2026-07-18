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
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

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
