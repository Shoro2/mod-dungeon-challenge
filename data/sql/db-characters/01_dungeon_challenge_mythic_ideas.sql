-- ============================================================================
-- mod-dungeon-challenge: Mythic+ ideas 8 and 10 (survey 2026-10-10, operator
-- 2026-10-16: "ja die hören sich alle gut an"). Idempotent: the updater
-- re-applies this file whenever its bytes change.
-- ============================================================================

-- (8) Combat time per boss: seconds from the first player hit to the kill.
SET @col_exists = (SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'dungeon_challenge_snapshot' AND COLUMN_NAME = 'combat_time');
SET @ddl = IF(@col_exists = 0,
    'ALTER TABLE `dungeon_challenge_snapshot` ADD COLUMN `combat_time` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT ''seconds from the first player hit to the kill'' AFTER `player_guid`',
    'SELECT 1');
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- (10) A running run, so it survives a restart or an instance unload. Written
-- at the start, after each boss kill and death; deleted when the run ends.
-- start_time is absolute: the downtime counts against the timer.
CREATE TABLE IF NOT EXISTS `dungeon_challenge_active` (
    `instance_id` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `difficulty` INT UNSIGNED NOT NULL,
    `start_time` INT UNSIGNED NOT NULL COMMENT 'unix time the run started',
    `timer_duration` INT UNSIGNED NOT NULL,
    `death_count` INT UNSIGNED NOT NULL DEFAULT 0,
    `penalty_time` INT UNSIGNED NOT NULL DEFAULT 0,
    `bosses_killed` INT UNSIGNED NOT NULL DEFAULT 0,
    `leader_guid` INT UNSIGNED NOT NULL,
    `participants` VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'character guids, comma-separated',
    `affixes` VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'affix ids of the run, comma-separated',
    `affix_week` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`instance_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='mod-dungeon-challenge: running runs, restored after a restart';
