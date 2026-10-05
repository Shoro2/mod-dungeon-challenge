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
    -- Must match the worldserver.conf values (DungeonChallenge.SummarySeconds / .DeathEndsRunMode)
    SUMMARY_SECONDS         = 30,   -- run-end summary / auto-leave countdown
    DEATH_ENDS_RUN          = 0,    -- 0 = wipe ends run, 1 = any death (C++ is authoritative; this is informational)
    MOB_SCAN_INTERVAL_MS    = 1000, -- mob counter HUD scan interval per instance
    PULL_SCAN_RANGE         = 100,  -- yards around each player scanned for pulled mobs
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
    { id = 10, name = "Hell Touched",    desc = "+666 hellfire dmg on hit, -2% stats per stack (10s, stacks 10)", minDiff = 100, spellId = 900054 },
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
local bossGroups = {}  -- creatureEntry -> groupId (multi-mob encounters count once)

-- ============================================================================
-- Load Dungeon Data from World DB
-- ============================================================================

local function LoadDungeons()
    dungeons = {}
    local query = WorldDBQuery(
        "SELECT map_id, name, entrance_x, entrance_y, entrance_z, entrance_o, "
        .. "timer_minutes, boss_count FROM dungeon_challenge_dungeons "
        .. "WHERE enabled = 1 ORDER BY map_id")
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

-- Multi-mob encounters (e.g. the Malachar trio in FL Ak'Tazia) share one boss
-- credit. C++ grants it on the LAST member's death; this tracker ticks the
-- group once on its FIRST member kill (aliveness scans are not practical in
-- Lua) — totals match, the HUD may just tick a grouped encounter early.
local function LoadBossGroups()
    bossGroups = {}
    local query = WorldDBQuery(
        "SELECT creature_entry, group_id FROM dungeon_challenge_boss_group")
    if query then
        repeat
            bossGroups[query:GetUInt32(0)] = query:GetUInt32(1)
        until not query:NextRow()
    end
end

LoadDungeons()
LoadBossGroups()

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
local startHandoffs = {} -- leaderGuidLow -> transient request; never a resumable run
local startingPlayers = {} -- memberGuidLow -> same request

local function CancelStart(handoff, message)
    startHandoffs[handoff.leaderLow] = nil
    for _, member in ipairs(handoff.members) do
        startingPlayers[member.low] = nil
        -- Remove only this unconsumed intent, never old snapshots/history/rewards.
        CharDBQuery(string.format(
            "DELETE FROM dungeon_challenge_pending WHERE player_guid=%d AND map_id=%d AND difficulty=%d",
            member.low, handoff.mapId, handoff.level))
        if trackedRuns[member.low] == handoff.run and handoff.run.state == "pending" then
            trackedRuns[member.low] = nil
        end
        local p = GetPlayerByGUID(member.guid)
        if p then AIO.Handle(p, "DungeonChallenge", "Error", message) end
    end
end

local function SameParty(handoff, players)
    local group = players[1]:GetGroup()
    if not handoff.groupGuid then return #players == 1 and not group end
    if not group or group:GetGUID() ~= handoff.groupGuid
        or not group:IsLeader(handoff.members[1].guid)
        or group:GetMembersCount() ~= #handoff.members then return false end
    for _, p in ipairs(players) do
        local current = p:GetGroup()
        if not current or current:GetGUID() ~= handoff.groupGuid then return false end
    end
    return true
end

-- Map change runs inside the worldport ACK stack. It only marks readiness here;
-- the next world update resolves fresh players and starts at most one next phase.
RegisterPlayerEvent(28, function(event, player)
    local h = startingPlayers[player:GetGUIDLow()]
    if not h then return end
    if h.phase == "outside" and player:GetMapId() ~= h.mapId then
        h.outside[player:GetGUIDLow()] = true
    elseif h.phase == "leader" and player:GetGUIDLow() == h.leaderLow
        and player:GetMapId() == h.mapId then
        h.leaderEntered = true
    end
end)

RegisterPlayerEvent(4, function(event, player) -- logout cancels; never retry on reconnect
    local h = startingPlayers[player:GetGUIDLow()]
    if h then CancelStart(h, "Start cancelled because a participant logged out.") end
end)

RegisterServerEvent(13, function(event, diff)
    local requests = {}
    for _, h in pairs(startHandoffs) do table.insert(requests, h) end
    for _, h in ipairs(requests) do
        if startHandoffs[h.leaderLow] == h then
            if os.time() >= h.deadline then
                CancelStart(h, "Start cancelled: map acknowledgement timed out.")
            else
                local players = {}
                for _, member in ipairs(h.members) do
                    local p = GetPlayerByGUID(member.guid)
                    if not p then break end -- normal far teleport is not in-world until ACK
                    table.insert(players, p)
                end
                if #players == #h.members then
                    local alive = true
                    for _, p in ipairs(players) do if not p:IsAlive() then alive = false end end
                    if not alive then
                        CancelStart(h, "Start cancelled because a participant died.")
                    elseif not SameParty(h, players) then
                        CancelStart(h, "Start cancelled because the party changed.")
                    elseif h.phase == "outside" then
                        local ready = true
                        for i, p in ipairs(players) do
                            if p:GetMapId() == h.mapId or not h.outside[h.members[i].low] then ready = false end
                        end
                        if ready then
                            h.phase = "leader"
                            if not players[1]:Teleport(h.mapId, h.dungeon.entranceX, h.dungeon.entranceY,
                                h.dungeon.entranceZ, h.dungeon.entranceO) then
                                CancelStart(h, "The leader could not enter a fresh challenge instance.")
                            end
                        end
                    elseif h.phase == "leader" and h.leaderEntered then
                        -- Leader ACK establishes the authoritative group destination
                        -- bind before any other member is admitted. No fixed ACK sleep.
                        if players[1]:GetMapId() ~= h.mapId
                            or players[1]:GetInstanceId() == h.members[1].oldInstance then
                            CancelStart(h, "The leader did not reach a fresh challenge instance.")
                        else
                            h.phase = "members"
                            for i = 2, #players do
                                if not players[i]:Teleport(h.mapId, h.dungeon.entranceX, h.dungeon.entranceY,
                                    h.dungeon.entranceZ, h.dungeon.entranceO) then
                                    CancelStart(h, "A participant could not enter the leader's instance.")
                                    break
                                end
                            end
                        end
                    elseif h.phase == "members" then
                        local ready = players[1]:GetMapId() == h.mapId
                        local instanceId = players[1]:GetInstanceId()
                        for _, p in ipairs(players) do
                            if p:GetMapId() ~= h.mapId or p:GetInstanceId() ~= instanceId then ready = false end
                        end
                        if ready then
                            startHandoffs[h.leaderLow] = nil
                            for _, member in ipairs(h.members) do startingPlayers[member.low] = nil end
                        end
                    end
                end
            end
        end
    end
end)

ServerHandlers.StartChallenge = function(player, mapId, difficulty)
    if not mapId or not difficulty then return end
    if type(mapId) ~= "number" or type(difficulty) ~= "number" then return end
    if mapId % 1 ~= 0 or difficulty % 1 ~= 0 or difficulty < 1 or difficulty > CONFIG.MAX_DIFFICULTY then
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
    if group and not group:IsLeader(player:GetGUID()) then
        AIO.Handle(player, "DungeonChallenge", "Error", "Only the group leader can start a challenge.")
        return
    end
    local members = { player } -- leader first; remaining entry waits for its ACK
    if group then
        for _, member in ipairs(group:GetMembers()) do
            if member:GetGUIDLow() ~= player:GetGUIDLow() then table.insert(members, member) end
        end
        if #members ~= group:GetMembersCount() then
            AIO.Handle(player, "DungeonChallenge", "Error", "All participants must be online.")
            return
        end
    end
    local guid = player:GetGUIDLow()
    local h = { leaderLow = guid, groupGuid = group and group:GetGUID(), members = {}, outside = {},
        mapId = mapId, level = difficulty, dungeon = dungeon, phase = "outside", deadline = os.time() + 30 }
    local frozen = {}
    for _, member in ipairs(members) do frozen[member:GetGUIDLow()] = true end
    for _, member in ipairs(members) do
        local low = member:GetGUIDLow()
        local previous = trackedRuns[low]
        if startingPlayers[low] or not member:IsAlive() or member:IsInCombat()
            or (previous and (previous.state == "running" or previous.state == "pending"))
            or CharDBQuery(string.format("SELECT player_guid FROM dungeon_challenge_pending WHERE player_guid=%d", low)) then
            AIO.Handle(player, "DungeonChallenge", "Error", "A participant is not ready for a new run.")
            return
        end
        local home = member:GetHomebind()
        if member:GetMapId() == mapId then
            local outsideMap = GetMapById(home.mapId, 0)
            if home.mapId == mapId or not outsideMap or outsideMap:IsDungeon() then
                AIO.Handle(player, "DungeonChallenge", "Error", "No safe outside destination is available.")
                return
            end
            for _, occupant in pairs(member:GetMap():GetPlayers()) do
                if not frozen[occupant:GetGUIDLow()] then
                    AIO.Handle(player, "DungeonChallenge", "Error", "Other players still occupy the old instance.")
                    return
                end
            end
        else
            h.outside[low] = true
        end
        table.insert(h.members, { guid = member:GetGUID(), low = low, home = home, oldInstance = member:GetInstanceId() })
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
        killedBossGroups = {},  -- groupId -> true (grouped encounters count once)
        affixString = BuildAffixString(difficulty),
        personalBest = {},   -- [guidLow] = pre-run personal best seconds (or nil)
        globalBest = nil,    -- pre-run global best seconds (or nil)
        exitDest = {},       -- [guidLow] = { map, x, y, z } home destination from the C++ signal
        exited = {},         -- [guidLow] = true once the exit teleport is accepted
    }

    -- Capture pre-run bests so the summary delta is not polluted by the run we are
    -- about to insert into the leaderboard. Global once; personal per member.
    local gbq = CharDBQuery(string.format(
        "SELECT MIN(completion_time) FROM dungeon_challenge_leaderboard "
        .. "WHERE map_id = %d AND difficulty = %d", mapId, difficulty))
    if gbq then
        local gv = gbq:GetUInt32(0)
        if gv and gv > 0 then runTrack.globalBest = gv end
    end

    local function capturePersonalBest(p)
        local pq = CharDBQuery(string.format(
            "SELECT MIN(completion_time) FROM dungeon_challenge_history "
            .. "WHERE player_guid = %d AND map_id = %d AND difficulty = %d AND completion_time > 0",
            p:GetGUIDLow(), mapId, difficulty))
        if pq then
            local pv = pq:GetUInt32(0)
            if pv and pv > 0 then runTrack.personalBest[p:GetGUIDLow()] = pv end
        end
    end

    if group then
        for _, member in ipairs(group:GetMembers()) do
            trackedRuns[member:GetGUIDLow()] = runTrack
            capturePersonalBest(member)
        end
    else
        trackedRuns[guid] = runTrack
        capturePersonalBest(player)
    end
    h.run = runTrack
    startHandoffs[guid] = h
    for _, member in ipairs(h.members) do
        startingPlayers[member.low] = h
        -- Existing synchronous pending is also the C++ pre-departure intent
        -- guard for NPC/native runs which have no Lua tracker. It is consumed
        -- only on target entry; cancellation removes its matching owned row.
        CharDBQuery(string.format(
            "INSERT INTO dungeon_challenge_pending (player_guid,map_id,difficulty) VALUES (%d,%d,%d)",
            member.low, mapId, difficulty))
        local intent = CharDBQuery(string.format(
            "SELECT map_id,difficulty FROM dungeon_challenge_pending WHERE player_guid=%d", member.low))
        if not intent or intent:GetUInt32(0) ~= mapId or intent:GetUInt32(1) ~= difficulty then
            CancelStart(h, "The Start request could not be recorded. No teleport was attempted.")
            return
        end
    end
    for i, member in ipairs(members) do
        AIO.Handle(member, "DungeonChallenge", "ChallengeStarted", dungeon.name, difficulty, player:GetName())
        if member:GetMapId() == mapId then
            local home = h.members[i].home
            if not member:Teleport(home.mapId, home.x, home.y, home.z, 0) then
                CancelStart(h, "The old run is active or its instance cannot be left safely.")
                return
            end
        end
    end
end

-- ============================================================================
-- Run-End Summary (pulled by the client when it detects death / dungeon clear)
-- ============================================================================

local function DungeonNameByMap(mapId)
    for _, d in ipairs(dungeons) do
        if d.mapId == mapId then return d.name end
    end
    return "Dungeon"
end

-- Read the C++ run-end signal for one player and, if present, render the summary.
-- Safe to call repeatedly: the signal row is deleted on first success.
local function TryShowSummaryFor(p)
    if not p then return end
    local g = p:GetGUIDLow()
    local run = trackedRuns[g]
    if run and run.exited[g] then return end  -- already leaving

    local q = CharDBQuery(string.format(
        "SELECT outcome, total_time, effective_time, in_time, deaths, map_id, difficulty, "
        .. "home_map, home_x, home_y, home_z "
        .. "FROM dungeon_challenge_runend WHERE player_guid = %d", g))
    if not q then return end  -- run has not ended yet; the client keeps polling

    local outcome    = q:GetUInt8(0)
    local effTime    = q:GetUInt32(2)
    local inTime     = q:GetUInt8(3) == 1
    local deaths     = q:GetUInt32(4)
    local mapId      = q:GetUInt32(5)
    local difficulty = q:GetUInt32(6)
    local homeMap    = q:GetUInt32(7)
    local homeX      = q:GetFloat(8)
    local homeY      = q:GetFloat(9)
    local homeZ      = q:GetFloat(10)

    -- Consume the signal so the summary is only sent once.
    CharDBExecute(string.format(
        "DELETE FROM dungeon_challenge_runend WHERE player_guid = %d", g))

    local dungeonName  = (run and run.dungeonName) or DungeonNameByMap(mapId)
    local bossKills    = (run and run.bossKills) or {}
    local personalBest = (run and run.personalBest[g]) or 0  -- 0 = no record (avoids nil mid-args)
    local globalBest   = (run and run.globalBest) or 0

    -- Store the home destination so RequestLeave can teleport the player out.
    if run then
        run.exitDest[g] = { map = homeMap, x = homeX, y = homeY, z = homeZ }
    end

    AIO.Handle(p, "DungeonChallenge", "ShowRunSummary",
        outcome, dungeonName, difficulty, effTime, inTime, deaths,
        bossKills, personalBest, globalBest, CONFIG.SUMMARY_SECONDS)
end

-- Client polls this while the local player is dead or after the last boss.
-- The first request also fans out to group members so everyone sees the summary
-- even if their own client did not trigger it (e.g. the "any death" mode).
ServerHandlers.RequestRunEnd = function(player)
    local group = player:GetGroup()
    if group then
        for _, member in ipairs(group:GetMembers()) do
            TryShowSummaryFor(member)
        end
    else
        TryShowSummaryFor(player)
    end
end

-- "Leave" button on the summary, and the client's auto-leave when the countdown
-- reaches 0: teleport the player home; binds are preserved until a fresh Start.
ServerHandlers.RequestLeave = function(player)
    local g = player:GetGUIDLow()
    local run = trackedRuns[g]
    if not run or not run.exitDest[g] or run.exited[g] then return end
    local d = run.exitDest[g]
    if player:Teleport(d.map, d.x, d.y, d.z, 0) then
        run.exited[g] = true -- Accepted request; actual map ACK remains authoritative.
    else
        AIO.Handle(player, "DungeonChallenge", "Error", "Could not leave the challenge instance.")
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

-- ============================================================================
-- Mob Counter HUD (top-center client frame)
--
-- One scanner per active challenge instance (MOB_SCAN_INTERVAL_MS). Counts:
--   pulled     — alive hostile creatures in combat within PULL_SCAN_RANGE of
--                any participant (grid search, so respawn copies, Lil' Bro
--                splits, bosses and their adds are all included)
--   left/total — alive / all regular DB-spawned mobs, using the same mob
--                definition as the C++ affix pass (non-boss, non-critter,
--                unit_class ~= 0, faction ~= 35). Temp summons are not
--                dungeon spawns and are excluded here by design.
-- Values are pushed to all instance players via AIO only when they change.
-- ============================================================================

local CREATURE_TYPE_CRITTER = 8
local FACTION_FRIENDLY = 35

-- Mirror of the C++ AssignAffixesToCreatures() mob filter
local function IsCountableMob(creature)
    if IsChallengeBoss(creature) then return false end
    if creature:GetCreatureType() == CREATURE_TYPE_CRITTER then return false end
    if creature:GetClass() == 0 then return false end
    if creature:GetFaction() == FACTION_FRIENDLY then return false end
    return true
end

local mobScanners = {}  -- instanceId -> { eventId, pulled, left, total }

local function StopMobScanner(instanceId)
    local scan = mobScanners[instanceId]
    if not scan then return end
    mobScanners[instanceId] = nil
    if scan.eventId then
        RemoveEventById(scan.eventId)
    end
end

local function ScanInstanceMobs(instanceId, mapId)
    if not mobScanners[instanceId] then return end

    -- The map is re-fetched every tick; the instance may be gone already
    local map = GetMapById(mapId, instanceId)
    if not map then
        StopMobScanner(instanceId)
        return
    end

    local players = map:GetPlayers()
    if not players or #players == 0 then
        StopMobScanner(instanceId)
        return
    end

    -- Regular spawn population: alive / all DB-spawned mobs.
    -- GetCreatures() is keyed by spawn id — pairs, never ipairs/#.
    local left, total = 0, 0
    for _, c in pairs(map:GetCreatures()) do
        if IsCountableMob(c) then
            total = total + 1
            if c:IsAlive() then
                left = left + 1
            end
        end
    end

    -- Pulled: hostile creatures in combat near any participant (deduped)
    local pulled = 0
    local seen = {}
    for _, p in pairs(players) do
        for _, c in pairs(p:GetCreaturesInRange(CONFIG.PULL_SCAN_RANGE, 0, 1, 1)) do
            local guid = c:GetGUIDLow()
            if not seen[guid] and c:IsInCombat()
                and c:GetCreatureType() ~= CREATURE_TYPE_CRITTER then
                seen[guid] = true
                pulled = pulled + 1
            end
        end
    end

    local scan = mobScanners[instanceId]
    if pulled == scan.pulled and left == scan.left and total == scan.total then
        return
    end
    scan.pulled, scan.left, scan.total = pulled, left, total

    for _, p in pairs(players) do
        AIO.Handle(p, "DungeonChallenge", "MobCounters", pulled, left, total)
    end
end

local function StartMobScanner(instanceId, mapId)
    if mobScanners[instanceId] then return end
    local scan = {}
    mobScanners[instanceId] = scan
    scan.eventId = CreateLuaEvent(function()
        ScanInstanceMobs(instanceId, mapId)
    end, CONFIG.MOB_SCAN_INTERVAL_MS, 0)  -- repeats = 0: run until removed
end

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

            -- Start the per-instance mob counter scanner (no-op if running).
            -- Late joiners/reconnects get the current values immediately —
            -- the scanner itself only pushes on change.
            local instanceId = player:GetInstanceId()
            StartMobScanner(instanceId, run.mapId)
            local scan = mobScanners[instanceId]
            if scan and scan.total then
                AIO.Handle(player, "DungeonChallenge", "MobCounters",
                    scan.pulled, scan.left, scan.total)
            end
        end
    elseif run.state == "running" and newMapId ~= run.mapId then
        -- Player left the dungeon
        AIO.Handle(player, "DungeonChallenge", "RunEnd")
        trackedRuns[guid] = nil
    end
end)

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

    -- Grouped encounter: count the group only once (see LoadBossGroups)
    local groupId = bossGroups[creature:GetEntry()]
    if groupId then
        if run.killedBossGroups[groupId] then return end
        run.killedBossGroups[groupId] = true
    end

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

        -- Keep trackedRuns alive: the run-end summary (pulled by the client via
        -- RequestRunEnd) needs bossKills/bests/exitDest. It is cleared when the
        -- player is teleported out (map-change handler) or on logout.
        local function notifyComplete(p)
            AIO.Handle(p, "DungeonChallenge", "RunCompleted",
                effectiveElapsed, inTime)
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
