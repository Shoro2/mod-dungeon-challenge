-- ============================================================================
-- mod-dungeon-challenge: Mythic+ ideas 4-9 (survey 2026-10-10, operator 2026-10-16:
-- "ja die hören sich alle gut an"). Idempotent: the updater re-applies this file
-- whenever its bytes change.
-- ============================================================================

-- ----------------------------------------------------------------------------
-- (4) Per-dungeon boss and trash factors, on top of the level multipliers.
--     (9) uplift_level lifts a low-level dungeon: trash to this level, elites
--     +1, bosses +2, with WotLK base stats; 0 = no uplift. A missing row means
--     all factors 1 and no uplift.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `dungeon_challenge_scale` (
    `map_id` INT UNSIGNED NOT NULL,
    `trash_health` FLOAT NOT NULL DEFAULT 1,
    `trash_damage` FLOAT NOT NULL DEFAULT 1,
    `boss_health` FLOAT NOT NULL DEFAULT 1,
    `boss_damage` FLOAT NOT NULL DEFAULT 1,
    `uplift_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `comment` VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='mod-dungeon-challenge: per-dungeon health/damage factors and level uplift';

-- ----------------------------------------------------------------------------
-- (5) Affixes as data. kind 0 = mob affix (on the affixed share of the mobs),
--     1 = run affix (the whole instance). weekly 1 = part of the weekly
--     rotation (DungeonChallenge.WeeklyAffixCount per week, turning Wednesday
--     00:00 UTC); weekly 0 = always on from min_level. The behaviour is code:
--     value1/value2 are its numbers (0 = the built-in default).
--       1 Call for Help   value1 = pull radius (yd)
--       3 Big Boy         value1 = health factor
--       4 Immolation Aura value1 = fire damage per challenge level, value2 = range (yd)
--       7 Lil' Bro        value1 = each split's share of the health
--       8 Damage Reduce   value1 = damage reduction (0.25 = 25 %), value2 = range (yd)
--       9 Bigger Boy      value1 = health factor
--      10 Hell Touched    value1 = extra damage per hit
--      11 Lightning Sphere value1 = seconds between spawn rolls, value2 = spawn chance %
--      12 Entangling Roots value1 = seconds between rolls per player, value2 = chance %
--      13 Enrage          value1 = seconds between rolls per mob in combat, value2 = chance %
--     11-13 follow silviu20092's mod-mythic-plus defaults.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `dungeon_challenge_affix` (
    `affix_id` TINYINT UNSIGNED NOT NULL,
    `name` VARCHAR(50) NOT NULL,
    `description` VARCHAR(255) NOT NULL DEFAULT '',
    `min_level` INT UNSIGNED NOT NULL DEFAULT 1,
    `kind` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = mob affix, 1 = run affix',
    `weekly` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = weekly rotation, 0 = always on',
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `value1` FLOAT NOT NULL DEFAULT 0,
    `value2` FLOAT NOT NULL DEFAULT 0,
    `spell_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'spell link shown in the UI',
    PRIMARY KEY (`affix_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='mod-dungeon-challenge: affix switches, levels, rotation and numbers';

DELETE FROM `dungeon_challenge_affix` WHERE `affix_id` BETWEEN 1 AND 13;
INSERT INTO `dungeon_challenge_affix` (`affix_id`, `name`, `description`, `min_level`, `kind`, `weekly`, `enabled`, `value1`, `value2`, `spell_id`) VALUES
(1,  'Call for Help',    'Calls allies within 30y for help when entering combat.', 10, 0, 0, 1, 30, 0, 900060),
(2,  'Speedy',           '+100% movement speed and +10% attack speed.', 20, 0, 0, 1, 0, 0, 900050),
(3,  'Big Boy',          '+50% HP and increased size.', 30, 0, 0, 1, 1.5, 0, 900056),
(4,  'Immolation Aura',  'Deals periodic fire damage (Level x 80) to nearby players.', 40, 0, 0, 1, 80, 8, 900051),
(5,  'CC Immunity',      'Immune to all crowd control effects.', 50, 0, 0, 1, 0, 0, 900052),
(6,  'Heavy Hits',       '+33% damage dealt.', 60, 0, 0, 1, 0, 0, 900058),
(7,  'Lil'' Bro',        'Splits into 2 copies on death (1->2->4), each with -90% HP.', 70, 0, 0, 1, 0.1, 0, 900059),
(8,  'Damage Reduce',    'Allies within 30y take 25% less damage.', 80, 0, 0, 1, 0.25, 30, 900055),
(9,  'Bigger Boy',       'Additional +50% HP, increased size and +10% damage.', 90, 0, 0, 1, 1.5, 0, 900057),
(10, 'Hell Touched',     'Deals 666 hellfire damage on hit. Reduces stats by 10% (10s, stacks 10).', 100, 0, 0, 1, 666, 0, 900054),
(11, 'Lightning Sphere', 'Lightning Spheres appear near the group and shock everyone within 100y until killed.', 10, 1, 1, 1, 25, 65, 61585),
(12, 'Entangling Roots', 'Nearby enemies now and then root a player in place.', 10, 1, 1, 1, 10, 15.33, 57095),
(13, 'Enrage',           'Enemies in combat, bosses included, now and then enrage.', 10, 1, 1, 1, 2, 6.5, 55285);

-- ----------------------------------------------------------------------------
-- (7) Item rewards per level band for a completed run, on top of the gold and
--     Paragon XP formula; the count doubles in time. Default (operator may
--     change): Emblem of Frost, 1 per started 10 levels.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `dungeon_challenge_reward` (
    `min_level` INT UNSIGNED NOT NULL,
    `max_level` INT UNSIGNED NOT NULL,
    `item_entry` INT UNSIGNED NOT NULL,
    `item_count` INT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`min_level`, `item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='mod-dungeon-challenge: item rewards per challenge level band';

DELETE FROM `dungeon_challenge_reward` WHERE `item_entry` = 49426;
INSERT INTO `dungeon_challenge_reward` (`min_level`, `max_level`, `item_entry`, `item_count`) VALUES
(1,  9,   49426, 1),
(10, 19,  49426, 2),
(20, 29,  49426, 3),
(30, 39,  49426, 4),
(40, 49,  49426, 5),
(50, 59,  49426, 6),
(60, 69,  49426, 7),
(70, 79,  49426, 8),
(80, 89,  49426, 9),
(90, 100, 49426, 10);

-- ----------------------------------------------------------------------------
-- (9) Creatures that count as a challenge boss although their rank is below 3
--     (the Turtle dungeons' bosses are rank-1 elites).
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `dungeon_challenge_boss_entry` (
    `creature_entry` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `comment` VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='mod-dungeon-challenge: extra boss entries (below rank 3)';

-- ----------------------------------------------------------------------------
-- (6) Lightning Sphere, the run affix's orb (NPC 500100, 06-custom-ids): level
--     80, hostile, pacified (no melee), passive and rooted by its script;
--     display 25144 and the numbers follow silviu20092's mod-mythic-plus.
-- ----------------------------------------------------------------------------
DELETE FROM `creature_template_model` WHERE `CreatureID` = 500100;
DELETE FROM `creature_template` WHERE `entry` = 500100;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `rank`, `dmgschool`, `BaseAttackTime`, `RangeAttackTime`, `unit_class`, `unit_flags`, `type`, `type_flags`, `AIName`, `MovementType`, `HoverHeight`, `RegenHealth`, `flags_extra`, `HealthModifier`, `ScriptName`) VALUES
(500100, 'Lightning Sphere', 'Dungeon Challenge', 80, 80, 2, 14, 0, 1, 0, 2000, 0, 8, 131072, 10, 0, '', 0, 1, 1, 0, 3, 'npc_dc_lightning_sphere');
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(500100, 0, 25144, 1, 1, 0);

-- ----------------------------------------------------------------------------
-- Chat commands
-- ----------------------------------------------------------------------------
DELETE FROM `command` WHERE `name` IN ('dchallenge', 'dchallenge affixes', 'dchallenge status', 'dchallenge reload', 'dchallenge scale', 'dchallenge week', 'dchallenge force', 'dchallenge start', 'dchallenge forget');
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('dchallenge', 0, 'Syntax: .dchallenge $subcommand\r\nType .dchallenge to see the list of possible subcommands.'),
('dchallenge affixes', 0, 'Syntax: .dchallenge affixes [$level]\r\nShows this week''s Dungeon Challenge affix rotation, and the affixes a run at $level gets.'),
('dchallenge status', 2, 'Syntax: .dchallenge status\r\nShows the Dungeon Challenge run of your instance and the scaling of the selected creature.'),
('dchallenge reload', 3, 'Syntax: .dchallenge reload\r\nReloads the Dungeon Challenge affix, dungeon factor, reward and boss tables. Runs in progress keep their affixes.'),
('dchallenge scale', 3, 'Syntax: .dchallenge scale $mapId $trashHealth $trashDamage $bossHealth $bossDamage\r\nSets a dungeon''s factors until the next reload or restart (new runs and newly scaled creatures).'),
('dchallenge week', 3, 'Syntax: .dchallenge week $week\r\nPins the affix rotation to week number $week for previews and tests; 0 = the real week again. Not saved.'),
('dchallenge force', 3, 'Syntax: .dchallenge force $affixId\r\nThe next check of a run affix (11 Lightning Sphere, 12 Entangling Roots, 13 Enrage) in your run fires at once.'),
('dchallenge start', 3, 'Syntax: .dchallenge start $mapId $level\r\nStarts a Dungeon Challenge run for you and your group, as the NPC does.'),
('dchallenge forget', 3, 'Syntax: .dchallenge forget\r\nDrops the run of your instance from memory and keeps its saved row, as a restart does; the next participant to enter brings it back.');

-- ----------------------------------------------------------------------------
-- (9) The Turtle dungeons in the Dungeon Challenge, lifted to level 80.
--     Entrances = their areatrigger_teleport targets (60000-60006, 107).
--     Crescent Grove is enabled (its four bosses per the port ledger
--     turtle-port/ledger/02); the other four are staged disabled until their
--     boss lists are checked in a full clear (a wrong boss_count blocks the
--     run's completion).
-- ----------------------------------------------------------------------------
DELETE FROM `dungeon_challenge_dungeons` WHERE `map_id` IN (35, 800, 802, 808, 815);
INSERT INTO `dungeon_challenge_dungeons` (`map_id`, `name`, `entrance_x`, `entrance_y`, `entrance_z`, `entrance_o`, `timer_minutes`, `boss_count`, `raid_difficulty`, `enabled`) VALUES
(802, 'Turtle: Crescent Grove',   579.13,   90.7,    276.11, 3.4, 30, 4, 0, 1),
(35,  'Turtle: Stormwind Vault',  -0.91,    40.57,   -24.23, 0,   30, 6, 0, 0),
(800, 'Turtle: Karazhan Crypt',   -11068.1, -1806.4, 52.7,   1.5, 30, 7, 0, 0),
(808, 'Turtle: Hateforge Quarry', -8173.9,  -3120.6, 199.8,  4.7, 30, 4, 0, 0),
(815, 'Turtle: Gilneas City',     -1100,    2237.75, 182.86, 2.6, 35, 8, 0, 0);

DELETE FROM `dungeon_challenge_scale` WHERE `map_id` IN (35, 800, 802, 808, 815);
INSERT INTO `dungeon_challenge_scale` (`map_id`, `trash_health`, `trash_damage`, `boss_health`, `boss_damage`, `uplift_level`, `comment`) VALUES
(802, 1, 1, 1, 1, 80, 'Turtle Crescent Grove (levels 30-40)'),
(35,  1, 1, 1, 1, 80, 'Turtle Stormwind Vault (levels 61-62)'),
(800, 1, 1, 1, 1, 80, 'Turtle Karazhan Crypt (levels 60-63)'),
(808, 1, 1, 1, 1, 80, 'Turtle Hateforge Quarry (levels 51-57)'),
(815, 1, 1, 1, 1, 80, 'Turtle Gilneas City (levels 41-49)');

DELETE FROM `dungeon_challenge_boss_entry` WHERE `map_id` IN (35, 800, 802, 808, 815);
INSERT INTO `dungeon_challenge_boss_entry` (`creature_entry`, `map_id`, `comment`) VALUES
(930036, 802, 'Grovetender Engryss'),
(930038, 802, 'Keeper Ranathos'),
(930040, 802, 'Fenektis the Deceiver'),
(930039, 802, 'Master Raxxieth'),
(930073, 35,  'Black Bride'),
(930074, 35,  'Volkan Cruelblade'),
(930075, 35,  'Tham''Grarr'),
(930076, 35,  'Aszosh Grimflame'),
(930077, 35,  'Damian'),
(930080, 35,  'Arc''tiras'),
(930021, 800, 'Guard Captain Gort (the rank-3 bosses count already)'),
(930008, 800, 'Commander Andreon'),
(930095, 808, 'Hatereaver Annihilator'),
(930096, 808, 'High Foreman Bargul Blackhammer'),
(930097, 808, 'Engineer Figgles'),
(930098, 808, 'Har''gesh Doomcaller'),
(930129, 815, 'Marshal Magnus Greystone'),
(930134, 815, 'Horsemaster Levvin'),
(930127, 815, 'Judge Sutherland'),
(930128, 815, 'Dustivan Blackcowl'),
(930126, 815, 'Packmaster Ragetooth'),
(930125, 815, 'Matthias Holtz'),
(930114, 815, 'Regent-Lady Celia Harlow'),
(930115, 815, 'Regent-Lord Mortimer Harlow'),
(930124, 815, 'Genn Greymane');

-- The Harlow pair is one encounter.
DELETE FROM `dungeon_challenge_boss_group` WHERE `creature_entry` IN (930114, 930115);
INSERT INTO `dungeon_challenge_boss_group` (`creature_entry`, `map_id`, `group_id`) VALUES
(930114, 815, 2),
(930115, 815, 2);
