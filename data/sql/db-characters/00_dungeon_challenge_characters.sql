-- ============================================================================
-- mod-dungeon-challenge: Characters Database Setup
-- ============================================================================

-- ============================================================================
-- Leaderboard Table
-- ============================================================================

DROP TABLE IF EXISTS `dungeon_challenge_leaderboard`;
CREATE TABLE `dungeon_challenge_leaderboard` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `map_id` INT UNSIGNED NOT NULL,
    `difficulty` INT UNSIGNED NOT NULL,
    `completion_time` INT UNSIGNED NOT NULL COMMENT 'in seconds (includes penalty)',
    `death_count` INT UNSIGNED NOT NULL DEFAULT 0,
    `leader_name` VARCHAR(50) NOT NULL,
    `leader_guid` INT UNSIGNED NOT NULL,
    `date_completed` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `participants` TEXT NOT NULL COMMENT 'comma-separated player names',
    PRIMARY KEY (`id`),
    INDEX `idx_map_difficulty` (`map_id`, `difficulty`),
    INDEX `idx_leader` (`leader_guid`),
    INDEX `idx_time` (`map_id`, `difficulty`, `completion_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- Player Challenge History
-- ============================================================================

DROP TABLE IF EXISTS `dungeon_challenge_history`;
CREATE TABLE `dungeon_challenge_history` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `player_guid` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `difficulty` INT UNSIGNED NOT NULL,
    `completion_time` INT UNSIGNED NOT NULL COMMENT 'in seconds (includes penalty), 0 = failed',
    `death_count` INT UNSIGNED NOT NULL DEFAULT 0,
    `in_time` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `date_completed` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    INDEX `idx_player` (`player_guid`),
    INDEX `idx_dungeon` (`map_id`, `difficulty`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- Player Best Scores (aggregated)
-- ============================================================================

DROP TABLE IF EXISTS `dungeon_challenge_best`;
CREATE TABLE `dungeon_challenge_best` (
    `player_guid` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `best_difficulty` INT UNSIGNED NOT NULL DEFAULT 0,
    `best_time` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'in seconds',
    `total_runs` INT UNSIGNED NOT NULL DEFAULT 0,
    `total_completions` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`player_guid`, `map_id`),
    INDEX `idx_difficulty` (`map_id`, `best_difficulty`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- Pending Challenge (communication between Lua GameObject UI and C++ scripts)
-- ============================================================================

DROP TABLE IF EXISTS `dungeon_challenge_pending`;
CREATE TABLE `dungeon_challenge_pending` (
    `player_guid` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `difficulty` INT UNSIGNED NOT NULL,
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`player_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- Boss Kill Snapshots (detailed per-boss-kill records)
-- ============================================================================

DROP TABLE IF EXISTS `dungeon_challenge_snapshot`;
CREATE TABLE `dungeon_challenge_snapshot` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `instance_id` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `difficulty` INT UNSIGNED NOT NULL,
    `start_time` INT UNSIGNED NOT NULL COMMENT 'unix timestamp of run start',
    `snap_time` INT UNSIGNED NOT NULL COMMENT 'elapsed seconds when boss was killed',
    `timer_limit` INT UNSIGNED NOT NULL COMMENT 'total allowed time in seconds',
    `creature_entry` INT UNSIGNED NOT NULL,
    `creature_name` VARCHAR(100) NOT NULL,
    `is_final_boss` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `rewarded` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `deaths` INT UNSIGNED NOT NULL DEFAULT 0,
    `penalty_time` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'accumulated death penalty in seconds',
    `player_name` VARCHAR(50) NOT NULL,
    `player_guid` INT UNSIGNED NOT NULL,
    `date_created` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    INDEX `idx_map_difficulty` (`map_id`, `difficulty`),
    INDEX `idx_instance` (`instance_id`),
    INDEX `idx_player` (`player_guid`),
    INDEX `idx_final_boss` (`map_id`, `is_final_boss`, `snap_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
