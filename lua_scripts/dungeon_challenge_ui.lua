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
    if diff <= 20 then
        return 0.0, 1.0, 0.0
    elseif diff <= 40 then
        return 1.0, 1.0, 0.0
    elseif diff <= 60 then
        return 1.0, 0.5, 0.0
    elseif diff <= 80 then
        return 1.0, 0.2, 0.0
    else
        return 1.0, 0.0, 0.0
    end
end

local function DifficultyColorHex(diff)
    if diff <= 20 then return "|cff00ff00"
    elseif diff <= 40 then return "|cffffff00"
    elseif diff <= 60 then return "|cffff8000"
    elseif diff <= 80 then return "|cffff3300"
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

local function AffixSpellLink(a)
    if a.spellId then
        return string.format("|cffff8000|Hspell:%d|h[%s]|h|r", a.spellId, a.name)
    end
    return "|cffff8000" .. a.name .. "|r"
end

-- ============================================================================
-- Main Frame
-- ============================================================================

local MainFrame = CreateFrame("Frame", "DungeonChallengeFrame", UIParent)
MainFrame:SetSize(676, 585)
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
    btn:SetSize(156, 28)
    btn:SetPoint("TOPLEFT", 8 + (index - 1) * 163, -38)

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
-- Content Area (scrollable, no XML template — works reliably with AIO)
-- ============================================================================

local ContentFrame = CreateFrame("Frame", nil, MainFrame)
ContentFrame:SetPoint("TOPLEFT", 8, -72)
ContentFrame:SetPoint("BOTTOMRIGHT", -8, 8)
ContentFrame:SetBackdrop({
    bgFile = "Interface/Tooltips/UI-Tooltip-Background",
    edgeFile = "Interface/Tooltips/UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 }
})
ContentFrame:SetBackdropColor(0.02, 0.02, 0.05, 0.9)
ContentFrame:SetBackdropBorderColor(0.3, 0.3, 0.5, 0.6)

-- Manual ScrollFrame (no UIPanelScrollFrameTemplate — avoids AIO/template issues)
local ScrollFrame = CreateFrame("ScrollFrame", nil, ContentFrame)
ScrollFrame:SetPoint("TOPLEFT", 4, -4)
ScrollFrame:SetPoint("BOTTOMRIGHT", -4, 4)

local SCROLL_CONTENT_WIDTH = 636

local ScrollChild = CreateFrame("Frame", nil)
ScrollChild:SetWidth(SCROLL_CONTENT_WIDTH)
ScrollChild:SetHeight(1)
ScrollFrame:SetScrollChild(ScrollChild)

-- Mouse wheel scrolling
ScrollFrame:EnableMouseWheel(true)
ScrollFrame:SetScript("OnMouseWheel", function(self, delta)
    local current = self:GetVerticalScroll()
    local maxScroll = max(0, ScrollChild:GetHeight() - self:GetHeight())
    local newScroll = current - (delta * 40)
    if newScroll < 0 then newScroll = 0 end
    if newScroll > maxScroll then newScroll = maxScroll end
    self:SetVerticalScroll(newScroll)
end)

-- ============================================================================
-- Dynamic Content Builder
-- We keep the same ScrollChild and just hide old elements on clear.
-- Hidden FontStrings/Textures stay attached but invisible (no SetParent needed).
-- ============================================================================

local contentElements = {}

local function ClearContent()
    for _, elem in ipairs(contentElements) do
        elem:Hide()
        elem:ClearAllPoints()
    end
    contentElements = {}
    ScrollChild:SetHeight(1)
    ScrollFrame:SetVerticalScroll(0)
end

local function AddLabel(yOffset, text, fontTemplate, r, g, b)
    fontTemplate = fontTemplate or "GameFontNormal"
    local label = ScrollChild:CreateFontString(nil, "OVERLAY", fontTemplate)
    label:SetPoint("TOPLEFT", 8, yOffset)
    label:SetWidth(SCROLL_CONTENT_WIDTH - 16)
    label:SetJustifyH("LEFT")
    label:SetText(text)
    if r and g and b then
        label:SetTextColor(r, g, b)
    end
    label:Show()
    table.insert(contentElements, label)
    return label
end

local function AddButton(yOffset, width, height, text, onClick)
    local btn = CreateFrame("Button", nil, ScrollChild, "UIPanelButtonTemplate")
    btn:SetSize(width, height)
    btn:SetPoint("TOPLEFT", 8, yOffset)
    btn:SetText(text)
    btn:SetScript("OnClick", onClick)
    btn:Show()
    table.insert(contentElements, btn)
    return btn
end

local function AddDivider(yOffset)
    local div = ScrollChild:CreateTexture(nil, "ARTWORK")
    div:SetPoint("TOPLEFT", 4, yOffset)
    div:SetWidth(SCROLL_CONTENT_WIDTH - 8)
    div:SetHeight(1)
    div:SetTexture(0.4, 0.4, 0.6, 0.5)
    div:Show()
    table.insert(contentElements, div)
    return div
end

-- ============================================================================
-- Panel: Dungeon Selection (simple clickable text list)
-- ============================================================================

local function AddClickableText(yOffset, text, onClick)
    local btn = CreateFrame("Button", nil, ScrollChild)
    btn:SetSize(SCROLL_CONTENT_WIDTH - 16, 20)
    btn:SetPoint("TOPLEFT", 8, yOffset)

    local label = btn:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    label:SetPoint("LEFT", 0, 0)
    label:SetText(text)
    label:SetJustifyH("LEFT")
    btn.label = label

    local highlight = btn:CreateTexture(nil, "HIGHLIGHT")
    highlight:SetAllPoints()
    highlight:SetTexture(0.3, 0.3, 0.6, 0.3)

    btn:SetScript("OnClick", onClick)
    btn:SetScript("OnEnter", function(self)
        self.label:SetTextColor(1.0, 0.82, 0.0)
    end)
    btn:SetScript("OnLeave", function(self)
        self.label:SetTextColor(1.0, 1.0, 1.0)
    end)
    btn:Show()
    table.insert(contentElements, btn)
    return btn
end

function ShowDungeonPanel()
    ClearContent()
    selectedDungeon = nil
    selectedDifficulty = nil

    DEFAULT_CHAT_FRAME:AddMessage(string.format(
        "|cff00ff00[DC Debug]|r ShowDungeonPanel called, dungeonData count: %d",
        dungeonData and #dungeonData or -1))

    -- Title
    local title = ScrollChild:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", 8, -8)
    title:SetText("|cffFFD700Select a Dungeon:|r")
    title:Show()
    table.insert(contentElements, title)

    if not dungeonData or #dungeonData == 0 then
        local y = -32
        AddLabel(y, "|cffff0000No dungeons available.|r")
        y = y - 18
        AddLabel(y, "|cffaaaaaaWaiting for server data... Try /reload|r")
        ScrollChild:SetHeight(math.abs(y) + 30)
        return
    end

    -- Tile grid layout — calculate columns/rows to fit the content area
    local count = #dungeonData
    local CONTENT_W = SCROLL_CONTENT_WIDTH - 16  -- usable width (padding)
    local CONTENT_H = ContentFrame:GetHeight() - 50  -- subtract title + padding
    local TILE_PAD = 6

    -- Determine grid dimensions: try 3 columns first, then 4, then 2
    local cols = 3
    if count <= 4 then
        cols = 2
    elseif count > 12 then
        cols = 4
    end
    local rows = math.ceil(count / cols)

    local tileW = math.floor((CONTENT_W - (cols - 1) * TILE_PAD) / cols)
    local tileH = math.floor((CONTENT_H - (rows - 1) * TILE_PAD) / rows)
    -- Clamp tile height
    if tileH > 90 then tileH = 90 end
    if tileH < 40 then tileH = 40 end

    local startY = -34
    local startX = 8

    for i, d in ipairs(dungeonData) do
        local col = (i - 1) % cols
        local row = math.floor((i - 1) / cols)

        local x = startX + col * (tileW + TILE_PAD)
        local y = startY - row * (tileH + TILE_PAD)

        local tile = CreateFrame("Button", nil, ScrollChild)
        tile:SetSize(tileW, tileH)
        tile:SetPoint("TOPLEFT", x, y)

        -- Background
        local bg = tile:CreateTexture(nil, "BACKGROUND")
        bg:SetAllPoints()
        bg:SetTexture(0.12, 0.12, 0.25, 0.8)

        -- Border
        tile:SetBackdrop({
            bgFile = "Interface/Tooltips/UI-Tooltip-Background",
            edgeFile = "Interface/Tooltips/UI-Tooltip-Border",
            tile = true, tileSize = 8, edgeSize = 10,
            insets = { left = 2, right = 2, top = 2, bottom = 2 }
        })
        tile:SetBackdropColor(0.12, 0.12, 0.25, 0.8)
        tile:SetBackdropBorderColor(0.4, 0.4, 0.7, 0.6)

        -- Highlight
        local hl = tile:CreateTexture(nil, "HIGHLIGHT")
        hl:SetAllPoints()
        hl:SetTexture(0.3, 0.3, 0.6, 0.3)

        -- Dungeon name
        local nameText = tile:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        nameText:SetPoint("TOP", 0, -8)
        nameText:SetWidth(tileW - 10)
        nameText:SetText("|cffffffff" .. (d.name or "Unknown") .. "|r")

        -- Info line
        local infoText = tile:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        infoText:SetPoint("BOTTOM", 0, 8)
        infoText:SetText(string.format("|cffaaaaaa%d bosses | %d min|r",
            d.bossCount or 0, d.timerMinutes or 30))

        tile:SetScript("OnClick", function()
            selectedDungeon = i
            ShowDifficultyPanel()
        end)
        tile:SetScript("OnEnter", function(self)
            self:SetBackdropBorderColor(1.0, 0.82, 0.0, 1.0)
        end)
        tile:SetScript("OnLeave", function(self)
            self:SetBackdropBorderColor(0.4, 0.4, 0.7, 0.6)
        end)

        tile:Show()
        table.insert(contentElements, tile)
    end

    local totalRows = math.ceil(count / cols)
    ScrollChild:SetHeight(math.abs(startY) + totalRows * (tileH + TILE_PAD) + 10)
end

-- ============================================================================
-- Panel: Difficulty Selection
-- ============================================================================

function ShowDifficultyPanel()
    ClearContent()

    local d = dungeonData[selectedDungeon]
    if not d then
        ShowDungeonPanel()
        return
    end

    local maxDiff = config.maxDifficulty or 20
    if not selectedDifficulty then
        selectedDifficulty = 1
    end

    local y = -8
    AddLabel(y, string.format("|cffFFD700%s|r - Select Difficulty:", d.name),
        "GameFontNormalLarge")
    y = y - 22

    AddLabel(y, string.format(
        "|cffaaaaaa Bosses: %d  |  Timer: %d min|r",
        d.bossCount or 0, d.timerMinutes or 30), "GameFontHighlightSmall")
    y = y - 20

    AddClickableText(y, "|cff888888<< Back to Dungeons|r", function()
        ShowDungeonPanel()
    end)
    y = y - 30

    AddDivider(y)
    y = y - 16

    -- Difficulty Slider
    local slider = CreateFrame("Slider", "DCDifficultySlider", ScrollChild, "OptionsSliderTemplate")
    slider:SetWidth(SCROLL_CONTENT_WIDTH - 60)
    slider:SetHeight(17)
    slider:SetPoint("TOPLEFT", 30, y)
    slider:SetMinMaxValues(1, maxDiff)
    slider:SetValueStep(1)
    slider:SetValue(selectedDifficulty)
    getglobal("DCDifficultySliderLow"):SetText("|cff00ff001|r")
    getglobal("DCDifficultySliderHigh"):SetText(string.format("|cffff0000%d|r", maxDiff))
    getglobal("DCDifficultySliderText"):SetText("")
    slider:Show()
    table.insert(contentElements, slider)
    y = y - 36

    -- "Run Level: X" label with clickable number
    local levelLabel = ScrollChild:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    levelLabel:SetPoint("TOPLEFT", 30, y)
    levelLabel:SetText("Run Level: ")
    levelLabel:Show()
    table.insert(contentElements, levelLabel)

    -- Clickable level number
    local levelBtn = CreateFrame("Button", nil, ScrollChild)
    levelBtn:SetSize(60, 22)
    levelBtn:SetPoint("LEFT", levelLabel, "RIGHT", 2, 0)

    local levelBtnText = levelBtn:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    levelBtnText:SetPoint("LEFT", 0, 0)
    levelBtnText:SetJustifyH("LEFT")
    local r, g, b = DifficultyColor(selectedDifficulty)
    levelBtnText:SetTextColor(r, g, b)
    levelBtnText:SetText(tostring(selectedDifficulty))
    levelBtn.text = levelBtnText

    local levelBtnUnderline = levelBtn:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    levelBtnUnderline:SetPoint("TOPLEFT", levelBtnText, "BOTTOMLEFT", 0, 2)
    levelBtnUnderline:SetText("|cff888888(click to edit)|r")

    levelBtn:Show()
    table.insert(contentElements, levelBtn)

    -- EditBox (hidden by default, shown on click)
    local editBox = CreateFrame("EditBox", nil, ScrollChild)
    editBox:SetSize(50, 22)
    editBox:SetPoint("LEFT", levelLabel, "RIGHT", 2, 0)
    editBox:SetFontObject(GameFontNormalLarge)
    editBox:SetAutoFocus(false)
    editBox:SetNumeric(true)
    editBox:SetMaxLetters(3)
    editBox:SetBackdrop({
        bgFile = "Interface/Tooltips/UI-Tooltip-Background",
        edgeFile = "Interface/Tooltips/UI-Tooltip-Border",
        tile = true, tileSize = 8, edgeSize = 8,
        insets = { left = 2, right = 2, top = 2, bottom = 2 }
    })
    editBox:SetBackdropColor(0.1, 0.1, 0.2, 0.9)
    editBox:SetBackdropBorderColor(0.6, 0.6, 0.9, 0.8)
    editBox:Hide()
    table.insert(contentElements, editBox)

    -- Click level number → show edit box
    levelBtn:SetScript("OnClick", function()
        levelBtn:Hide()
        editBox:SetText(tostring(selectedDifficulty))
        editBox:Show()
        editBox:SetFocus()
        editBox:HighlightText()
    end)

    -- Function to apply the edit box value
    local function ApplyEditValue()
        local val = tonumber(editBox:GetText())
        if val then
            val = math.max(1, math.min(maxDiff, val))
            selectedDifficulty = val
            slider:SetValue(val)
        end
        editBox:Hide()
        levelBtn:Show()
        local cr, cg, cb = DifficultyColor(selectedDifficulty)
        levelBtnText:SetTextColor(cr, cg, cb)
        levelBtnText:SetText(tostring(selectedDifficulty))
        -- Refresh the stats/affixes below
        UpdateDifficultyDetails()
    end

    editBox:SetScript("OnEnterPressed", ApplyEditValue)
    editBox:SetScript("OnEscapePressed", function()
        editBox:Hide()
        levelBtn:Show()
    end)

    y = y - 32
    AddDivider(y)
    y = y - 14

    -- Stats & Affixes info area (updated dynamically)
    local statsLabel = ScrollChild:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    statsLabel:SetPoint("TOPLEFT", 8, y)
    statsLabel:SetWidth(SCROLL_CONTENT_WIDTH - 16)
    statsLabel:SetJustifyH("LEFT")
    statsLabel:Show()
    table.insert(contentElements, statsLabel)
    y = y - 18

    local affixLabel = ScrollChild:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    affixLabel:SetPoint("TOPLEFT", 8, y)
    affixLabel:SetWidth(SCROLL_CONTENT_WIDTH - 16)
    affixLabel:SetJustifyH("LEFT")
    affixLabel:Show()
    table.insert(contentElements, affixLabel)
    y = y - 24

    local affixDetailLabel = ScrollChild:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    affixDetailLabel:SetPoint("TOPLEFT", 16, y)
    affixDetailLabel:SetWidth(SCROLL_CONTENT_WIDTH - 32)
    affixDetailLabel:SetJustifyH("LEFT")
    affixDetailLabel:Show()
    table.insert(contentElements, affixDetailLabel)
    y = y - 40

    -- Confirm button — anchored to bottom of ContentFrame so it never overlaps text
    local confirmBtn = CreateFrame("Button", nil, ContentFrame, "UIPanelButtonTemplate")
    confirmBtn:SetSize(220, 30)
    confirmBtn:SetPoint("BOTTOM", ContentFrame, "BOTTOM", 0, 10)
    confirmBtn:SetText("|cff00ff00Continue >>|r")
    confirmBtn:SetScript("OnClick", function()
        ShowConfirmPanel()
    end)
    confirmBtn:Show()
    table.insert(contentElements, confirmBtn)

    ScrollChild:SetHeight(math.abs(y) + 10)

    -- Update function for stats/affixes display
    function UpdateDifficultyDetails()
        local diff = selectedDifficulty
        local hpMult = GetHPMult(diff)
        local dmgMult = GetDMGMult(diff)
        local affixes = GetAffixesForDifficulty(diff)
        local colorHex = DifficultyColorHex(diff)

        statsLabel:SetText(string.format(
            "%sLevel %d|r  —  |cffaaaaaaHP: x%.1f  |  DMG: x%.1f  |  Affixes: %d|r",
            colorHex, diff, hpMult, dmgMult, #affixes))

        if #affixes > 0 then
            local affixNames = {}
            for _, a in ipairs(affixes) do
                table.insert(affixNames, AffixSpellLink(a))
            end
            affixLabel:SetText("|cffFFD700Active Affixes:|r  " .. table.concat(affixNames, ", "))
            local details = {}
            for _, a in ipairs(affixes) do
                table.insert(details, string.format("  %s — %s", AffixSpellLink(a), a.desc or ""))
            end
            affixDetailLabel:SetText(table.concat(details, "\n"))
        else
            affixLabel:SetText("|cff00ff00No affixes at this level.|r")
            affixDetailLabel:SetText("")
        end
    end

    -- Slider change handler
    slider:SetScript("OnValueChanged", function(self, value)
        value = math.floor(value + 0.5)
        selectedDifficulty = value
        local cr, cg, cb = DifficultyColor(value)
        levelBtnText:SetTextColor(cr, cg, cb)
        levelBtnText:SetText(tostring(value))
        UpdateDifficultyDetails()
    end)

    -- Initial display
    UpdateDifficultyDetails()
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

    -- Navigation links — top-right, on same line as title
    local changeDiffBtn = CreateFrame("Button", nil, ScrollChild)
    changeDiffBtn:SetSize(130, 18)
    changeDiffBtn:SetPoint("TOPRIGHT", ScrollChild, "TOPRIGHT", -140, y)
    local changeDiffLabel = changeDiffBtn:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    changeDiffLabel:SetPoint("RIGHT", 0, 0)
    changeDiffLabel:SetText("|cff888888Change Difficulty|r")
    changeDiffBtn:SetScript("OnClick", function() ShowDifficultyPanel() end)
    changeDiffBtn:SetScript("OnEnter", function() changeDiffLabel:SetTextColor(1.0, 0.82, 0.0) end)
    changeDiffBtn:SetScript("OnLeave", function() changeDiffLabel:SetTextColor(0.53, 0.53, 0.53) end)
    changeDiffBtn:Show()
    table.insert(contentElements, changeDiffBtn)

    local changeDungBtn = CreateFrame("Button", nil, ScrollChild)
    changeDungBtn:SetSize(130, 18)
    changeDungBtn:SetPoint("TOPRIGHT", ScrollChild, "TOPRIGHT", -8, y)
    local changeDungLabel = changeDungBtn:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    changeDungLabel:SetPoint("RIGHT", 0, 0)
    changeDungLabel:SetText("|cff888888Change Dungeon|r")
    changeDungBtn:SetScript("OnClick", function() ShowDungeonPanel() end)
    changeDungBtn:SetScript("OnEnter", function() changeDungLabel:SetTextColor(1.0, 0.82, 0.0) end)
    changeDungBtn:SetScript("OnLeave", function() changeDungLabel:SetTextColor(0.53, 0.53, 0.53) end)
    changeDungBtn:Show()
    table.insert(contentElements, changeDungBtn)

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
            AddLabel(y, string.format("  %s - |cffaaaaaa%s|r",
                AffixSpellLink(a), a.desc or ""))
            y = y - 16
        end
    end

    y = y - 10
    AddLabel(y, string.format(
        "|cffaaaaaa~%d%% of dungeon mobs will receive ALL available affixes!|r",
        config.affixPercentage or 10))
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
    table.insert(contentElements, startBtn)
    y = y - 36

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
        AddClickableText(y, "|cffffffff" .. (d.name or "Unknown") .. "|r", function()
            AIO.Msg()
                :Add("DungeonChallenge", "RequestLeaderboard", d.mapId)
                :Send()
        end)
        y = y - 24
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

    AddClickableText(y, "|cff888888<< Back|r", function()
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
        AddClickableText(y, "|cffffffff" .. (d.name or "Unknown") .. "|r", function()
            AIO.Msg()
                :Add("DungeonChallenge", "RequestSnapshots", d.mapId)
                :Send()
        end)
        y = y - 24
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

    AddClickableText(y, "|cff888888<< Back|r", function()
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
-- Active Run Tracker Frame
-- ============================================================================

local activeRunData = nil
-- { dungeonName, difficulty, timerSeconds, totalBosses, affixString,
--   startClientTime, bossesKilled, deathCount, penaltyTime,
--   bossKills = { {name, time}, ... }, completed, completionTime, inTime }

AIO.AddSavedVarChar("DChallenge_TrackerPos")
DChallenge_TrackerPos = DChallenge_TrackerPos or {}

local TRACKER_W = 250
local TRK_PAD = 8
local TRK_INNER = TRACKER_W - 2 * TRK_PAD

local TrackerFrame = CreateFrame("Frame", "DCTrackerFrame", UIParent)
TrackerFrame:SetWidth(TRACKER_W)
TrackerFrame:SetHeight(180)
TrackerFrame:SetPoint("TOPRIGHT", UIParent, "TOPRIGHT", -20, -180)
TrackerFrame:SetMovable(true)
TrackerFrame:EnableMouse(true)
TrackerFrame:RegisterForDrag("LeftButton")
TrackerFrame:SetScript("OnDragStart", TrackerFrame.StartMoving)
TrackerFrame:SetScript("OnDragStop", TrackerFrame.StopMovingOrSizing)
TrackerFrame:SetBackdrop({
    bgFile = "Interface/Tooltips/UI-Tooltip-Background",
    edgeFile = "Interface/Tooltips/UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 }
})
TrackerFrame:SetBackdropColor(0.05, 0.05, 0.1, 0.92)
TrackerFrame:SetBackdropBorderColor(0.4, 0.4, 0.8, 0.8)
TrackerFrame:SetFrameStrata("HIGH")
TrackerFrame:Hide()

AIO.SavePosition(TrackerFrame, true)

-- Close button
local trkClose = CreateFrame("Button", nil, TrackerFrame, "UIPanelCloseButton")
trkClose:SetPoint("TOPRIGHT", 2, 2)
trkClose:SetScale(0.65)
trkClose:SetScript("OnClick", function() TrackerFrame:Hide() end)

-- Title: "+Level - DungeonName"
local trkTitle = TrackerFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
trkTitle:SetPoint("TOPLEFT", TRK_PAD, -TRK_PAD)
trkTitle:SetWidth(TRK_INNER - 16)
trkTitle:SetJustifyH("LEFT")

-- Affixes
local trkAffixes = TrackerFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
trkAffixes:SetPoint("TOPLEFT", trkTitle, "BOTTOMLEFT", 0, -1)
trkAffixes:SetWidth(TRK_INNER)
trkAffixes:SetJustifyH("LEFT")

-- Separator 1
local trkSep1 = TrackerFrame:CreateTexture(nil, "ARTWORK")
trkSep1:SetPoint("TOPLEFT", trkAffixes, "BOTTOMLEFT", -2, -3)
trkSep1:SetWidth(TRK_INNER + 4)
trkSep1:SetHeight(1)
trkSep1:SetTexture(0.4, 0.4, 0.6, 0.5)

-- Timer remaining (large)
local trkTimer = TrackerFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
trkTimer:SetPoint("TOPLEFT", trkSep1, "BOTTOMLEFT", 2, -4)
trkTimer:SetJustifyH("LEFT")

-- Timer detail (elapsed / total)
local trkTimerDetail = TrackerFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
trkTimerDetail:SetPoint("LEFT", trkTimer, "RIGHT", 6, 0)
trkTimerDetail:SetJustifyH("LEFT")

-- +2/+3 thresholds
local trkThresh = TrackerFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
trkThresh:SetPoint("TOPLEFT", trkTimer, "BOTTOMLEFT", 0, -2)
trkThresh:SetWidth(TRK_INNER)
trkThresh:SetJustifyH("LEFT")

-- Separator 2
local trkSep2 = TrackerFrame:CreateTexture(nil, "ARTWORK")
trkSep2:SetPoint("TOPLEFT", trkThresh, "BOTTOMLEFT", -2, -3)
trkSep2:SetWidth(TRK_INNER + 4)
trkSep2:SetHeight(1)
trkSep2:SetTexture(0.4, 0.4, 0.6, 0.5)

-- Boss list (multi-line)
local trkBosses = TrackerFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
trkBosses:SetPoint("TOPLEFT", trkSep2, "BOTTOMLEFT", 2, -3)
trkBosses:SetWidth(TRK_INNER)
trkBosses:SetJustifyH("LEFT")

-- Separator 3
local trkSep3 = TrackerFrame:CreateTexture(nil, "ARTWORK")
trkSep3:SetPoint("TOPLEFT", trkBosses, "BOTTOMLEFT", -2, -3)
trkSep3:SetWidth(TRK_INNER + 4)
trkSep3:SetHeight(1)
trkSep3:SetTexture(0.4, 0.4, 0.6, 0.5)

-- Deaths
local trkDeaths = TrackerFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
trkDeaths:SetPoint("TOPLEFT", trkSep3, "BOTTOMLEFT", 2, -3)
trkDeaths:SetWidth(TRK_INNER)
trkDeaths:SetJustifyH("LEFT")

-- Helper: format seconds as MM:SS (with floor)
local function FmtTime(sec)
    sec = math.max(0, math.floor(sec))
    return string.format("%d:%02d", math.floor(sec / 60), sec % 60)
end

-- Update tracker display (called from OnUpdate)
local function UpdateTrackerDisplay()
    if not activeRunData then return end

    local run = activeRunData
    local clientElapsed = GetTime() - run.startClientTime
    local effectiveElapsed = clientElapsed + run.penaltyTime
    local remaining = run.timerSeconds - effectiveElapsed

    -- If completed, freeze at completion time
    if run.completed then
        effectiveElapsed = run.completionTime
        remaining = run.timerSeconds - effectiveElapsed
    end

    -- Title
    local diffHex = DifficultyColorHex(run.difficulty)
    trkTitle:SetText(string.format("%s+%d|r - |cffffffff%s|r",
        diffHex, run.difficulty, run.dungeonName))

    -- Affixes
    trkAffixes:SetText("|cffff8000" .. run.affixString .. "|r")

    -- Timer color
    local timerColor
    if run.completed then
        timerColor = run.inTime and "|cff00ff00" or "|cffff0000"
    elseif remaining > run.timerSeconds * 0.4 then
        timerColor = "|cff00ff00"
    elseif remaining > run.timerSeconds * 0.2 then
        timerColor = "|cffffff00"
    elseif remaining > 0 then
        timerColor = "|cffff0000"
    else
        timerColor = "|cff888888"
    end

    -- Timer text
    if run.completed then
        local tag = run.inTime and "COMPLETED" or "OVER TIME"
        trkTimer:SetText(string.format("%s%s %s|r",
            timerColor, tag, FmtTime(effectiveElapsed)))
        trkTimerDetail:SetText("")
    else
        trkTimer:SetText(string.format("%s%s|r",
            timerColor, FmtTime(math.max(0, remaining))))
        trkTimerDetail:SetText(string.format("|cffaaaaaa(%s / %s)|r",
            FmtTime(effectiveElapsed), FmtTime(run.timerSeconds)))
    end

    -- +2/+3 thresholds (complete within 80%/60% of timer)
    local thresh2 = run.timerSeconds * 0.8
    local thresh3 = run.timerSeconds * 0.6
    local rem2 = thresh2 - effectiveElapsed
    local rem3 = thresh3 - effectiveElapsed
    local c2 = rem2 > 0 and "|cff00ff00" or "|cffff0000"
    local c3 = rem3 > 0 and "|cff00ff00" or "|cffff0000"
    trkThresh:SetText(string.format(
        "%s+2 (%s): %s|r  %s+3 (%s): %s|r",
        c2, FmtTime(thresh2), FmtTime(math.max(0, rem2)),
        c3, FmtTime(thresh3), FmtTime(math.max(0, rem3))))

    -- Boss progress
    local bossLines = {}
    for i = 1, run.totalBosses do
        local bk = run.bossKills[i]
        if bk then
            table.insert(bossLines, string.format(
                "|cff00ff00- 1/1 %s|r  |cffaaaaaa%s|r",
                bk.name, FmtTime(bk.time)))
        else
            table.insert(bossLines, string.format(
                "|cff666666- 0/1 Boss %d|r", i))
        end
    end
    trkBosses:SetText(table.concat(bossLines, "\n"))

    -- Deaths
    if run.deathCount > 0 then
        trkDeaths:SetText(string.format(
            "|cffff0000%d Deaths|r  |cffff4444-%s|r",
            run.deathCount, FmtTime(run.penaltyTime)))
    else
        trkDeaths:SetText("|cff00ff000 Deaths|r")
    end

    -- Adjust frame height dynamically
    -- Base: pad(8) + title(16) + affixes(12) + sep(7) + timer(18) + thresh(14) + sep(7) + sep(7) + deaths(14) + pad(8)
    local bossHeight = run.totalBosses * 14
    local height = 8 + 16 + 12 + 7 + 18 + 14 + 7 + bossHeight + 7 + 14 + 8
    TrackerFrame:SetHeight(height)
end

-- OnUpdate: tick tracker display
local trackerUpdateTimer = 0
TrackerFrame:SetScript("OnUpdate", function(self, elapsed)
    trackerUpdateTimer = trackerUpdateTimer + elapsed
    if trackerUpdateTimer >= 0.1 then
        trackerUpdateTimer = 0
        UpdateTrackerDisplay()
    end
end)

-- Auto-hide timer for completed runs
local autoHideTimer = nil

local function StartAutoHide(delay)
    autoHideTimer = delay
    TrackerFrame:SetScript("OnUpdate", function(self, elapsed)
        -- Continue updating display
        trackerUpdateTimer = trackerUpdateTimer + elapsed
        if trackerUpdateTimer >= 0.1 then
            trackerUpdateTimer = 0
            UpdateTrackerDisplay()
        end
        -- Count down auto-hide
        if autoHideTimer then
            autoHideTimer = autoHideTimer - elapsed
            if autoHideTimer <= 0 then
                autoHideTimer = nil
                TrackerFrame:Hide()
                activeRunData = nil
            end
        end
    end)
end

-- ============================================================================
-- AIO Client Handlers (called from server)
-- ============================================================================

-- IMPORTANT: Use a GLOBAL handler table that persists across addon reloads.
-- AIO.AddHandlers only allows one registration per name, and the internal
-- handler wrapper holds a reference to the TABLE object. By using a global
-- table and updating its functions in-place, the old registration still
-- dispatches to the NEW function closures (which reference current locals).
if not DC_ClientHandlers then
    DC_ClientHandlers = {}
end

-- NOTE: AIO always passes 'player' as the first argument to client handlers,
-- even on the client side. All handlers must accept (player, ...) signature.

-- Receive config via AIO init message (small payload)
DC_ClientHandlers.InitConfig = function(player, cfg)
    config = cfg or {}
end

-- Begin receiving dungeon/affix data (sent individually after login)
DC_ClientHandlers.InitBegin = function(player, dungeonCount, affixCount)
    dungeonData = {}
    affixData = {}
end

-- Receive a single dungeon (flat parameters, no nested tables)
DC_ClientHandlers.InitDungeon = function(player, mapId, name, timerMinutes, bossCount)
    table.insert(dungeonData, {
        mapId        = mapId,
        name         = name,
        timerMinutes = timerMinutes,
        bossCount    = bossCount,
    })
end

-- Receive a single affix (flat parameters)
DC_ClientHandlers.InitAffix = function(player, id, name, desc, minDiff, spellId)
    table.insert(affixData, {
        id      = id,
        name    = name,
        desc    = desc,
        minDiff = minDiff,
        spellId = spellId,
    })
end

-- All init data received
DC_ClientHandlers.InitComplete = function(player)
    DEFAULT_CHAT_FRAME:AddMessage(string.format(
        "|cff00ff00[Dungeon Challenge]|r Loaded %d dungeons, %d affixes.",
        #dungeonData, #affixData))
end

-- Server tells us to show the UI
DC_ClientHandlers.ShowUI = function(player)
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
DC_ClientHandlers.LeaderboardData = function(player, mapId, entries)
    ShowLeaderboardData(mapId, entries or {})
end

-- Receive personal runs data
DC_ClientHandlers.MyRunsData = function(player, entries)
    ShowMyRunsData(entries or {})
end

-- Receive snapshot data
DC_ClientHandlers.SnapshotData = function(player, mapId, entries)
    ShowSnapshotData(mapId, entries or {})
end

-- Challenge started notification
DC_ClientHandlers.ChallengeStarted = function(player, dungeonName, difficulty, starterName)
    DEFAULT_CHAT_FRAME:AddMessage(string.format(
        "|cff00ff00[Dungeon Challenge]|r |cffff8000%s|r started: "
        .. "|cff00ff00%s|r at Level |cffff8000%d|r!",
        starterName or "?", dungeonName or "?", difficulty or 0))
    MainFrame:Hide()
end

-- Error from server
DC_ClientHandlers.Error = function(player, errorMsg)
    DEFAULT_CHAT_FRAME:AddMessage(
        "|cffff0000[Dungeon Challenge]|r " .. (errorMsg or "Unknown error"))
end

-- ============================================================================
-- Active Run Tracker Handlers
-- ============================================================================

-- Server signals run has started (player entered dungeon)
DC_ClientHandlers.RunStart = function(player, dungeonName, difficulty, timerSeconds, totalBosses, affixString)
    activeRunData = {
        dungeonName = dungeonName or "Unknown",
        difficulty = difficulty or 1,
        timerSeconds = timerSeconds or 1800,
        totalBosses = totalBosses or 3,
        affixString = affixString or "None",
        startClientTime = GetTime(),
        bossesKilled = 0,
        deathCount = 0,
        penaltyTime = 0,
        bossKills = {},
        completed = false,
        completionTime = 0,
        inTime = false,
    }
    autoHideTimer = nil
    TrackerFrame:Show()
    DEFAULT_CHAT_FRAME:AddMessage(string.format(
        "|cff00ff00[Dungeon Challenge]|r Tracker active: |cffff8000%s|r +%d",
        dungeonName or "?", difficulty or 0))
end

-- Boss killed notification
DC_ClientHandlers.BossKilled = function(player, bossName, bossIndex, killElapsed)
    if not activeRunData then return end
    activeRunData.bossesKilled = bossIndex or (activeRunData.bossesKilled + 1)
    table.insert(activeRunData.bossKills, {
        name = bossName or "Unknown Boss",
        time = killElapsed or 0,
    })
    UpdateTrackerDisplay()
end

-- Death update notification
DC_ClientHandlers.DeathUpdate = function(player, deathCount, penaltyTime)
    if not activeRunData then return end
    activeRunData.deathCount = deathCount or 0
    activeRunData.penaltyTime = penaltyTime or 0
    UpdateTrackerDisplay()
end

-- Run completed (all bosses killed)
DC_ClientHandlers.RunCompleted = function(player, totalElapsed, inTime)
    if not activeRunData then return end
    activeRunData.completed = true
    activeRunData.completionTime = totalElapsed or 0
    activeRunData.inTime = inTime
    UpdateTrackerDisplay()

    if inTime then
        DEFAULT_CHAT_FRAME:AddMessage(
            "|cff00ff00[Dungeon Challenge]|r |cff00ff00COMPLETED IN TIME!|r")
    else
        DEFAULT_CHAT_FRAME:AddMessage(
            "|cff00ff00[Dungeon Challenge]|r |cffff0000Completed over time.|r")
    end

    -- Auto-hide tracker after 60 seconds
    StartAutoHide(60)
end

-- Run ended (player left dungeon or run was abandoned)
DC_ClientHandlers.RunEnd = function(player)
    activeRunData = nil
    TrackerFrame:Hide()
end

-- Only register once — avoids AIO "already registered" assert on addon reload
if not DC_HandlersRegistered then
    AIO.AddHandlers("DungeonChallenge", DC_ClientHandlers)
    DC_HandlersRegistered = true
end

-- ============================================================================
-- Slash Command to toggle UI
-- ============================================================================

SLASH_DUNGEONCHALLENGE1 = "/dc"
SLASH_DUNGEONCHALLENGE2 = "/dungeonchallenge"
SlashCmdList["DUNGEONCHALLENGE"] = function(msg)
    msg = (msg or ""):lower():trim()

    -- /dc tracker — toggle active run tracker
    if msg == "tracker" then
        if TrackerFrame:IsShown() then
            TrackerFrame:Hide()
        elseif activeRunData then
            TrackerFrame:Show()
        else
            DEFAULT_CHAT_FRAME:AddMessage(
                "|cff00ff00[Dungeon Challenge]|r No active run to track.")
        end
        return
    end

    -- Default: toggle main UI
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
