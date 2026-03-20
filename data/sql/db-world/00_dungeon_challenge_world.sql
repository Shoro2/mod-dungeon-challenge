-- ============================================================================
-- mod-dungeon-challenge: World Database Setup
-- ============================================================================

-- ============================================================================
-- Dungeon Challenge NPC (Entry: 500000)
-- ============================================================================

SET @NPC_ENTRY := 500000;

DELETE FROM `creature_template` WHERE `entry` = @NPC_ENTRY;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_class`, `unit_flags`, `type`, `type_flags`, `ScriptName`) VALUES
(@NPC_ENTRY, 'Dungeon Challenge', 'Herausforderungsmeister', 'Interact', 0, 80, 80, 35, 1, 1, 2, 7, 0, 'npc_dungeon_challenge');

-- Model: Use a fitting humanoid model (Khadgar-style)
DELETE FROM `creature_template_model` WHERE `CreatureID` = @NPC_ENTRY;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(@NPC_ENTRY, 0, 20925, 1.0, 1.0);

-- ============================================================================
-- Dungeon Challenge Dungeons Table
-- ============================================================================

DROP TABLE IF EXISTS `dungeon_challenge_dungeons`;
CREATE TABLE `dungeon_challenge_dungeons` (
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

-- Default WotLK Dungeons with entrance coordinates
DELETE FROM `dungeon_challenge_dungeons`;
INSERT INTO `dungeon_challenge_dungeons` (`map_id`, `name`, `entrance_x`, `entrance_y`, `entrance_z`, `entrance_o`, `timer_minutes`, `boss_count`) VALUES
(574, 'Utgarde Keep',                 1.31,   -16.57,  15.21,  0.0,  25, 3),
(575, 'Utgarde Pinnacle',             503.63, -2.12,   242.11, 0.0,  28, 4),
(576, 'The Nexus',                    155.79, 11.18,   -19.07, 0.0,  28, 4),
(578, 'The Oculus',                   1046.08, 983.11, 360.0,  0.0,  35, 4),
(595, 'Culling of Stratholme',        1810.12, 1279.21, 141.71, 0.0, 30, 5),
(599, 'Halls of Stone',               1152.63, 809.53, 195.45, 0.0,  28, 3),
(600, 'Drak\'Tharon Keep',            -514.22, -696.03, 28.58, 0.0,  25, 4),
(601, 'Azjol-Nerub',                  553.34, 295.85,  224.28, 0.0,  20, 3),
(602, 'Halls of Lightning',           1331.06, 192.07, 52.47,  0.0,  28, 4),
(604, 'Gundrak',                      1891.92, 652.87, 107.17, 0.0,  28, 4),
(608, 'Violet Hold',                  1830.32, 803.93, 44.36,  0.0,  25, 3),
(619, 'Ahn''kahet: The Old Kingdom',  399.36, -172.39, -75.5,  0.0,  30, 5),
(632, 'The Forge of Souls',           5594.88, 2211.32, 533.17, 0.0, 22, 2),
(650, 'Trial of the Champion',        746.42, 661.2,   411.68, 0.0,  25, 3),
(658, 'Pit of Saron',                 442.25, 212.55,  528.71, 0.0,  28, 3),
(668, 'Halls of Reflection',          5239.01, 1929.89, 707.69, 0.0, 25, 2);
