-- mod-dungeon-challenge: the four other Turtle dungeons passed a bot boss clear on 2026-10-17
-- (tests/dc_turtle_*.tbs, runs 771-774: every boss credited, the run cleared). Enabled here;
-- 01_dungeon_challenge_mythic_ideas.sql staged them disabled. Host: MIG-159.
UPDATE `dungeon_challenge_dungeons` SET `enabled` = 1 WHERE `map_id` IN (35, 800, 808, 815);
