-- ============================================================================
-- mod-dungeon-challenge: AIO Client UI
-- This file is sent to the WoW client via AIO and creates a proper UI frame.
-- ============================================================================

local AIO = AIO or require("AIO")

-- This file is added by the server script via AIO.AddAddon()
-- On server side, stop execution here
if AIO.AddAddon() then
    return
end

-- ============================================================================
-- CLIENT-SIDE CODE BELOW
-- ============================================================================

-- Saved variables for UI position
AIO.AddSavedVarChar("DChallenge_UIPos")
DChallenge_UIPos = DChallenge_UIPos or {}

-- ============================================================================
-- Local State
-- ============================================================================

local dungeonData = {}
local affixData = {}
local config = {}
local selectedDungeon = nil    -- index into dungeonData
local selectedDifficulty = nil
local currentTab = "dungeons"

-- ============================================================================
-- Color Helpers
-- ============================================================================

local function DifficultyColor(diff)
    if diff <= 5 then
        return 0.0, 1.0, 0.0
    elseif diff <= 10 then
        return 1.0, 1.0, 0.0
    elseif diff <= 15 then
        return 1.0, 0.5, 0.0
    else
        return 1.0, 0.0, 0.0
    end
end

local function DifficultyColorHex(diff)
    if diff <= 5 then return "|cff00ff00"
    elseif diff <= 10 then return "|cffffff00"
    elseif diff <= 15 then return "|cffff8000"
    else return "|cffff0000"
    end
end

local function FormatTime(seconds)
    local m = math.floor(seconds / 60)
    local s = seconds % 60
    return string.format("%d:%02d", m, s)
end

-- ============================================================================
-- Calculation Helpers
-- ============================================================================

local function GetHPMult(diff)
    return 1.0 + ((config.hpMultPerLevel or 0.15) * diff)
end

local function GetDMGMult(diff)
    return 1.0 + ((config.dmgMultPerLevel or 0.08) * diff)
end

local function GetAffixesForDifficulty(diff)
    local result = {}
    for _, a in ipairs(affixData) do
        if diff >= a.minDiff then
            table.insert(result, a)
        end
    end
    return result
end

-- ============================================================================
-- Main Frame
-- ============================================================================

local MainFrame = CreateFrame("Frame", "DungeonChallengeFrame", UIParent)
MainFrame:SetSize(520, 450)
MainFrame:SetPoint("CENTER", 0, 50)
MainFrame:SetMovable(true)
MainFrame:EnableMouse(true)
MainFrame:RegisterForDrag("LeftButton")
MainFrame:SetScript("OnDragStart", MainFrame.StartMoving)
MainFrame:SetScript("OnDragStop", MainFrame.StopMovingOrSizing)
MainFrame:SetBackdrop({
    bgFile = "Interface/Tooltips/UI-Tooltip-Background",
    edgeFile = "Interface/Tooltips/UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 }
})
MainFrame:SetBackdropColor(0.05, 0.05, 0.1, 0.95)
MainFrame:SetBackdropBorderColor(0.4, 0.4, 0.8, 0.8)
MainFrame:SetFrameStrata("DIALOG")
MainFrame:Hide()

AIO.SavePosition(MainFrame, true)

-- Close with Escape
table.insert(UISpecialFrames, "DungeonChallengeFrame")

-- ============================================================================
-- Title Bar
-- ============================================================================

local TitleBg = MainFrame:CreateTexture(nil, "ARTWORK")
TitleBg:SetPoint("TOPLEFT", 6, -6)
TitleBg:SetPoint("TOPRIGHT", -6, -6)
TitleBg:SetHeight(28)
TitleBg:SetTexture(0.1, 0.1, 0.25, 0.9)

local TitleIcon = MainFrame:CreateTexture(nil, "OVERLAY")
TitleIcon:SetSize(24, 24)
TitleIcon:SetPoint("TOPLEFT", 10, -8)
TitleIcon:SetTexture("Interface/Icons/Achievement_Dungeon_ClassicDungeonMaster")

local TitleText = MainFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
TitleText:SetPoint("LEFT", TitleIcon, "RIGHT", 6, 0)
TitleText:SetText("|cffFFD700Dungeon Challenge|r")

-- Close Button
local CloseBtn = CreateFrame("Button", nil, MainFrame, "UIPanelCloseButton")
CloseBtn:SetPoint("TOPRIGHT", -2, -2)
CloseBtn:SetScript("OnClick", function() MainFrame:Hide() end)

-- ============================================================================
-- Tab Buttons
-- ============================================================================

local TAB_NAMES = {
    { key = "dungeons",    label = "Dungeons" },
    { key = "leaderboard", label = "Leaderboard" },
    { key = "myruns",      label = "My Runs" },
    { key = "records",     label = "Records" },
}

local tabButtons = {}

local function CreateTabButton(parent, index, tabInfo)
    local btn = CreateFrame("Button", "DCTab_" .. tabInfo.key, parent)
    btn:SetSize(120, 24)
    btn:SetPoint("TOPLEFT", 8 + (index - 1) * 126, -38)

    local bg = btn:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints()
    bg:SetTexture(0.15, 0.15, 0.3, 0.8)
    btn.bg = bg

    local highlight = btn:CreateTexture(nil, "HIGHLIGHT")
    highlight:SetAllPoints()
    highlight:SetTexture(0.3, 0.3, 0.6, 0.4)

    local text = btn:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    text:SetPoint("CENTER", 0, 0)
    text:SetText(tabInfo.label)
    btn.text = text

    btn:SetScript("OnClick", function()
        currentTab = tabInfo.key
        for _, tb in ipairs(tabButtons) do
            tb.bg:SetTexture(0.15, 0.15, 0.3, 0.8)
        end
        bg:SetTexture(0.25, 0.25, 0.5, 1.0)

        if tabInfo.key == "dungeons" then
            ShowDungeonPanel()
        elseif tabInfo.key == "leaderboard" then
            ShowLeaderboardPanel()
        elseif tabInfo.key == "myruns" then
            AIO.Msg():Add("DungeonChallenge", "RequestMyRuns"):Send()
        elseif tabInfo.key == "records" then
            ShowRecordsPanel()
        end
    end)

    return btn
end

for i, t in ipairs(TAB_NAMES) do
    tabButtons[i] = CreateTabButton(MainFrame, i, t)
end

-- ============================================================================
-- Content Area (scrollable)
-- ============================================================================

local ContentFrame = CreateFrame("Frame", nil, MainFrame)
ContentFrame:SetPoint("TOPLEFT", 8, -66)
ContentFrame:SetPoint("BOTTOMRIGHT", -8, 8)
ContentFrame:SetBackdrop({
    bgFile = "Interface/Tooltips/UI-Tooltip-Background",
    edgeFile = "Interface/Tooltips/UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 }
})
ContentFrame:SetBackdropColor(0.02, 0.02, 0.05, 0.9)
ContentFrame:SetBackdropBorderColor(0.3, 0.3, 0.5, 0.6)

local ScrollFrame = CreateFrame("ScrollFrame", "DCScrollFrame", ContentFrame,
    "UIPanelScrollFrameTemplate")
ScrollFrame:SetPoint("TOPLEFT", 4, -4)
ScrollFrame:SetPoint("BOTTOMRIGHT", -26, 4)

local ScrollChild = CreateFrame("Frame", nil, ScrollFrame)
ScrollChild:SetSize(1, 1)
ScrollFrame:SetScrollChild(ScrollChild)

-- ============================================================================
-- Dynamic Content Builder
-- Instead of SetParent(nil) (which crashes for FontStrings/Textures in 3.3.5),
-- we destroy and recreate the ScrollChild each time content changes.
-- ============================================================================

local function ClearContent()
    -- Hide old scroll child (WoW will garbage collect orphaned children)
    ScrollChild:Hide()
    ScrollChild:SetParent(nil)

    -- Create fresh scroll child
    ScrollChild = CreateFrame("Frame", nil, ScrollFrame)
    ScrollChild:SetSize(1, 1)
    ScrollFrame:SetScrollChild(ScrollChild)
    ScrollFrame:SetVerticalScroll(0)
end

local function AddLabel(yOffset, text, fontTemplate, r, g, b)
    fontTemplate = fontTemplate or "GameFontNormal"
    local label = ScrollChild:CreateFontString(nil, "OVERLAY", fontTemplate)
    label:SetPoint("TOPLEFT", 8, yOffset)
    label:SetPoint("TOPRIGHT", -8, yOffset)
    label:SetJustifyH("LEFT")
    label:SetText(text)
    if r and g and b then
        label:SetTextColor(r, g, b)
    end
    label:Show()
    return label
end

local function AddButton(yOffset, width, height, text, onClick)
    local btn = CreateFrame("Button", nil, ScrollChild, "UIPanelButtonTemplate")
    btn:SetSize(width, height)
    btn:SetPoint("TOPLEFT", 8, yOffset)
    btn:SetText(text)
    btn:SetScript("OnClick", onClick)
    btn:Show()
    return btn
end

local function AddDivider(yOffset)
    local div = ScrollChild:CreateTexture(nil, "ARTWORK")
    div:SetPoint("TOPLEFT", 4, yOffset)
    div:SetPoint("TOPRIGHT", -4, yOffset)
    div:SetHeight(1)
    div:SetTexture(0.4, 0.4, 0.6, 0.5)
    div:Show()
    return div
end

-- ============================================================================
-- Panel: Dungeon Selection
-- ============================================================================

function ShowDungeonPanel()
    ClearContent()
    selectedDungeon = nil
    selectedDifficulty = nil

    local y = -8
    AddLabel(y, "|cffFFD700Select a Dungeon:|r", "GameFontNormalLarge")
    y = y - 24

    if not dungeonData or #dungeonData == 0 then
        AddLabel(y, "|cffff0000No dungeons available.|r")
        y = y - 18
        AddLabel(y, "|cffaaaaaaWaiting for server data... Try /reload|r")
        ScrollChild:SetHeight(math.abs(y) + 30)
        return
    end

    for i, d in ipairs(dungeonData) do
        AddDivider(y - 2)
        y = y - 6

        local btn = AddButton(y, 460, 28, d.name, function()
            selectedDungeon = i
            ShowDifficultyPanel()
        end)

        local info = ScrollChild:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        info:SetPoint("TOPLEFT", 8, y - 30)
        info:SetText(string.format(
            "    |cffaaaaaa Timer: %d min  |  Bosses: %d|r",
            d.timerMinutes or 30, d.bossCount or 0))
        info:Show()

        y = y - 50
    end

    ScrollChild:SetHeight(math.abs(y) + 10)
end

-- ============================================================================
-- Panel: Difficulty Selection
-- ============================================================================

function ShowDifficultyPanel()
    ClearContent()
    selectedDifficulty = nil

    local d = dungeonData[selectedDungeon]
    if not d then
        ShowDungeonPanel()
        return
    end

    local y = -8
    AddLabel(y, string.format("|cffFFD700%s|r - Select Difficulty:", d.name),
        "GameFontNormalLarge")
    y = y - 22

    AddButton(y, 100, 22, "<< Back", function()
        ShowDungeonPanel()
    end)
    y = y - 30

    AddDivider(y)
    y = y - 8

    local maxDiff = config.maxDifficulty or 20

    for diff = 1, maxDiff do
        local hpMult = GetHPMult(diff)
        local dmgMult = GetDMGMult(diff)
        local affixes = GetAffixesForDifficulty(diff)
        local r, g, b = DifficultyColor(diff)

        local btn = AddButton(y, 90, 24, string.format("Level %d", diff), function()
            selectedDifficulty = diff
            ShowConfirmPanel()
        end)

        local statsText = ScrollChild:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        statsText:SetPoint("LEFT", btn, "RIGHT", 10, 0)
        statsText:SetText(string.format(
            "|cffaaaaaaHP: |cff%02x%02x00x%.1f|r  |cffaaaaaaDMG: |cff%02x%02x00x%.1f|r  |cffaaaaaaAffixes: %d|r",
            math.floor(r*255), math.floor(g*255), hpMult,
            math.floor(r*255), math.floor(g*255), dmgMult,
            #affixes))
        statsText:Show()

        y = y - 28
    end

    ScrollChild:SetHeight(math.abs(y) + 10)
end

-- ============================================================================
-- Panel: Confirm Challenge
-- ============================================================================

function ShowConfirmPanel()
    ClearContent()

    local d = dungeonData[selectedDungeon]
    if not d or not selectedDifficulty then
        ShowDungeonPanel()
        return
    end

    local diff = selectedDifficulty
    local hpMult = GetHPMult(diff)
    local dmgMult = GetDMGMult(diff)
    local affixes = GetAffixesForDifficulty(diff)

    local y = -8

    AddLabel(y, "|cffFFD700Confirm Challenge|r", "GameFontNormalLarge")
    y = y - 30
    AddDivider(y)
    y = y - 14

    AddLabel(y, string.format("Dungeon: |cffff8000%s|r", d.name))
    y = y - 18
    AddLabel(y, string.format("Level: %s%d|r", DifficultyColorHex(diff), diff))
    y = y - 18
    AddLabel(y, string.format("Bosses: |cffffff00%d|r", d.bossCount or 0))
    y = y - 18
    AddLabel(y, string.format("Timer: |cffffff00%d minutes|r", d.timerMinutes or 30))
    y = y - 22
    AddDivider(y)
    y = y - 14

    AddLabel(y, "|cffFFD700Scaling:|r")
    y = y - 18
    AddLabel(y, string.format("  HP Multiplier: |cffff0000x%.2f|r", hpMult))
    y = y - 16
    AddLabel(y, string.format("  DMG Multiplier: |cffff0000x%.2f|r", dmgMult))
    y = y - 16
    AddLabel(y, string.format("  Death Penalty: |cffff0000+%ds per death|r",
        config.deathPenaltySeconds or 15))
    y = y - 22
    AddDivider(y)
    y = y - 14

    AddLabel(y, string.format("|cffFFD700Active Affixes (%d):|r", #affixes))
    y = y - 18

    if #affixes == 0 then
        AddLabel(y, "  |cff00ff00None at this level.|r")
        y = y - 16
    else
        for _, a in ipairs(affixes) do
            AddLabel(y, string.format("  |cffff8000%s|r - |cffaaaaaa%s|r",
                a.name, a.desc or ""))
            y = y - 16
        end
    end

    y = y - 10
    AddLabel(y, string.format(
        "|cffaaaaaa~%d%% of dungeon mobs will receive random affixes!|r",
        config.affixPercentage or 5))
    y = y - 24
    AddDivider(y)
    y = y - 14

    -- Start button
    local startBtn = CreateFrame("Button", nil, ScrollChild, "UIPanelButtonTemplate")
    startBtn:SetSize(200, 30)
    startBtn:SetPoint("TOP", ScrollChild, "TOP", 0, y)
    startBtn:SetText("|cff00ff00START CHALLENGE!|r")
    startBtn:SetScript("OnClick", function()
        AIO.Msg()
            :Add("DungeonChallenge", "StartChallenge", d.mapId, diff)
            :Send()
        MainFrame:Hide()
    end)
    startBtn:Show()
    y = y - 36

    AddButton(y, 180, 24, "<< Change Difficulty", function()
        ShowDifficultyPanel()
    end)
    y = y - 28

    AddButton(y, 180, 24, "<< Change Dungeon", function()
        ShowDungeonPanel()
    end)
    y = y - 28

    ScrollChild:SetHeight(math.abs(y) + 10)
end

-- ============================================================================
-- Panel: Leaderboard
-- ============================================================================

function ShowLeaderboardPanel()
    ClearContent()

    local y = -8
    AddLabel(y, "|cffFFD700Leaderboard - Select Dungeon:|r", "GameFontNormalLarge")
    y = y - 28

    if not dungeonData or #dungeonData == 0 then
        AddLabel(y, "|cffaaaaaaNo dungeons loaded.|r")
        ScrollChild:SetHeight(math.abs(y) + 30)
        return
    end

    for i, d in ipairs(dungeonData) do
        AddButton(y, 400, 26, d.name, function()
            AIO.Msg()
                :Add("DungeonChallenge", "RequestLeaderboard", d.mapId)
                :Send()
        end)
        y = y - 30
    end

    ScrollChild:SetHeight(math.abs(y) + 10)
end

function ShowLeaderboardData(mapId, entries)
    ClearContent()

    local dungeonName = "Unknown"
    for _, d in ipairs(dungeonData) do
        if d.mapId == mapId then
            dungeonName = d.name
            break
        end
    end

    local y = -8
    AddLabel(y, string.format("|cffFFD700Leaderboard: %s|r", dungeonName),
        "GameFontNormalLarge")
    y = y - 22

    AddButton(y, 100, 22, "<< Back", function()
        ShowLeaderboardPanel()
    end)
    y = y - 30
    AddDivider(y)
    y = y - 10

    if not entries or #entries == 0 then
        AddLabel(y, "|cffaaaaaaNo entries yet.|r")
        y = y - 20
    else
        AddLabel(y, "|cffFFD700 #   Level   Time        Player          Deaths|r",
            "GameFontHighlightSmall")
        y = y - 16

        for rank, e in ipairs(entries) do
            local timeStr = FormatTime(e.time or 0)
            local line = string.format(
                " |cffffcc00#%-3d|r  %s%d|r     |cff00ff00%-10s|r  |cff69ccf0%-15s|r  %d",
                rank, DifficultyColorHex(e.difficulty or 1), e.difficulty or 1,
                timeStr, e.leader or "?", e.deaths or 0)
            AddLabel(y, line, "GameFontHighlightSmall")
            y = y - 15
        end
    end

    ScrollChild:SetHeight(math.abs(y) + 10)
end

-- ============================================================================
-- Panel: My Runs
-- ============================================================================

function ShowMyRunsData(entries)
    ClearContent()

    local y = -8
    AddLabel(y, "|cffFFD700My Best Runs:|r", "GameFontNormalLarge")
    y = y - 28

    if not entries or #entries == 0 then
        AddLabel(y, "|cffaaaaaaNo completed challenges yet.|r")
        y = y - 20
    else
        for _, e in ipairs(entries) do
            local dungeonName = "Unknown"
            for _, d in ipairs(dungeonData) do
                if d.mapId == e.mapId then
                    dungeonName = d.name
                    break
                end
            end

            local timeStr = FormatTime(e.time or 0)
            local line = string.format(
                " |cffff8000%-20s|r  %s%d|r     |cff00ff00%-10s|r  %d deaths",
                dungeonName, DifficultyColorHex(e.difficulty or 1),
                e.difficulty or 1, timeStr, e.deaths or 0)
            AddLabel(y, line, "GameFontHighlightSmall")
            y = y - 15
        end
    end

    ScrollChild:SetHeight(math.abs(y) + 10)
end

-- ============================================================================
-- Panel: Records (Boss Kill Snapshots)
-- ============================================================================

function ShowRecordsPanel()
    ClearContent()

    local y = -8
    AddLabel(y, "|cffFFD700Boss Kill Records - Select Dungeon:|r",
        "GameFontNormalLarge")
    y = y - 28

    if not dungeonData or #dungeonData == 0 then
        AddLabel(y, "|cffaaaaaaNo dungeons loaded.|r")
        ScrollChild:SetHeight(math.abs(y) + 30)
        return
    end

    for i, d in ipairs(dungeonData) do
        AddButton(y, 400, 26, d.name, function()
            AIO.Msg()
                :Add("DungeonChallenge", "RequestSnapshots", d.mapId)
                :Send()
        end)
        y = y - 30
    end

    ScrollChild:SetHeight(math.abs(y) + 10)
end

function ShowSnapshotData(mapId, entries)
    ClearContent()

    local dungeonName = "Unknown"
    for _, d in ipairs(dungeonData) do
        if d.mapId == mapId then
            dungeonName = d.name
            break
        end
    end

    local y = -8
    AddLabel(y, string.format("|cffFFD700Boss Records: %s|r", dungeonName),
        "GameFontNormalLarge")
    y = y - 22

    AddButton(y, 100, 22, "<< Back", function()
        ShowRecordsPanel()
    end)
    y = y - 30
    AddDivider(y)
    y = y - 10

    if not entries or #entries == 0 then
        AddLabel(y, "|cffaaaaaaNo records yet.|r")
        y = y - 20
    else
        for _, e in ipairs(entries) do
            local timeStr = FormatTime(e.snapTime or 0)
            local finalTag = ""
            if e.isFinal then
                finalTag = " |cff00ff00[FINAL]|r"
            end
            local rewardTag = ""
            if e.rewarded then
                rewardTag = " |cffffcc00[REWARDED]|r"
            end

            local line = string.format(
                " %sLv%d|r  |cffff8000%s|r  at |cff00ff00%s|r  |cffaaaaaa%d deaths (+%ds)|r  |cff69ccf0%s|r%s%s",
                DifficultyColorHex(e.difficulty or 1), e.difficulty or 1,
                e.bossName or "?", timeStr, e.deaths or 0, e.penalty or 0,
                e.playerName or "?", finalTag, rewardTag)
            AddLabel(y, line, "GameFontHighlightSmall")
            y = y - 16
        end
    end

    ScrollChild:SetHeight(math.abs(y) + 10)
end

-- ============================================================================
-- AIO Client Handlers (called from server)
-- ============================================================================

local ClientHandlers = {}

-- Receive config via AIO init message (small payload)
ClientHandlers.InitConfig = function(cfg)
    config = cfg or {}
end

-- Begin receiving dungeon/affix data (sent individually after login)
ClientHandlers.InitBegin = function(dungeonCount, affixCount)
    dungeonData = {}
    affixData = {}
end

-- Receive a single dungeon (flat parameters, no nested tables)
ClientHandlers.InitDungeon = function(mapId, name, timerMinutes, bossCount)
    table.insert(dungeonData, {
        mapId        = mapId,
        name         = name,
        timerMinutes = timerMinutes,
        bossCount    = bossCount,
    })
end

-- Receive a single affix (flat parameters)
ClientHandlers.InitAffix = function(id, name, desc, minDiff)
    table.insert(affixData, {
        id      = id,
        name    = name,
        desc    = desc,
        minDiff = minDiff,
    })
end

-- All init data received
ClientHandlers.InitComplete = function()
    DEFAULT_CHAT_FRAME:AddMessage(string.format(
        "|cff00ff00[Dungeon Challenge]|r Loaded %d dungeons, %d affixes.",
        #dungeonData, #affixData))
end

-- Server tells us to show the UI
ClientHandlers.ShowUI = function()
    if MainFrame:IsShown() then
        MainFrame:Hide()
        return
    end

    currentTab = "dungeons"
    for i, tb in ipairs(tabButtons) do
        if i == 1 then
            tb.bg:SetTexture(0.25, 0.25, 0.5, 1.0)
        else
            tb.bg:SetTexture(0.15, 0.15, 0.3, 0.8)
        end
    end
    ShowDungeonPanel()
    MainFrame:Show()
end

-- Receive leaderboard data
ClientHandlers.LeaderboardData = function(mapId, entries)
    ShowLeaderboardData(mapId, entries or {})
end

-- Receive personal runs data
ClientHandlers.MyRunsData = function(entries)
    ShowMyRunsData(entries or {})
end

-- Receive snapshot data
ClientHandlers.SnapshotData = function(mapId, entries)
    ShowSnapshotData(mapId, entries or {})
end

-- Challenge started notification
ClientHandlers.ChallengeStarted = function(dungeonName, difficulty, starterName)
    DEFAULT_CHAT_FRAME:AddMessage(string.format(
        "|cff00ff00[Dungeon Challenge]|r |cffff8000%s|r started: "
        .. "|cff00ff00%s|r at Level |cffff8000%d|r!",
        starterName or "?", dungeonName or "?", difficulty or 0))
    MainFrame:Hide()
end

-- Error from server
ClientHandlers.Error = function(errorMsg)
    DEFAULT_CHAT_FRAME:AddMessage(
        "|cffff0000[Dungeon Challenge]|r " .. (errorMsg or "Unknown error"))
end

AIO.AddHandlers("DungeonChallenge", ClientHandlers)

-- ============================================================================
-- Slash Command to toggle UI
-- ============================================================================

SLASH_DUNGEONCHALLENGE1 = "/dc"
SLASH_DUNGEONCHALLENGE2 = "/dungeonchallenge"
SlashCmdList["DUNGEONCHALLENGE"] = function(msg)
    if MainFrame:IsShown() then
        MainFrame:Hide()
    else
        currentTab = "dungeons"
        for i, tb in ipairs(tabButtons) do
            if i == 1 then
                tb.bg:SetTexture(0.25, 0.25, 0.5, 1.0)
            else
                tb.bg:SetTexture(0.15, 0.15, 0.3, 0.8)
            end
        end
        ShowDungeonPanel()
        MainFrame:Show()
    end
end

DEFAULT_CHAT_FRAME:AddMessage(
    "|cff00ff00[Dungeon Challenge]|r UI loaded. Use |cffFFD700/dc|r to toggle.")
