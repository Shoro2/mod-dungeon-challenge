-- ============================================================================
-- mod-dungeon-challenge: GameObject Lua UI
-- Eluna script for AzerothCore 3.3.5a
--
-- This script registers gossip events on the Dungeon Challenge Stone
-- (GameObject entry 500002). Players click the stone to select a dungeon
-- and difficulty level, then get teleported into the instance.
--
-- Communication with the C++ module happens via the DB table
-- `dungeon_challenge_pending` (characters DB).
-- ============================================================================

local GO_ENTRY = 500002

-- ============================================================================
-- Configuration (must match worldserver.conf values)
-- ============================================================================

local CONFIG = {
    MAX_DIFFICULTY          = 20,
    HP_MULT_PER_LEVEL       = 0.15,   -- DungeonChallenge.HealthMultiplierPerLevel / 100
    DMG_MULT_PER_LEVEL      = 0.08,   -- DungeonChallenge.DamageMultiplierPerLevel / 100
    DEATH_PENALTY_SECONDS   = 15,     -- DungeonChallenge.DeathPenaltySeconds
}

-- ============================================================================
-- Gossip Action IDs
-- ============================================================================

local ACTION_MAIN_MENU          = 1000
local ACTION_SELECT_DUNGEON     = 1100
local ACTION_SELECT_DIFFICULTY  = 1200
local ACTION_START_RUN          = 1300
local ACTION_LEADERBOARD        = 1400
local ACTION_MY_RUNS            = 1500
local ACTION_SNAPSHOTS          = 1950

local DUNGEON_OFFSET            = 2000
local DIFFICULTY_OFFSET         = 3000
local LEADERBOARD_OFFSET        = 4000
local SNAPSHOT_OFFSET           = 5000

-- ============================================================================
-- Affix Data
-- ============================================================================

local AFFIXES = {
    { id = 1,  name = "Bolstering",  minDiff = 2  },
    { id = 2,  name = "Raging",      minDiff = 2  },
    { id = 3,  name = "Sanguine",    minDiff = 4  },
    { id = 4,  name = "Necrotic",    minDiff = 4  },
    { id = 5,  name = "Bursting",    minDiff = 7  },
    { id = 6,  name = "Explosive",   minDiff = 7  },
    { id = 7,  name = "Fortified",   minDiff = 2  },
    { id = 8,  name = "Volcanic",    minDiff = 10 },
    { id = 9,  name = "Storming",    minDiff = 10 },
    { id = 10, name = "Inspiring",   minDiff = 14 },
}

-- ============================================================================
-- Runtime Data
-- ============================================================================

local dungeons = {}             -- loaded from DB
local playerSelections = {}     -- guid -> { mapId, dungeonName, difficulty }

-- ============================================================================
-- Load Dungeon Data from World DB
-- ============================================================================

local function LoadDungeons()
    dungeons = {}
    local query = WorldDBQuery(
        "SELECT map_id, name, entrance_x, entrance_y, entrance_z, entrance_o, "
        .. "timer_minutes, boss_count FROM dungeon_challenge_dungeons ORDER BY map_id")
    if query then
        repeat
            table.insert(dungeons, {
                mapId        = query:GetUInt32(0),
                name         = query:GetString(1),
                entranceX    = query:GetFloat(2),
                entranceY    = query:GetFloat(3),
                entranceZ    = query:GetFloat(4),
                entranceO    = query:GetFloat(5),
                timerMinutes = query:GetUInt32(6),
                bossCount    = query:GetUInt32(7),
            })
        until not query:NextRow()
    end
    print("[mod-dungeon-challenge] Lua: Loaded " .. #dungeons .. " dungeons.")
end

-- Load immediately (Eluna loads scripts after world DB is ready)
LoadDungeons()

-- ============================================================================
-- Helper Functions
-- ============================================================================

local function GetHPMultiplier(difficulty)
    return 1.0 + (CONFIG.HP_MULT_PER_LEVEL * difficulty)
end

local function GetDMGMultiplier(difficulty)
    return 1.0 + (CONFIG.DMG_MULT_PER_LEVEL * difficulty)
end

local function GetAffixCountForDifficulty(difficulty)
    local count = 0
    for _, a in ipairs(AFFIXES) do
        if difficulty >= a.minDiff then
            count = count + 1
        end
    end
    return count
end

local function GetAffixNamesForDifficulty(difficulty)
    local names = {}
    for _, a in ipairs(AFFIXES) do
        if difficulty >= a.minDiff then
            table.insert(names, a.name)
        end
    end
    if #names == 0 then return "None" end
    return table.concat(names, ", ")
end

local function GetDungeonByIndex(index)
    return dungeons[index]
end

local function GetDungeonByMapId(mapId)
    for _, d in ipairs(dungeons) do
        if d.mapId == mapId then return d end
    end
    return nil
end

-- ============================================================================
-- Gossip: Main Menu
-- ============================================================================

local function ShowMainMenu(player, object)
    player:GossipClearMenu()

    player:GossipMenuAddItem(9,
        "|TInterface\\Icons\\Achievement_Dungeon_ClassicDungeonMaster:30:30|t Start Dungeon Challenge",
        0, ACTION_SELECT_DUNGEON)

    player:GossipMenuAddItem(2,
        "|TInterface\\Icons\\INV_Misc_Trophy_Argent:30:30|t Leaderboard",
        0, ACTION_LEADERBOARD)

    player:GossipMenuAddItem(0,
        "|TInterface\\Icons\\Achievement_BG_KillXEnemies_GeneralsRoom:30:30|t My Best Runs",
        0, ACTION_MY_RUNS)

    player:GossipMenuAddItem(0,
        "|TInterface\\Icons\\INV_Misc_Book_09:30:30|t Boss Kill Records",
        0, ACTION_SNAPSHOTS)

    player:GossipSendMenu(1, object)
end

-- ============================================================================
-- Gossip: Dungeon Selection
-- ============================================================================

local function ShowDungeonMenu(player, object)
    player:GossipClearMenu()

    for i, d in ipairs(dungeons) do
        local label = string.format(
            "|TInterface\\Icons\\Spell_Shadow_SummonVoidwalker:20:20|t %s (Timer: %d Min)",
            d.name, d.timerMinutes)
        player:GossipMenuAddItem(9, label, 0, DUNGEON_OFFSET + i)
    end

    player:GossipMenuAddItem(0, "<< Back", 0, ACTION_MAIN_MENU)
    player:GossipSendMenu(1, object)
end

-- ============================================================================
-- Gossip: Difficulty Selection
-- ============================================================================

local function ShowDifficultyMenu(player, object)
    player:GossipClearMenu()

    local guid = player:GetGUIDLow()
    if not playerSelections[guid] then
        ShowMainMenu(player, object)
        return
    end

    for d = 1, CONFIG.MAX_DIFFICULTY do
        local hpMult = GetHPMultiplier(d)
        local dmgMult = GetDMGMultiplier(d)
        local affixCount = GetAffixCountForDifficulty(d)

        local color
        if d <= 5 then       color = "|cff00ff00"
        elseif d <= 10 then  color = "|cffffff00"
        elseif d <= 15 then  color = "|cffff8000"
        else                 color = "|cffff0000"
        end

        local label = string.format(
            "%sLevel %d|r  |cffaaaaaa(HP: x%.1f, DMG: x%.1f, Affixes: %d)|r",
            color, d, hpMult, dmgMult, affixCount)
        player:GossipMenuAddItem(9, label, 0, DIFFICULTY_OFFSET + d)
    end

    player:GossipMenuAddItem(0, "<< Back", 0, ACTION_SELECT_DUNGEON)
    player:GossipSendMenu(1, object)
end

-- ============================================================================
-- Gossip: Confirm Menu
-- ============================================================================

local function ShowConfirmMenu(player, object)
    player:GossipClearMenu()

    local guid = player:GetGUIDLow()
    local sel = playerSelections[guid]
    if not sel or not sel.difficulty then
        ShowMainMenu(player, object)
        return
    end

    local hpMult   = GetHPMultiplier(sel.difficulty)
    local dmgMult  = GetDMGMultiplier(sel.difficulty)
    local affixStr = GetAffixNamesForDifficulty(sel.difficulty)
    local dungeon  = GetDungeonByMapId(sel.mapId)
    local timer    = dungeon and dungeon.timerMinutes or 30

    local confirmText = string.format(
        "|cff00ff00Confirm:|r\n\n"
        .. "Dungeon: |cffff8000%s|r\n"
        .. "Level: |cffff8000%d|r\n"
        .. "HP: |cffff0000x%.1f|r\n"
        .. "Damage: |cffff0000x%.1f|r\n"
        .. "Timer: |cffffff00%d minutes|r\n"
        .. "Death Penalty: |cffff0000+%ds per death|r\n"
        .. "Affixes: |cffff8000%s|r\n\n"
        .. "~5%% of mobs receive random affixes!",
        sel.dungeonName, sel.difficulty, hpMult, dmgMult,
        timer, CONFIG.DEATH_PENALTY_SECONDS, affixStr)

    player:SendBroadcastMessage(confirmText)

    player:GossipMenuAddItem(9,
        "|TInterface\\Icons\\Achievement_Dungeon_ClassicDungeonMaster:30:30|t |cff00ff00GO! Start Challenge!|r",
        0, ACTION_START_RUN)

    player:GossipMenuAddItem(0, "<< Change Difficulty", 0, ACTION_SELECT_DIFFICULTY)
    player:GossipMenuAddItem(0, "<< Change Dungeon",    0, ACTION_SELECT_DUNGEON)
    player:GossipMenuAddItem(0, "<< Main Menu",         0, ACTION_MAIN_MENU)
    player:GossipSendMenu(1, object)
end

-- ============================================================================
-- Gossip: Leaderboard
-- ============================================================================

local function ShowLeaderboardForDungeon(player, object, mapId, dungeonName)
    player:GossipClearMenu()

    player:SendBroadcastMessage(
        "|cff00ff00[Dungeon Challenge]|r Leaderboard: |cffff8000" .. dungeonName .. "|r")

    local query = CharDBQuery(string.format(
        "SELECT difficulty, completion_time, death_count, leader_name "
        .. "FROM dungeon_challenge_leaderboard "
        .. "WHERE map_id = %d ORDER BY completion_time ASC LIMIT 10", mapId))

    if not query then
        player:SendBroadcastMessage("  No entries yet.")
    else
        local rank = 1
        repeat
            local diff   = query:GetUInt32(0)
            local time   = query:GetUInt32(1)
            local deaths = query:GetUInt32(2)
            local leader = query:GetString(3)
            local min    = math.floor(time / 60)
            local sec    = time % 60

            player:SendBroadcastMessage(string.format(
                "  |cffffcc00#%d|r Level |cffff8000%d|r - |cff00ff00%d:%02d|r by |cff69ccf0%s|r (%d deaths)",
                rank, diff, min, sec, leader, deaths))
            rank = rank + 1
        until not query:NextRow()
    end

    player:GossipMenuAddItem(0, "<< Back to Leaderboard", 0, ACTION_LEADERBOARD)
    player:GossipMenuAddItem(0, "<< Main Menu",           0, ACTION_MAIN_MENU)
    player:GossipSendMenu(1, object)
end

local function ShowLeaderboardMenu(player, object)
    player:GossipClearMenu()

    for i, d in ipairs(dungeons) do
        player:GossipMenuAddItem(2, d.name, 0, LEADERBOARD_OFFSET + i)
    end

    player:GossipMenuAddItem(0, "<< Back", 0, ACTION_MAIN_MENU)
    player:GossipSendMenu(1, object)
end

-- ============================================================================
-- Gossip: My Best Runs
-- ============================================================================

local function ShowMyRuns(player, object)
    player:GossipClearMenu()

    player:SendBroadcastMessage("|cff00ff00[Dungeon Challenge]|r Your best runs:")

    local query = CharDBQuery(string.format(
        "SELECT map_id, difficulty, completion_time, death_count "
        .. "FROM dungeon_challenge_leaderboard "
        .. "WHERE leader_guid = %d ORDER BY completion_time ASC LIMIT 10",
        player:GetGUIDLow()))

    if not query then
        player:SendBroadcastMessage("  No completed challenges yet.")
    else
        repeat
            local mapId  = query:GetUInt32(0)
            local diff   = query:GetUInt32(1)
            local time   = query:GetUInt32(2)
            local deaths = query:GetUInt32(3)
            local dungeon = GetDungeonByMapId(mapId)
            local dName  = dungeon and dungeon.name or "Unknown"
            local min    = math.floor(time / 60)
            local sec    = time % 60

            player:SendBroadcastMessage(string.format(
                "  |cffff8000%s|r Level |cffff8000%d|r - |cff00ff00%d:%02d|r (%d deaths)",
                dName, diff, min, sec, deaths))
        until not query:NextRow()
    end

    player:GossipMenuAddItem(0, "<< Back", 0, ACTION_MAIN_MENU)
    player:GossipSendMenu(1, object)
end

-- ============================================================================
-- Gossip: Boss Kill Records (Snapshots)
-- ============================================================================

local function ShowSnapshotsForDungeon(player, object, mapId, dungeonName)
    player:GossipClearMenu()

    player:SendBroadcastMessage(
        "|cff00ff00[Dungeon Challenge]|r Boss Kill Records: |cffff8000" .. dungeonName .. "|r")

    local query = CharDBQuery(string.format(
        "SELECT difficulty, creature_name, snap_time, deaths, penalty_time, "
        .. "player_name, is_final_boss, rewarded "
        .. "FROM dungeon_challenge_snapshot "
        .. "WHERE map_id = %d ORDER BY difficulty DESC, snap_time ASC LIMIT 20", mapId))

    if not query then
        player:SendBroadcastMessage("  No records yet.")
    else
        repeat
            local diff      = query:GetUInt32(0)
            local bossName  = query:GetString(1)
            local snapTime  = query:GetUInt32(2)
            local deaths    = query:GetUInt32(3)
            local penalty   = query:GetUInt32(4)
            local pName     = query:GetString(5)
            local isFinal   = query:GetUInt8(6) == 1
            local rewarded  = query:GetUInt8(7) == 1
            local min       = math.floor(snapTime / 60)
            local sec       = snapTime % 60
            local finalStr  = isFinal and " |cff00ff00[FINAL BOSS]|r" or ""
            local rewardStr = rewarded and " |cffffcc00[REWARDED]|r" or ""

            player:SendBroadcastMessage(string.format(
                "  Level |cffff8000%d|r - |cffff8000%s|r at %d:%02d | "
                .. "%d deaths (+%ds) | %s%s%s",
                diff, bossName, min, sec, deaths, penalty, pName, finalStr, rewardStr))
        until not query:NextRow()
    end

    player:GossipMenuAddItem(0, "<< Back",      0, ACTION_SNAPSHOTS)
    player:GossipMenuAddItem(0, "<< Main Menu", 0, ACTION_MAIN_MENU)
    player:GossipSendMenu(1, object)
end

local function ShowSnapshotMenu(player, object)
    player:GossipClearMenu()

    for i, d in ipairs(dungeons) do
        player:GossipMenuAddItem(0, d.name, 0, SNAPSHOT_OFFSET + i)
    end

    player:GossipMenuAddItem(0, "<< Back", 0, ACTION_MAIN_MENU)
    player:GossipSendMenu(1, object)
end

-- ============================================================================
-- Start Challenge Run
-- ============================================================================

local function StartChallengeRun(player)
    player:GossipComplete()

    local guid = player:GetGUIDLow()
    local sel = playerSelections[guid]
    if not sel or not sel.difficulty then
        player:SendBroadcastMessage(
            "|cffff0000[Dungeon Challenge]|r Error: No selection found. Please try again.")
        return
    end

    local dungeon = GetDungeonByMapId(sel.mapId)
    if not dungeon then
        player:SendBroadcastMessage("|cffff0000[Dungeon Challenge]|r Dungeon not found!")
        return
    end

    -- Validate group size if in group
    local group = player:GetGroup()
    if group and group:GetMembersCount() > 5 then
        player:SendBroadcastMessage(
            "|cffff0000[Dungeon Challenge]|r Maximum of 5 players allowed!")
        return
    end

    -- Store pending challenge in DB (read by C++ OnPlayerMapChanged)
    CharDBExecute(string.format(
        "REPLACE INTO `dungeon_challenge_pending` "
        .. "(`player_guid`, `map_id`, `difficulty`) VALUES (%d, %d, %d)",
        guid, sel.mapId, sel.difficulty))

    -- Announce and teleport
    if group then
        local members = group:GetMembers()
        for _, member in ipairs(members) do
            member:SendBroadcastMessage(string.format(
                "|cff00ff00[Dungeon Challenge]|r |cffff8000%s|r started: "
                .. "|cff00ff00%s|r at Level |cffff8000%d|r!",
                player:GetName(), dungeon.name, sel.difficulty))
            member:Teleport(sel.mapId,
                dungeon.entranceX, dungeon.entranceY,
                dungeon.entranceZ, dungeon.entranceO)
        end
    else
        -- Solo mode
        player:SendBroadcastMessage(string.format(
            "|cff00ff00[Dungeon Challenge]|r Challenge started: "
            .. "|cff00ff00%s|r at Level |cffff8000%d|r! Teleporting...",
            dungeon.name, sel.difficulty))
        player:Teleport(sel.mapId,
            dungeon.entranceX, dungeon.entranceY,
            dungeon.entranceZ, dungeon.entranceO)
    end

    playerSelections[guid] = nil
end

-- ============================================================================
-- GameObject Gossip Event Handlers
-- ============================================================================

local function OnGossipHello(event, player, object)
    ShowMainMenu(player, object)
end

local function OnGossipSelect(event, player, object, sender, intid, code)
    player:GossipClearMenu()
    local guid = player:GetGUIDLow()

    -- Dungeon selection (DUNGEON_OFFSET+1 .. DIFFICULTY_OFFSET-1)
    if intid > DUNGEON_OFFSET and intid < DIFFICULTY_OFFSET then
        local index = intid - DUNGEON_OFFSET
        local dungeon = GetDungeonByIndex(index)
        if dungeon then
            playerSelections[guid] = {
                mapId       = dungeon.mapId,
                dungeonName = dungeon.name,
                difficulty  = nil,
            }
            ShowDifficultyMenu(player, object)
        end
        return
    end

    -- Difficulty selection (DIFFICULTY_OFFSET+1 .. LEADERBOARD_OFFSET-1)
    if intid > DIFFICULTY_OFFSET and intid < LEADERBOARD_OFFSET then
        local difficulty = intid - DIFFICULTY_OFFSET
        if playerSelections[guid] then
            playerSelections[guid].difficulty = difficulty
            ShowConfirmMenu(player, object)
        end
        return
    end

    -- Leaderboard dungeon selection
    if intid > LEADERBOARD_OFFSET and intid < SNAPSHOT_OFFSET then
        local index = intid - LEADERBOARD_OFFSET
        local dungeon = GetDungeonByIndex(index)
        if dungeon then
            ShowLeaderboardForDungeon(player, object, dungeon.mapId, dungeon.name)
        end
        return
    end

    -- Snapshot dungeon selection
    if intid > SNAPSHOT_OFFSET then
        local index = intid - SNAPSHOT_OFFSET
        local dungeon = GetDungeonByIndex(index)
        if dungeon then
            ShowSnapshotsForDungeon(player, object, dungeon.mapId, dungeon.name)
        end
        return
    end

    -- Named actions
    if intid == ACTION_MAIN_MENU then
        ShowMainMenu(player, object)
    elseif intid == ACTION_SELECT_DUNGEON then
        ShowDungeonMenu(player, object)
    elseif intid == ACTION_SELECT_DIFFICULTY then
        ShowDifficultyMenu(player, object)
    elseif intid == ACTION_START_RUN then
        StartChallengeRun(player)
    elseif intid == ACTION_LEADERBOARD then
        ShowLeaderboardMenu(player, object)
    elseif intid == ACTION_MY_RUNS then
        ShowMyRuns(player, object)
    elseif intid == ACTION_SNAPSHOTS then
        ShowSnapshotMenu(player, object)
    else
        ShowMainMenu(player, object)
    end
end

-- ============================================================================
-- Register Events
-- ============================================================================

RegisterGameObjectGossipEvent(GO_ENTRY, 1, OnGossipHello)   -- GOSSIP_EVENT_ON_HELLO
RegisterGameObjectGossipEvent(GO_ENTRY, 2, OnGossipSelect)  -- GOSSIP_EVENT_ON_SELECT

print("[mod-dungeon-challenge] Lua: GameObject gossip script loaded (Entry: " .. GO_ENTRY .. ")")
