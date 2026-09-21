--[[
	GX - Gold Export
	-----------------
	Simple account-wide gold tracker.

	Purpose:
	  - Stores each character's current gold in the shared SavedVariables table
	    (GX_DB), so values from all characters on one account are aggregated.
	  - "/gx show"     shows total account gold in a modern Blizzard-themed frame.
	  - "/gx export"   shows a copyable text box with the current total.
	  - "/gx autosave" periodically reloads the UI so SavedVariables get written
	    to disk even without a manual reload (a reload is the only moment the
	    WoW client flushes SavedVariables to disk).

	Why this design?
	  The WoW Lua sandbox has NO io/os file API. An addon can never write a
	  plain .txt file - the only thing it writes is SavedVariables, and the
	  client only flushes those on reload/logout. That is why /gx autosave
	  exists (periodic ReloadUI) and why a small external script
	  (watcher.py) is used to read GX.lua and produce a clean text file for
	  OBS.

	UI reference (wow-ui-source):
	  - PortraitFrameFlatTemplate  (Blizzard_SharedXML/Mainline/SharedUIPanelTemplates.xml)
	  - DialogBorderTemplate & DialogHeaderTemplate
	                               (Blizzard_SharedXML/Shared/Dialog/DialogTemplates.xml)
	  - UIPanelCloseButton         (Blizzard_SharedXML/Mainline/SharedUIPanelTemplates.xml)
	  - InputBoxTemplate           (Blizzard_SharedXML/Shared/InputBox/InputBoxTemplates.xml)
	  Money display is done with the current GetMoneyString(...) from
	  Blizzard_SharedXML/FormattingUtil.lua.
]]

-- Chat prefix helper. Defined first so every function below can use it.
-- Do not remove - it's referenced by the autosave and slash command code.
local function addonLabel()
	return "|cff00c8ffGX|r: "
end

-- ===========================================================================
-- SavedVariables
-- GX_DB --- written to WTF\Account\<account>\SavedVariables\<folder>.lua
-- ===========================================================================

local function InitDB()
	if type(GX_DB) ~= "table" then
		GX_DB = {}
	end
	GX_DB.version = GX_DB.version or 1
	if type(GX_DB.characters) ~= "table" then
		GX_DB.characters = {}
	end
	if type(GX_DB.warband) ~= "table" then
		GX_DB.warband = { gold = 0, lastSeen = 0 }
	end
	if GX_DB.warband.gold == nil then
		GX_DB.warband.gold = 0
	end
	if type(GX_DB.minimap) ~= "table" then
		GX_DB.minimap = {}
	end
	-- LibDBIcon-1.0 reads/writes "hide" and "minimapPos" on this table directly.
	-- Migrate the old hand-rolled schema (shown / degrees) into that shape once.
	if GX_DB.minimap.hide == nil then
		if GX_DB.minimap.shown ~= nil then
			GX_DB.minimap.hide = (GX_DB.minimap.shown == false)
		else
			GX_DB.minimap.hide = false
		end
	end
	if GX_DB.minimap.minimapPos == nil then
		GX_DB.minimap.minimapPos = GX_DB.minimap.degrees or 220
	end
	GX_DB.minimap.shown = nil
	GX_DB.minimap.degrees = nil
	if GX_DB.autosaveNoInstances == nil then
		GX_DB.autosaveNoInstances = true
	end
end

InitDB()

-- Same coin icon is used for the /gx show portrait and the minimap button
-- (both rendered through the TempPortraitAlphaMask circle mask, so they
-- look like identical circular coin icons).
local COIN_ICON = "Interface\\Icons\\INV_Misc_Coin_05"

local COPPER_PER_SILVER = 100
local SILVER_PER_GOLD = 100
local COPPER_PER_GOLD = COPPER_PER_SILVER * SILVER_PER_GOLD

-- Default autosave interval (seconds). 60 minutes is safe, 60 s is the
-- documented minimum. ReloadUI briefly freezes the UI.
local AUTOSAVE_DEFAULT_S = 60 * 60

-- ===========================================================================
-- Globals for the frames (referenced from the UISpecialFrames table and the
-- watcher documentation). Pre-declared so nil-checking is trivial.
-- ===========================================================================

GXMainFrame = nil
GXExportFrame = nil

-- LibDataBroker data object backing the minimap/DataBar icon (assigned once
-- LibDataBroker is confirmed available; see the minimap button section).
local GXLDBObject = nil

-- ===========================================================================
-- Money helpers
-- ===========================================================================

-- Realms are inconsistent about spaces/apostrophes between API calls and
-- client versions (e.g. "Drak'thul" vs "Drakthul"), and connected realms can
-- report slightly different display names. Strip both so the same character
-- always maps to the same key.
local function NormalizeRealmName(realm)
	if not realm or realm == "" then
		return realm
	end
	realm = realm:gsub("'", "")
	realm = realm:gsub("%s+", "")
	return realm
end

-- Character lookup key: "Name-NormalizedRealm".
-- GetNormalizedRealmName() already strips spaces (it's what unit tokens use
-- for connected realms); NormalizeRealmName() additionally strips apostrophes.
local function GetCharacterKey()
	local name = UnitName("player")
	local realm = (GetNormalizedRealmName and GetNormalizedRealmName()) or GetRealmName()
	if not name or name == UNKNOWNOBJECT then
		return nil
	end
	realm = NormalizeRealmName(realm)
	if not realm or realm == "" then
		return nil
	end
	return name .. "-" .. realm
end

local function SplitCharacterKey(key)
	return key:match("^(.-)%-(.+)$")
end

local function NormalizeCharacterKey(key)
	local name, realm = SplitCharacterKey(key)
	if not name or not realm then
		return key
	end
	return name .. "-" .. NormalizeRealmName(realm)
end

-- One-time migration: fold any old-format keys (raw GetRealmName(), with
-- spaces/apostrophes) into the normalized key, keeping whichever entry was
-- seen most recently instead of silently dropping data.
local characterKeysMigrated = false
local function MigrateCharacterKeys()
	if characterKeysMigrated then
		return
	end
	characterKeysMigrated = true
	InitDB()
	local merged = {}
	for key, data in pairs(GX_DB.characters) do
		local normalizedKey = NormalizeCharacterKey(key)
		local existing = merged[normalizedKey]
		if type(data) == "table" and (not existing or (tonumber(data.lastSeen) or 0) > (tonumber(existing.lastSeen) or 0)) then
			merged[normalizedKey] = data
		elseif not existing then
			merged[normalizedKey] = data
		end
	end
	GX_DB.characters = merged
end

-- Store (overwrite) the current character's gold with a timestamp.
local function SaveCurrentCharacterGold()
	InitDB()
	local key = GetCharacterKey()
	if not key then
		return
	end
	local gold = GetMoney() or 0
	local entry = GX_DB.characters[key]
	if entry then
		entry.gold = gold
		entry.lastSeen = time()
	else
		GX_DB.characters[key] = { gold = gold, lastSeen = time() }
	end
end

-- Query and cache Warband (Account) Bank gold (The War Within / Midnight 11.0+)
local function UpdateWarbandBankGold()
	InitDB()
	if C_Bank and C_Bank.FetchDepositedMoney and Enum and Enum.BankType and Enum.BankType.Account then
		local ok, money = pcall(C_Bank.FetchDepositedMoney, Enum.BankType.Account)
		if ok and type(money) == "number" then
			local canView = C_Bank.CanViewBank and C_Bank.CanViewBank(Enum.BankType.Account)
			if money > 0 or canView then
				GX_DB.warband = GX_DB.warband or {}
				GX_DB.warband.gold = money
				GX_DB.warband.lastSeen = time()
			end
		end
	end
end

-- Forward declaration: real body is defined further down, once GXMainFrame /
-- GXExportFrame exist. Declared here (not "local function") so the closure
-- below always calls whatever RefreshOpenFrames currently points to.
local RefreshOpenFrames

-- Refresh Warband bank gold and any open frame, then re-check shortly after.
-- C_Bank.FetchDepositedMoney can lag a frame behind the event that reports a
-- deposit/withdraw made directly in the bank UI, so a single immediate fetch
-- can still show the pre-transaction amount.
local function ScheduleWarbandBankUpdate()
	UpdateWarbandBankGold()
	RefreshOpenFrames()
	C_Timer.After(0.2, function()
		UpdateWarbandBankGold()
		RefreshOpenFrames()
	end)
end

-- Sum all stored character gold values and Warband bank across the account.
local function GetTotalGold()
	InitDB()
	local total = 0
	for _, data in pairs(GX_DB.characters) do
		if type(data) == "table" then
			local gold = tonumber(data.gold)
			if gold then
				total = total + gold
			end
		end
	end
	if GX_DB.warband and type(GX_DB.warband.gold) == "number" then
		total = total + GX_DB.warband.gold
	end
	return total
end

-- Plain-text money ("12,345g 67s 89c") - used in the copyable export box.
-- Thousands separator uses the client's own BreakUpLargeNumbers (FrameXML),
-- so it stays consistent with locale settings instead of a hand-rolled regex.
local function FormatMoneyText(money)
	local gold = math.floor(money / COPPER_PER_GOLD)
	local silver = math.floor((money - gold * COPPER_PER_GOLD) / COPPER_PER_SILVER)
	local copper = money % COPPER_PER_SILVER

	local text = ""
	local separator = ""
	if gold > 0 then
		text = text .. BreakUpLargeNumbers(gold) .. "g"
		separator = " "
	end
	if silver > 0 then
		text = text .. separator .. silver .. "s"
		separator = " "
	end
	if copper > 0 or text == "" then
		text = text .. separator .. copper .. "c"
	end
	return text
end

-- Short gold-only text ("12,345g") for the LibDataBroker feed - what Titan
-- Panel / ElvUI DataBars / Details! etc. show next to the icon.
local function FormatMoneyCompact(money)
	local gold = math.floor(money / COPPER_PER_GOLD)
	return BreakUpLargeNumbers(gold) .. "g"
end

-- Native-styled money string (coin texture markup / colorblind abbreviations),
-- produced by Blizzard's GetMoneyString. Used in the /gx show frame.
-- Some client versions can return an empty string for 0 copper even with
-- ShowZeroAsGold set, so fall back to a plain "0g" rather than showing nothing.
local function GetStyledMoneyString(money)
	money = tonumber(money) or 0
	local text = GetMoneyString(
		money,
		MoneyStringConstants.SeparateThousands,
		MoneyStringConstants.CheckGoldThreshold,
		MoneyStringConstants.ShowZeroAsGold
	)
	if not text or text == "" then
		return "0g"
	end
	return text
end

-- ===========================================================================
-- Main frame (/gx show) - created lazily from "GXMainFrameTemplate"
-- ===========================================================================

local GXMainFrameMixin = {}
_G.GXMainFrameMixin = GXMainFrameMixin

function GXMainFrameMixin:OnLoad()
	self:SetTitle("GX - Total Account Gold")
	self:SetPortraitToAsset(COIN_ICON)

	-- Compact: single readable line with the total + a small subtitle.
	local amount = self:CreateFontString(nil, "OVERLAY", "GameFontNormalHuge")
	amount:SetPoint("CENTER", 0, -2)
	amount:SetJustifyH("CENTER")
	amount:SetFont(STANDARD_TEXT_FONT, 20)
	amount:SetShadowOffset(1, -1)
	self.Amount = amount

	local subtitle = self:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	subtitle:SetPoint("CENTER", 0, -30)
	subtitle:SetJustifyH("CENTER")
	subtitle:SetFont(STANDARD_TEXT_FONT, 11)
	subtitle:SetText("Total account gold")
	self.Subtitle = subtitle

	-- Draggable.
	self:SetMovable(true)
	self:EnableMouse(true)
	self:RegisterForDrag("LeftButton")
end

function GXMainFrameMixin:OnDragStart()
	self:StartMoving()
end

function GXMainFrameMixin:OnDragStop()
	self:StopMovingOrSizing()
end

-- ===========================================================================
-- Export frame (/gx export) - created lazily from "GXExportFrameTemplate"
-- ===========================================================================

local GXExportFrameMixin = {}
_G.GXExportFrameMixin = GXExportFrameMixin

function GXExportFrameMixin:OnLoad()
	self.Header:Setup("Gold Export - copy me")

	local hint = self:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	hint:SetPoint("BOTTOM", self, "BOTTOM", 0, 10)
	hint:SetText("Select the text and press Ctrl+C to copy.")
	self.Hint = hint

	-- Draggable.
	self:SetMovable(true)
	self:EnableMouse(true)
	self:RegisterForDrag("LeftButton")
end

function GXExportFrameMixin:OnDragStart()
	self:StartMoving()
end

function GXExportFrameMixin:OnDragStop()
	self:StopMovingOrSizing()
end

-- ===========================================================================
-- Frame creation (lazy, guarded against double-create)
-- ===========================================================================

local function ShowTotalGoldFrame()
	if not GXMainFrame then
		GXMainFrame = CreateFrame("Frame", "GXMainFrame", UIParent, "GXMainFrameTemplate")
		-- ESC closes the frame.
		tinsert(UISpecialFrames, "GXMainFrame")
	end
	local total = GetTotalGold()
	GXMainFrame.Amount:SetText(GetStyledMoneyString(total))
	GXMainFrame:Show()
end

local function ToggleTotalGoldFrame()
	if GXMainFrame and GXMainFrame:IsShown() then
		GXMainFrame:Hide()
		return
	end
	ShowTotalGoldFrame()
end

local function ShowExportFrame()
	if not GXExportFrame then
		GXExportFrame = CreateFrame("Frame", "GXExportFrame", UIParent, "GXExportFrameTemplate")
		-- ESC closes the frame.
		tinsert(UISpecialFrames, "GXExportFrame")
	end
	local total = GetTotalGold()
	GXExportFrame.EditBox:SetText(FormatMoneyText(total) .. " (" .. tostring(total) .. "c)")
	GXExportFrame:Show()
end

local function ToggleExportFrame()
	if GXExportFrame and GXExportFrame:IsShown() then
		GXExportFrame:Hide()
		return
	end
	ShowExportFrame()
end

-- If any exported frame (or the LDB feed) is open/registered, refresh its
-- content. Assigned (not "local function") because it was forward-declared
-- earlier so ScheduleWarbandBankUpdate can already call it.
RefreshOpenFrames = function()
	local total = GetTotalGold()
	if GXMainFrame and GXMainFrame:IsShown() then
		GXMainFrame.Amount:SetText(GetStyledMoneyString(total))
	end
	if GXExportFrame and GXExportFrame:IsShown() then
		GXExportFrame.EditBox:SetText(FormatMoneyText(total) .. " (" .. tostring(total) .. "c)")
	end
	if GXLDBObject then
		GXLDBObject.text = FormatMoneyCompact(total)
	end
end

-- ===========================================================================
-- Autosave: periodic ReloadUI so SavedVariables are flushed to disk.
-- ===========================================================================

local autosaveTimer = nil
local autosaveSkipNoticeShown = false

local function CancelAutosaveTimer()
	if autosaveTimer then
		autosaveTimer:Cancel()
		autosaveTimer = nil
	end
	autosaveSkipNoticeShown = false
end

-- Defer the autosave reload while inside an instance (dungeon/raid/arena/BG)
-- if the "skip reload in instances" option is on. Reloading mid-content drops
-- the UI (brief freeze) and can be disruptive.
local function IsReloadAllowed()
	return not (GX_DB.autosaveNoInstances ~= false and IsInInstance())
end

local function ArmAutosaveTimer(seconds)
	autosaveTimer = C_Timer.NewTimer(seconds, function()
		autosaveTimer = nil
		if IsReloadAllowed() then
			ReloadUI()
		else
			if not autosaveSkipNoticeShown then
				autosaveSkipNoticeShown = true
				print(addonLabel() .. "Autosave reload skipped - player is in an instance.")
			end
			ArmAutosaveTimer(seconds)
		end
	end)
end

local function SetAutosave(seconds, silent)
	CancelAutosaveTimer()
	seconds = tonumber(seconds)
	if not seconds or seconds <= 0 then
		GX_DB.autosaveMinutes = nil
		if not silent then
			print(addonLabel() .. "Autosave disabled.")
		end
		return
	end

	GX_DB.autosaveMinutes = seconds / 60
	ArmAutosaveTimer(seconds)
	if not silent then
		print(addonLabel() .. string.format("Autosave enabled - UI reload every %d s.", seconds))
		print(addonLabel() .. "Reload flushes SavedVariables; expect a brief (< 1 s) UI freeze.")
	end
end

-- Re-arm on login so the loop survives reloads.
local function ArmAutosaveFromSaved()
	if GX_DB.autosaveMinutes and GX_DB.autosaveMinutes > 0 then
		local minutes = GX_DB.autosaveMinutes
		ArmAutosaveTimer(minutes * 60)
		print(addonLabel() .. string.format("Autosave active - UI reload every %.0f min.", minutes))
	end
end

-- ===========================================================================
-- Minimap / DataBroker icon
-- Exposes gold through a standard LibDataBroker-1.1 data object instead of
-- hand-rolled minimap trig. LibDBIcon-1.0 turns that data object into the
-- draggable minimap button; any other LDB display (Titan Panel, ElvUI
-- DataBars, Details!, XIV Databar, ...) can show the same feed directly.
-- ===========================================================================

local LDB = LibStub and LibStub:GetLibrary("LibDataBroker-1.1", true)
local LDBIcon = LibStub and LibStub:GetLibrary("LibDBIcon-1.0", true)

-- Forward declarations for the options panel
local GXSettingsCategory = nil
local GXSettingsPanel = nil
local OpenSettingsPanel
local RegisterGXSettings

-- Shared by the LDB tooltip and the Addon Compartment tooltip below.
local function AddGXTooltipLines(tooltip)
	tooltip:AddLine("GX - Total Account Gold")
	tooltip:AddLine(GetStyledMoneyString(GetTotalGold()), 1, 1, 1)
	if GX_DB.warband and GX_DB.warband.gold and GX_DB.warband.gold > 0 then
		tooltip:AddLine("Warband Bank: " .. GetStyledMoneyString(GX_DB.warband.gold), 0.8, 0.8, 0.8)
	end
	tooltip:AddLine(" ")
	tooltip:AddLine("Left-click: toggle gold window | Right-click: options", 0.6, 0.8, 1)
end

-- Shared by the LDB OnClick and the Addon Compartment click handler below.
local function OnGXDataObjectClick(_, mouseButton)
	if mouseButton == "RightButton" then
		OpenSettingsPanel()
	else
		SaveCurrentCharacterGold()
		ToggleTotalGoldFrame()
	end
end

local function CreateMinimapButton()
	if not LDB then
		print(addonLabel() .. "LibDataBroker-1.1 not available - minimap icon disabled.")
		return
	end
	GXLDBObject = LDB:NewDataObject("GX", {
		type = "data source",
		text = FormatMoneyCompact(GetTotalGold()),
		icon = COIN_ICON,
		OnClick = OnGXDataObjectClick,
		OnTooltipShow = function(tooltip)
			AddGXTooltipLines(tooltip)
			tooltip:AddLine("Drag to move around minimap", 0.4, 0.4, 0.4)
		end,
	})
	if LDBIcon then
		LDBIcon:Register("GX", GXLDBObject, GX_DB.minimap)
	else
		print(addonLabel() .. "LibDBIcon-1.0 not available - minimap icon disabled.")
	end
end

-- ===========================================================================
-- Addon Compartment (Blizzard native minimap menu in Dragonflight / TWW)
-- ===========================================================================

function GX_OnAddonCompartmentClick(addonName, mouseButton)
	OnGXDataObjectClick(nil, mouseButton)
end

function GX_OnAddonCompartmentEnter(addonName, button)
	GameTooltip:SetOwner(button, "ANCHOR_LEFT")
	AddGXTooltipLines(GameTooltip)
	GameTooltip:Show()
end

function GX_OnAddonCompartmentLeave(addonName, button)
	GameTooltip:Hide()
end

local function RegisterAddonCompartment()
	if AddonCompartmentFrame and AddonCompartmentFrame.RegisterAddon then
		AddonCompartmentFrame:RegisterAddon({
			text = "GX - Gold Export",
			icon = COIN_ICON,
			notCheckable = true,
			registerForAnyClick = true,
			func = function(_, menuInputData)
				local mouseButton = type(menuInputData) == "table" and menuInputData.buttonName or menuInputData
				GX_OnAddonCompartmentClick("GX", mouseButton)
			end,
			funcOnEnter = function(button)
				GX_OnAddonCompartmentEnter("GX", button)
			end,
			funcOnLeave = function(button)
				GX_OnAddonCompartmentLeave("GX", button)
			end,
		})
	end
end

-- ===========================================================================
-- Options panel (Esc -> Options -> AddOns -> GX - Gold Export)
-- Built through the modern Settings canvas API. All values are applied
-- immediately and live in GX_DB.
-- ===========================================================================

local function GetSettingsCategoryID()
	if not GXSettingsCategory and RegisterGXSettings then
		RegisterGXSettings()
	end
	if not GXSettingsCategory then
		return nil
	end
	if type(GXSettingsCategory) == "table" then
		if GXSettingsCategory.GetID then
			return GXSettingsCategory:GetID()
		elseif GXSettingsCategory.ID then
			return GXSettingsCategory.ID
		end
	elseif type(GXSettingsCategory) == "number" then
		return GXSettingsCategory
	end
	return nil
end

OpenSettingsPanel = function()
	local categoryID = GetSettingsCategoryID()
	if categoryID and Settings and Settings.OpenToCategory then
		local ok = pcall(Settings.OpenToCategory, categoryID)
		if not ok then
			pcall(Settings.OpenToCategory, GXSettingsCategory)
		end
	elseif SettingsPanel and SettingsPanel.Show then
		SettingsPanel:Show()
	else
		print(addonLabel() .. "Options panel not available (Settings addon missing).")
	end
end

local function BuildSettingsPanel(panel)
	panel:SetSize(460, 380)

	local title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
	title:SetPoint("TOPLEFT", 20, -20)
	title:SetText("GX - Gold Export")

	local totalLabel = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	totalLabel:SetPoint("TOPLEFT", 20, -52)
	totalLabel:SetJustifyH("LEFT")
	panel.TotalLabel = totalLabel

	-- Minimap button ---------------------------------------------------------
	local mmHeader = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
	mmHeader:SetPoint("TOPLEFT", 20, -88)
	mmHeader:SetText("Minimap button")

	local minimapCheck = CreateFrame("CheckButton", nil, panel, "UICheckButtonTemplate")
	minimapCheck:SetSize(26, 26)
	minimapCheck:SetPoint("TOPLEFT", 20, -108)
	minimapCheck:SetScript("OnClick", function(self)
		InitDB()
		GX_DB.minimap.hide = self:GetChecked() ~= true
		if LDBIcon then
			if GX_DB.minimap.hide then
				LDBIcon:Hide("GX")
			else
				LDBIcon:Show("GX")
			end
		end
	end)
	panel.MinimapCheck = minimapCheck

	local mmCheckLabel = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	mmCheckLabel:SetPoint("LEFT", minimapCheck, "RIGHT", 8, 0)
	mmCheckLabel:SetText("Show circular icon next to the minimap")

	-- Autosave ---------------------------------------------------------------
	local autoHeader = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
	autoHeader:SetPoint("TOPLEFT", 20, -152)
	autoHeader:SetText("Autosave")

	local autoSlider = CreateFrame("Slider", nil, panel, "OptionsSliderTemplate")
	autoSlider:SetSize(300, 17)
	autoSlider:SetPoint("TOPLEFT", 20, -175)
	autoSlider:SetMinMaxValues(0, 240)
	autoSlider:SetValueStep(5)
	autoSlider:SetObeyStepOnDrag(true)
	panel.AutoSlider = autoSlider

	local autoValue = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	autoValue:SetPoint("LEFT", autoSlider, "RIGHT", 12, 0)
	autoValue:SetText("")
	panel.AutoValue = autoValue

	local function SliderMinutes(slider)
		return math.floor(slider:GetValue() + 0.5)
	end

	autoSlider:SetScript("OnValueChanged", function(self, value)
		local minutes = math.floor(value + 0.5)
		autoValue:SetText(minutes <= 0 and "off" or (minutes .. " min"))
	end)
	autoSlider:SetScript("OnMouseUp", function(self)
		SetAutosave(SliderMinutes(self) * 60, true)
	end)

	local autoInfo = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
	autoInfo:SetPoint("TOPLEFT", 20, -210)
	autoInfo:SetJustifyH("LEFT")
	autoInfo:SetText("0 = disabled. Every N minutes the UI reloads so the client writes\nSavedVariables to disk and watcher.py updates totalgold.txt for OBS.")

	local noInstancesCheck = CreateFrame("CheckButton", nil, panel, "UICheckButtonTemplate")
	noInstancesCheck:SetSize(26, 26)
	noInstancesCheck:SetPoint("TOPLEFT", 20, -244)
	noInstancesCheck:SetScript("OnClick", function(self)
		InitDB()
		GX_DB.autosaveNoInstances = self:GetChecked() == true
	end)
	panel.NoInstancesCheck = noInstancesCheck

	local noInstancesLabel = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	noInstancesLabel:SetPoint("LEFT", noInstancesCheck, "RIGHT", 8, 0)
	noInstancesLabel:SetText("Skip autosave reload while in an instance")

	-- Buttons ----------------------------------------------------------------
	local showButton = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
	showButton:SetSize(190, 26)
	showButton:SetText("Show gold window")
	showButton:SetPoint("BOTTOMLEFT", 20, 26)
	showButton:SetScript("OnClick", function()
		SaveCurrentCharacterGold()
		ShowTotalGoldFrame()
	end)

	local reloadButton = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
	reloadButton:SetSize(190, 26)
	reloadButton:SetText("Save & reload now")
	reloadButton:SetPoint("LEFT", showButton, "RIGHT", 10, 0)
	reloadButton:SetScript("OnClick", function()
		SaveCurrentCharacterGold()
		ReloadUI()
	end)

	local resetButton = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
	resetButton:SetSize(190, 26)
	resetButton:SetText("Reset saved gold")
	resetButton:SetPoint("LEFT", reloadButton, "RIGHT", 10, 0)
	resetButton:SetScript("OnClick", function()
		StaticPopup_Show("GX_CONFIRM_RESET")
	end)

	local resetHint = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
	resetHint:SetPoint("BOTTOMLEFT", showButton, "TOPLEFT", 0, 10)
	resetHint:SetJustifyH("LEFT")
	resetHint:SetText("To drop a single stale character (deleted/renamed/moved) instead of\nwiping everything, use |cffffffff/gx characters|r and |cffffffff/gx forget <name-realm>|r.")

	-- Sync the controls with GX_DB whenever the settings panel is shown.
	panel.OnRefresh = function(self)
		InitDB()
		self.TotalLabel:SetText("Current total: " .. GetStyledMoneyString(GetTotalGold()))
		self.MinimapCheck:SetChecked(not GX_DB.minimap or GX_DB.minimap.hide ~= true)
		self.NoInstancesCheck:SetChecked(GX_DB.autosaveNoInstances ~= false)
		local minutes = math.max(math.floor((GX_DB.autosaveMinutes or 0) + 0.5), 0)
		self.AutoSlider:SetValue(minutes)
		self.AutoValue:SetText(minutes <= 0 and "off" or (minutes .. " min"))
	end
end

-- Register the category into Blizzard's in-game AddOns settings list.
do
	RegisterGXSettings = function()
		if GXSettingsCategory then
			return GXSettingsCategory
		end
		if not (Settings and Settings.RegisterCanvasLayoutCategory) then
			print(addonLabel() .. "Settings API not available - options panel skipped.")
			return nil
		end
		InitDB()
		local panel = CreateFrame("Frame", "GXSettingsFrame", UIParent)
		BuildSettingsPanel(panel)
		GXSettingsPanel = panel
		local category = Settings.RegisterCanvasLayoutCategory(panel, "GX - Gold Export")
		GXSettingsCategory = category
		Settings.RegisterAddOnCategory(category)
		return category
	end

	-- Fires immediately (GX is already loaded at this point) or on ADDON_LOADED.
	if EventUtil and EventUtil.ContinueOnAddOnLoaded then
		EventUtil.ContinueOnAddOnLoaded("GX", function()
			RegisterGXSettings()
		end)
	else
		RegisterGXSettings()
	end
end

-- ===========================================================================
-- Character list management (/gx characters, /gx forget)
-- Lets a single stale entry (deleted/renamed/moved character) be dropped
-- without wiping the whole database via /gx reset.
-- ===========================================================================

local function GetSortedCharacterEntries()
	InitDB()
	local entries = {}
	for key, data in pairs(GX_DB.characters) do
		if type(data) == "table" then
			table.insert(entries, { key = key, data = data })
		end
	end
	table.sort(entries, function(a, b)
		return (tonumber(a.data.lastSeen) or 0) > (tonumber(b.data.lastSeen) or 0)
	end)
	return entries
end

local function PrintCharacterList()
	local entries = GetSortedCharacterEntries()
	if #entries == 0 then
		print(addonLabel() .. "No saved characters yet.")
		return
	end
	print(addonLabel() .. "Saved characters (" .. #entries .. "):")
	for i, entry in ipairs(entries) do
		local lastSeen = tonumber(entry.data.lastSeen)
		local when = lastSeen and date("%Y-%m-%d", lastSeen) or "?"
		print(string.format("%s|cffffffff%d.|r %s - %s (last seen %s)",
			addonLabel(), i, entry.key, GetStyledMoneyString(tonumber(entry.data.gold) or 0), when))
	end
	print(addonLabel() .. "Use |cffffffff/gx forget <number or Name-Realm>|r to drop one.")
end

local function ForgetCharacter(identifier)
	identifier = (identifier or ""):match("^%s*(.-)%s*$")
	if identifier == "" then
		print(addonLabel() .. "Usage: /gx forget <number from /gx characters, or Name-Realm>.")
		return
	end
	local entries = GetSortedCharacterEntries()
	local index = tonumber(identifier)
	local targetKey = nil
	if index and entries[math.floor(index)] then
		targetKey = entries[math.floor(index)].key
	else
		for _, entry in ipairs(entries) do
			if entry.key:lower() == identifier:lower() then
				targetKey = entry.key
				break
			end
		end
	end
	if not targetKey then
		print(addonLabel() .. "No saved character matches '" .. identifier .. "'. Use /gx characters to list them.")
		return
	end
	GX_DB.characters[targetKey] = nil
	print(addonLabel() .. "Forgot " .. targetKey .. ". New total: " .. GetStyledMoneyString(GetTotalGold()))
	RefreshOpenFrames()
end

-- ===========================================================================
-- Confirmation dialogs
-- ===========================================================================

StaticPopupDialogs["GX_CONFIRM_RESET"] = {
	text = "Delete ALL saved GX gold data - every character and the Warband bank? This cannot be undone.\n\nTip: to drop just one stale character instead, use /gx forget.",
	button1 = "Delete all",
	button2 = "Cancel",
	OnAccept = function()
		GX_DB.characters = {}
		GX_DB.warband = { gold = 0, lastSeen = time() }
		print(addonLabel() .. "All saved gold data cleared.")
		RefreshOpenFrames()
	end,
	timeout = 0,
	whileDead = true,
	hideOnEscape = true,
	preferredIndex = 3,
}

-- ===========================================================================
-- Slash command: /gx
-- ===========================================================================

SLASH_GX1 = "/gx"

SlashCmdList.GX = function(msg)
	local cmd, rest = string.match(msg or "", "^(%S*)%s*(.-)$")
	cmd = string.lower(cmd or "")

	if cmd == "" or cmd == "help" then
		print(addonLabel() .. "Commands:")
		print(addonLabel() .. "  |cffffffff/gx show|r        - open the compact gold window")
		print(addonLabel() .. "  |cffffffff/gx export|r      - open a copyable export box")
		print(addonLabel() .. "  |cffffffff/gx autosave|r    - toggle periodic reload (default 60 min)")
		print(addonLabel() .. "  |cffffffff/gx autosave <min>|r  - set interval (>= 1)")
		print(addonLabel() .. "  |cffffffff/gx autosave off|r    - stop autosave")
		print(addonLabel() .. "  |cffffffff/gx settings|r    - open the options panel (Esc -> Options -> AddOns)")
		print(addonLabel() .. "  |cffffffff/gx minimap|r     - show / hide the minimap icon")
		print(addonLabel() .. "  |cffffffff/gx characters|r  - list every saved character + last seen date")
		print(addonLabel() .. "  |cffffffff/gx forget <# or Name-Realm>|r - drop one stale character's saved gold")
		print(addonLabel() .. "  |cffffffff/gx reset|r       - clear ALL saved gold data (asks to confirm)")
	elseif cmd == "show" then
		SaveCurrentCharacterGold()
		ToggleTotalGoldFrame()
	elseif cmd == "export" then
		SaveCurrentCharacterGold()
		ToggleExportFrame()
	elseif cmd == "autosave" then
		if rest == "" then
			if GX_DB.autosaveMinutes and GX_DB.autosaveMinutes > 0 then
				SetAutosave(0)
			else
				SetAutosave(AUTOSAVE_DEFAULT_S)
			end
		elseif rest == "off" or rest == "0" or rest == "stop" then
			SetAutosave(0)
		else
			local minutes = tonumber(rest)
			if minutes and minutes >= 1 then
				SetAutosave(minutes * 60)
			else
				print(addonLabel() .. "Invalid interval. Usage: /gx autosave <minutes> (>= 1).")
			end
		end
	elseif cmd == "settings" or cmd == "options" then
		OpenSettingsPanel()
	elseif cmd == "minimap" then
		InitDB()
		GX_DB.minimap.hide = not (GX_DB.minimap.hide == true)
		if LDBIcon then
			if GX_DB.minimap.hide then
				LDBIcon:Hide("GX")
			else
				LDBIcon:Show("GX")
			end
		end
		print(addonLabel() .. (GX_DB.minimap.hide and "Minimap icon hidden." or "Minimap icon shown."))
	elseif cmd == "characters" then
		PrintCharacterList()
	elseif cmd == "forget" then
		ForgetCharacter(rest)
	elseif cmd == "reset" then
		StaticPopup_Show("GX_CONFIRM_RESET")
	else
		print(addonLabel() .. "Unknown command '" .. cmd .. "'. Type /gx for help.")
	end
end

-- ===========================================================================
-- Event handling
-- ===========================================================================

-- PLAYER_MONEY fires repeatedly during mass looting/vendor sales. Debounce it
-- (trailing edge) so a burst of events collapses into one save + refresh.
local MONEY_UPDATE_DEBOUNCE_S = 0.5
local moneyUpdateTimer = nil

local function DebouncedPlayerMoneyUpdate()
	if moneyUpdateTimer then
		moneyUpdateTimer:Cancel()
	end
	moneyUpdateTimer = C_Timer.NewTimer(MONEY_UPDATE_DEBOUNCE_S, function()
		moneyUpdateTimer = nil
		SaveCurrentCharacterGold()
		RefreshOpenFrames()
	end)
end

local eventFrame = CreateFrame("Frame")
eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:RegisterEvent("PLAYER_LOGIN")
eventFrame:RegisterEvent("PLAYER_MONEY")
eventFrame:RegisterEvent("ACCOUNT_MONEY")
eventFrame:RegisterEvent("BANKFRAME_OPENED")
eventFrame:RegisterEvent("BANKFRAME_CLOSED")
eventFrame:SetScript("OnEvent", function(self, event, ...)
	if event == "ADDON_LOADED" then
		local addon = ...
		if addon == "GX" then
			InitDB()
		end
	elseif event == "PLAYER_LOGIN" then
		InitDB()
		MigrateCharacterKeys()
		-- Fresh login: save the live value right away and re-arm autosave.
		SaveCurrentCharacterGold()
		ScheduleWarbandBankUpdate()
		ArmAutosaveFromSaved()
		RegisterAddonCompartment()
		-- Create the minimap/DataBroker icon once the UI is fully up.
		CreateMinimapButton()
	elseif event == "PLAYER_MONEY" then
		DebouncedPlayerMoneyUpdate()
	elseif event == "ACCOUNT_MONEY" or event == "BANKFRAME_OPENED" or event == "BANKFRAME_CLOSED" then
		-- Deposits/withdrawals made directly in the bank UI: refresh immediately
		-- and once more shortly after (see ScheduleWarbandBankUpdate).
		ScheduleWarbandBankUpdate()
	end
end)

-- ===========================================================================
-- Self-test at load: make sure the essential FrameXML helpers we rely on
-- exist. If something is missing we report it instead of erroring later.
-- ===========================================================================

do
	local missing = {}
	if not GetMoneyString then
		tinsert(missing, "GetMoneyString")
	end
	if not BreakUpLargeNumbers then
		tinsert(missing, "BreakUpLargeNumbers")
	end
	if not UISpecialFrames then
		tinsert(missing, "UISpecialFrames")
	end
	if not EventUtil then
		tinsert(missing, "EventUtil")
	end
	if not StaticPopupDialogs then
		tinsert(missing, "StaticPopupDialogs")
	end
	if not LibStub then
		tinsert(missing, "LibStub")
	end
	if next(missing) then
		print(addonLabel() .. "WARNING: missing FrameXML helpers - " .. table.concat(missing, ", "))
	end
end