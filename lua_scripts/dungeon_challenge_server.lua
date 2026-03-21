-- ============================================================================
-- mod-dungeon-challenge: AIO Server Script
-- Handles server-side logic and communicates with client UI via AIO
--
-- Replaces the old gossip-based UI with a proper client-side AIO frame.
-- Communication with C++ module still happens via `dungeon_challenge_pending`.
-- ============================================================================

local AIO = AIO or require("AIO")

-- Register the client-side UI addon to be sent to players
-- Path is relative to worldserver.exe and must match the deployed location
AIO.AddAddon("lua_scripts/Dungeon_Challenge/dungeon_challenge_ui.lua", "DChallengeUI")

-- ============================================================================
-- Configuration (must match worldserver.conf values)
-- ============================================================================

local GO_ENTRY = 500002

local CONFIG = {
    MAX_DIFFICULTY          = 100,
    HP_MULT_PER_LEVEL       = 0.15,
    DMG_MULT_PER_LEVEL      = 0.08,
    DEATH_PENALTY_SECONDS   = 15,
    AFFIX_PERCENTAGE        = 10,
}

-- ============================================================================
-- Affix Data
-- ============================================================================

-- Every 10 levels adds +1 affix. Selected mobs receive ALL available affixes.
local AFFIXES = {
    { id = 1,  name = "Call for Help",   desc = "Calls allies within 30y for help",                     minDiff = 10,  spellId = 900060 },
    { id = 2,  name = "Speedy",          desc = "+100% move speed, +10% attack speed",                  minDiff = 20,  spellId = 900050 },
    { id = 3,  name = "Big Boy",         desc = "+50% HP, increased size",                              minDiff = 30,  spellId = 900056 },
    { id = 4,  name = "Immolation Aura", desc = "Periodic fire damage (Level x 80) to nearby players",  minDiff = 40,  spellId = 900051 },
    { id = 5,  name = "CC Immunity",     desc = "Immune to all crowd control",                           minDiff = 50,  spellId = 900052 },
    { id = 6,  name = "Heavy Hits",      desc = "+33% damage",                                           minDiff = 60,  spellId = 900058 },
    { id = 7,  name = "Lil' Bro",        desc = "Splits into 2 on death (1->2->4), -90% HP each tier",  minDiff = 70,  spellId = 900059 },
    { id = 8,  name = "Damage Reduce",   desc = "Allies within 30y take -25% damage",                    minDiff = 80,  spellId = 900055 },
    { id = 9,  name = "Bigger Boy",      desc = "Additional +50% HP, increased size, +10% damage",       minDiff = 90,  spellId = 900057 },
    { id = 10, name = "Hell Touched",    desc = "+666 hellfire dmg on hit, -10% stats (10s, stacks 10)", minDiff = 100, spellId = 900054 },
}

-- ============================================================================
-- Active Run UI Tracking State
-- ============================================================================

local trackedRuns = {}  -- playerGuidLow -> shared run tracking table

local function BuildAffixString(difficulty)
    local names = {}
    for _, a in ipairs(AFFIXES) do
        if difficulty >= a.minDiff then
            table.insert(names, a.name)
        end
    end
    if #names == 0 then return "None" end
    return table.concat(names, ", ")
end

-- ============================================================================
-- Runtime Data
-- ============================================================================

local dungeons = {}

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
    print("[mod-dungeon-challenge] AIO Server: Loaded " .. #dungeons .. " dungeons.")
end

LoadDungeons()

-- ============================================================================
-- Helper: Build config table for client (small, safe for init message)
-- ============================================================================

local function GetConfigForClient()
    return {
        maxDifficulty       = CONFIG.MAX_DIFFICULTY,
        hpMultPerLevel      = CONFIG.HP_MULT_PER_LEVEL,
        dmgMultPerLevel     = CONFIG.DMG_MULT_PER_LEVEL,
        deathPenaltySeconds = CONFIG.DEATH_PENALTY_SECONDS,
        affixPercentage     = CONFIG.AFFIX_PERCENTAGE,
    }
end

-- ============================================================================
-- Send initial data to client on login
-- Uses individual messages per dungeon/affix to avoid AIO message size limits
-- ============================================================================

AIO.AddOnInit(function(msg, player)
    if player then
        -- Send config via init message (small payload)
        msg:Add("DungeonChallenge", "InitConfig", GetConfigForClient())
    end
    return msg
end)

-- Send dungeons and affixes individually after login via player event
local PLAYER_EVENT_ON_LOGIN = 3

RegisterPlayerEvent(PLAYER_EVENT_ON_LOGIN, function(event, player)
    -- Small delay to ensure AIO init has completed
    player:RegisterEvent(function(eventId, delay, repeats, pl)
        -- Send total counts first so client knows what to expect
        AIO.Handle(pl, "DungeonChallenge", "InitBegin", #dungeons, #AFFIXES)

        -- Send each dungeon as flat parameters (no nested tables)
        for _, d in ipairs(dungeons) do
            AIO.Handle(pl, "DungeonChallenge", "InitDungeon",
                d.mapId, d.name, d.timerMinutes, d.bossCount)
        end

        -- Send each affix as flat parameters (including spellId for links)
        for _, a in ipairs(AFFIXES) do
            AIO.Handle(pl, "DungeonChallenge", "InitAffix",
                a.id, a.name, a.desc, a.minDiff, a.spellId)
        end

        -- Signal that init is complete
        AIO.Handle(pl, "DungeonChallenge", "InitComplete")

        print("[mod-dungeon-challenge] AIO: Sent init to " .. pl:GetName()
            .. " (" .. #dungeons .. " dungeons, " .. #AFFIXES .. " affixes)")
    end, 1, 1, 1) -- 1ms delay, 1 repeat
end)

-- ============================================================================
-- AIO Handlers (server-side, called from client)
-- ============================================================================

local ServerHandlers = {}

-- Client requests to open the UI (from GameObject click)
ServerHandlers.RequestOpen = function(player)
    -- Just echo back — the client already has all dungeon data from Init
    AIO.Handle(player, "DungeonChallenge", "ShowUI")
end

-- Client requests leaderboard for a specific dungeon
ServerHandlers.RequestLeaderboard = function(player, mapId)
    if not mapId or type(mapId) ~= "number" then return end

    local entries = {}
    local query = CharDBQuery(string.format(
        "SELECT difficulty, completion_time, death_count, leader_name "
        .. "FROM dungeon_challenge_leaderboard "
        .. "WHERE map_id = %d ORDER BY difficulty DESC, completion_time ASC LIMIT 20",
        mapId))

    if query then
        repeat
            table.insert(entries, {
                difficulty = query:GetUInt32(0),
                time       = query:GetUInt32(1),
                deaths     = query:GetUInt32(2),
                leader     = query:GetString(3),
            })
        until not query:NextRow()
    end

    AIO.Handle(player, "DungeonChallenge", "LeaderboardData", mapId, entries)
end

-- Client requests personal best runs
ServerHandlers.RequestMyRuns = function(player)
    local entries = {}
    local query = CharDBQuery(string.format(
        "SELECT map_id, difficulty, completion_time, death_count "
        .. "FROM dungeon_challenge_leaderboard "
        .. "WHERE leader_guid = %d ORDER BY difficulty DESC, completion_time ASC LIMIT 20",
        player:GetGUIDLow()))

    if query then
        repeat
            table.insert(entries, {
                mapId      = query:GetUInt32(0),
                difficulty = query:GetUInt32(1),
                time       = query:GetUInt32(2),
                deaths     = query:GetUInt32(3),
            })
        until not query:NextRow()
    end

    AIO.Handle(player, "DungeonChallenge", "MyRunsData", entries)
end

-- Client requests boss kill snapshots for a dungeon
ServerHandlers.RequestSnapshots = function(player, mapId)
    if not mapId or type(mapId) ~= "number" then return end

    local entries = {}
    local query = CharDBQuery(string.format(
        "SELECT difficulty, creature_name, snap_time, deaths, penalty_time, "
        .. "player_name, is_final_boss, rewarded "
        .. "FROM dungeon_challenge_snapshot "
        .. "WHERE map_id = %d ORDER BY difficulty DESC, snap_time ASC LIMIT 30",
        mapId))

    if query then
        repeat
            table.insert(entries, {
                difficulty = query:GetUInt32(0),
                bossName   = query:GetString(1),
                snapTime   = query:GetUInt32(2),
                deaths     = query:GetUInt32(3),
                penalty    = query:GetUInt32(4),
                playerName = query:GetString(5),
                isFinal    = query:GetUInt8(6) == 1,
                rewarded   = query:GetUInt8(7) == 1,
            })
        until not query:NextRow()
    end

    AIO.Handle(player, "DungeonChallenge", "SnapshotData", mapId, entries)
end

-- Client requests to start a challenge run
ServerHandlers.StartChallenge = function(player, mapId, difficulty)
    if not mapId or not difficulty then return end
    if type(mapId) ~= "number" or type(difficulty) ~= "number" then return end
    if difficulty < 1 or difficulty > CONFIG.MAX_DIFFICULTY then
        AIO.Handle(player, "DungeonChallenge", "Error", "Invalid difficulty level!")
        return
    end

    -- Find dungeon
    local dungeon = nil
    for _, d in ipairs(dungeons) do
        if d.mapId == mapId then
            dungeon = d
            break
        end
    end

    if not dungeon then
        AIO.Handle(player, "DungeonChallenge", "Error", "Dungeon not found!")
        return
    end

    -- Validate group size
    local group = player:GetGroup()
    if group and group:GetMembersCount() > 5 then
        AIO.Handle(player, "DungeonChallenge", "Error",
            "Maximum of 5 players allowed!")
        return
    end

    -- Note: Heroic difficulty is set by C++ OnPlayerMapChanged hook

    -- Store pending challenge in DB (read by C++ OnPlayerMapChanged)
    local guid = player:GetGUIDLow()
    CharDBExecute(string.format(
        "REPLACE INTO `dungeon_challenge_pending` "
        .. "(`player_guid`, `map_id`, `difficulty`) VALUES (%d, %d, %d)",
        guid, mapId, difficulty))

    -- Announce, unbind instances, and teleport
    if group then
        local members = group:GetMembers()
        for _, member in ipairs(members) do
            -- Store pending for each group member
            CharDBExecute(string.format(
                "REPLACE INTO `dungeon_challenge_pending` "
                .. "(`player_guid`, `map_id`, `difficulty`) VALUES (%d, %d, %d)",
                member:GetGUIDLow(), mapId, difficulty))

            AIO.Handle(member, "DungeonChallenge", "ChallengeStarted",
                dungeon.name, difficulty, player:GetName())

            -- Clear all instance lockouts before teleporting
            member:UnbindAllInstances()

            member:Teleport(mapId,
                dungeon.entranceX, dungeon.entranceY,
                dungeon.entranceZ, dungeon.entranceO)
        end
    else
        AIO.Handle(player, "DungeonChallenge", "ChallengeStarted",
            dungeon.name, difficulty, player:GetName())

        -- Clear all instance lockouts before teleporting
        player:UnbindAllInstances()

        player:Teleport(mapId,
            dungeon.entranceX, dungeon.entranceY,
            dungeon.entranceZ, dungeon.entranceO)
    end

    -- Track run for active UI
    local runTrack = {
        mapId = mapId,
        difficulty = difficulty,
        dungeonName = dungeon.name,
        timerSeconds = dungeon.timerMinutes * 60,
        totalBosses = dungeon.bossCount,
        bossesKilled = 0,
        deathCount = 0,
        penaltyTime = 0,
        startTime = nil,
        state = "pending",
        bossKills = {},
        killedBossGuids = {},
        affixString = BuildAffixString(difficulty),
    }

    if group then
        for _, member in ipairs(group:GetMembers()) do
            trackedRuns[member:GetGUIDLow()] = runTrack
        end
    else
        trackedRuns[guid] = runTrack
    end
end

AIO.AddHandlers("DungeonChallenge", ServerHandlers)

-- ============================================================================
-- GameObject Gossip: Open UI via AIO instead of gossip menus
-- ============================================================================

local function OnGossipHello(event, player, object)
    player:GossipComplete()
    AIO.Handle(player, "DungeonChallenge", "ShowUI")
end

RegisterGameObjectGossipEvent(GO_ENTRY, 1, OnGossipHello)

-- ============================================================================
-- Active Run UI: Eluna Event Hooks
-- ============================================================================

-- Detect dungeon entry: start run timer + send RunStart to client
RegisterPlayerEvent(28, function(event, player)  -- PLAYER_EVENT_ON_MAP_CHANGE
    local guid = player:GetGUIDLow()
    local run = trackedRuns[guid]
    if not run then return end

    local newMapId = player:GetMapId()

    if newMapId == run.mapId then
        if run.state == "pending" then
            -- First player to enter starts the run timer
            run.state = "running"
            run.startTime = os.time()
        end
        if run.state == "running" then
            -- Send RunStart to this player (works for first and subsequent members)
            AIO.Handle(player, "DungeonChallenge", "RunStart",
                run.dungeonName, run.difficulty, run.timerSeconds,
                run.totalBosses, run.affixString)
        end
    elseif run.state == "running" and newMapId ~= run.mapId then
        -- Player left the dungeon
        AIO.Handle(player, "DungeonChallenge", "RunEnd")
        trackedRuns[guid] = nil
    end
end)

-- Helper: Boss detection matching C++ IsChallengeBoss()
-- C++ checks: rank >= 3 OR isWorldBoss() OR IsDungeonBoss()
-- IsDungeonBoss() checks flags_extra set dynamically by instance scripts
local function IsChallengeBoss(creature)
    if creature:GetRank() >= 3 then return true end
    if creature:IsWorldBoss() then return true end
    -- IsDungeonBoss may not exist in all Eluna versions; check safely
    if creature.IsDungeonBoss and creature:IsDungeonBoss() then return true end
    return false
end

-- Detect boss kills: update tracker for all participants
RegisterPlayerEvent(7, function(event, player, creature)  -- PLAYER_EVENT_ON_KILL_CREATURE
    local guid = player:GetGUIDLow()
    local run = trackedRuns[guid]
    if not run or run.state ~= "running" then return end
    if not IsChallengeBoss(creature) then return end

    -- Prevent double-counting (hook fires per player in group)
    local cGuid = creature:GetGUIDLow()
    if run.killedBossGuids[cGuid] then return end
    run.killedBossGuids[cGuid] = true

    run.bossesKilled = run.bossesKilled + 1
    local elapsed = os.time() - run.startTime
    local bossName = creature:GetName()
    table.insert(run.bossKills, { name = bossName, time = elapsed })

    -- Notify all participants
    local group = player:GetGroup()
    if group then
        for _, member in ipairs(group:GetMembers()) do
            AIO.Handle(member, "DungeonChallenge", "BossKilled",
                bossName, run.bossesKilled, elapsed)
        end
    else
        AIO.Handle(player, "DungeonChallenge", "BossKilled",
            bossName, run.bossesKilled, elapsed)
    end

    -- Check completion
    if run.bossesKilled >= run.totalBosses then
        run.state = "completed"
        local effectiveElapsed = elapsed + run.penaltyTime
        local inTime = effectiveElapsed <= run.timerSeconds

        local function notifyComplete(p)
            AIO.Handle(p, "DungeonChallenge", "RunCompleted",
                effectiveElapsed, inTime)
            trackedRuns[p:GetGUIDLow()] = nil
        end

        if group then
            for _, member in ipairs(group:GetMembers()) do
                notifyComplete(member)
            end
        else
            notifyComplete(player)
        end
    end
end)

-- Detect player deaths: update tracker for all participants
RegisterPlayerEvent(8, function(event, player, killer)  -- PLAYER_EVENT_ON_KILLED_BY_CREATURE
    local guid = player:GetGUIDLow()
    local run = trackedRuns[guid]
    if not run or run.state ~= "running" then return end

    run.deathCount = run.deathCount + 1
    run.penaltyTime = run.penaltyTime + CONFIG.DEATH_PENALTY_SECONDS

    local group = player:GetGroup()
    if group then
        for _, member in ipairs(group:GetMembers()) do
            AIO.Handle(member, "DungeonChallenge", "DeathUpdate",
                run.deathCount, run.penaltyTime)
        end
    else
        AIO.Handle(player, "DungeonChallenge", "DeathUpdate",
            run.deathCount, run.penaltyTime)
    end
end)

-- Cleanup on logout
RegisterPlayerEvent(4, function(event, player)  -- PLAYER_EVENT_ON_LOGOUT
    trackedRuns[player:GetGUIDLow()] = nil
end)

print("[mod-dungeon-challenge] AIO Server: Script loaded (GO Entry: " .. GO_ENTRY .. ")")
