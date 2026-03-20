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
    `completion_time` INT UNSIGNED NOT NULL COMMENT 'in seconds',
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
    `completion_time` INT UNSIGNED NOT NULL COMMENT 'in seconds, 0 = failed',
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
