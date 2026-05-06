-- mod-playerbots-characters: initial schema
-- Creates all required tables if they do not already exist.
-- Also handles migration from legacy table formats.

-- ============================================================
-- Migration: mod_pbc_bot_location → mod_pbc_data
-- If the old table exists and the new one doesn't, rename it
-- and add the roll_chance_modifier column.
-- For fresh installs the CREATE TABLE IF NOT EXISTS below
-- handles everything.
-- ============================================================

SET @old_exists = (SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_bot_location');
SET @new_exists = (SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_data');

-- Rename old table to new name (only if old exists and new doesn't)
SET @sql = IF(@old_exists > 0 AND @new_exists = 0,
    'RENAME TABLE `mod_pbc_bot_location` TO `mod_pbc_data`',
    'SELECT ''Skip rename: mod_pbc_data already exists or mod_pbc_bot_location does not exist'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Add roll_chance_modifier column if it doesn't exist yet (upgrade path)
SET @col_exists = (SELECT COUNT(*) FROM information_schema.columns
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_data' AND column_name = 'roll_chance_modifier');

SET @sql = IF(@new_exists > 0 AND @col_exists = 0,
    'ALTER TABLE `mod_pbc_data` ADD COLUMN `roll_chance_modifier` INT NOT NULL DEFAULT 0 COMMENT ''Per-character roll chance modifier (-100 to 100), added to every roll chance'' AFTER `last_location`',
    'SELECT ''Skip add column: roll_chance_modifier already exists'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Drop last_location column if it still exists (location tracking removed)
SET @loc_col_exists = (SELECT COUNT(*) FROM information_schema.columns
    WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_data' AND column_name = 'last_location');

SET @sql = IF(@new_exists > 0 AND @loc_col_exists > 0,
    'ALTER TABLE `mod_pbc_data` DROP COLUMN `last_location`',
    'SELECT ''Skip drop column: last_location does not exist'' AS status');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Update the table comment (safe to run always, if table exists)
SET @sql = IF(@new_exists> 0,
    'ALTER TABLE `mod_pbc_data` COMMENT = ''Per-bot persistent data (roll modifier)''',
    'SELECT ''Skipped comment update: mod_pbc_data does not exist'' AS status'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ============================================================
-- Schema: all tables
-- ============================================================

-- Stores condensed history appended to a character's card (in the DB, not on disk).
CREATE TABLE IF NOT EXISTS `mod_pbc_character_card_additions` (
    `id`            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `bot_guid`      BIGINT UNSIGNED NOT NULL COMMENT 'GUID of the playerbot character',
    `addition`      MEDIUMTEXT NOT NULL         COMMENT 'Condensed text appended to the base character card',
    `created_at`    DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX `idx_bot_guid_created` (`bot_guid`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='LLM-condensed additions to bot character cards';

-- Per-bot chat and event history lines for LLM context.
-- Each row is one pre-formatted single-line string, e.g.:
--   "John: Hello, how are you?"
--   "*John picked up [Thunderfury, Blessed Blade of the Windseeker]*"
CREATE TABLE IF NOT EXISTS `mod_pbc_chat_history` (
    `id`        BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `bot_guid`  BIGINT UNSIGNED NOT NULL COMMENT 'GUID of the playerbot this history belongs to',
    `timestamp` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `message`   MEDIUMTEXT NOT NULL COMMENT 'Pre-formatted chat or event line (single-line, no embedded newlines)',
    INDEX `idx_bot_guid` (`bot_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Per-bot chat and event history lines for LLM context';

-- Per-bot persistent data: roll chance modifier.
-- Written when roll_modifier is set; loaded on server start so that
-- roll modifiers are preserved across server restarts.
CREATE TABLE IF NOT EXISTS `mod_pbc_data` (
    `bot_guid`              BIGINT UNSIGNED NOT NULL PRIMARY KEY COMMENT 'GUID of the playerbot character',
    `roll_chance_modifier`  INT NOT NULL DEFAULT 0 COMMENT 'Per-character roll chance modifier (-100 to 100), added to every roll chance',
    `updated_at`            DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Per-bot persistent data (roll modifier)';

-- LLM-generated relationship descriptions per bot, one row per (bot, target) pair.
CREATE TABLE IF NOT EXISTS `mod_pbc_relationships` (
    `id`                         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `bot_guid`                   BIGINT UNSIGNED NOT NULL COMMENT 'GUID of the playerbot that owns this relationship',
    `target_name`                VARCHAR(64)     NOT NULL COMMENT 'Name of the other character',
    `relationship_text`          MEDIUMTEXT      NOT NULL COMMENT 'LLM-generated relationship description',
    `mention_count_at_last_update` INT UNSIGNED  NOT NULL DEFAULT 0 COMMENT 'Total mention count in history at the time of the last relationship update',
    `updated_at`                 DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY `uq_bot_target` (`bot_guid`, `target_name`),
    INDEX `idx_bot_guid` (`bot_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='LLM-generated per-bot relationship descriptions with other characters';
