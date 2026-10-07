local function current_script_dir()
    local source = debug.getinfo(1, "S").source or ""
    if source:sub(1, 1) == "@" then
        source = source:sub(2)
    end
    source = source:gsub("\\", "/")
    return source:match("^(.*)/[^/]+$") or "."
end

local DATA_DIR = current_script_dir()

local CONFIG = {
    speech_file = DATA_DIR .. "/730184662509771.txt",
    assist_settings_file = DATA_DIR .. "/118437650298441.lua",
    random_encounter_patch_addr = 0xCDFF,
    random_encounter_patch_original = 0xF0,
    random_encounter_patch_disabled = 0x60,
    stable_frames = 2,
    name_entry_delay_frames = 10,
    message_speed_delay_frames = 10,
    yes_no_prompt_delay_frames = 25,
    copy_destination_delay_frames = 10,
    menu_settle_frames = 10,
    command_cursor_settle_frames = 4,
    spell_cursor_settle_frames = 8,
    spell_window_open_confirm_frames = 4,
    spell_window_close_confirm_frames = 2,
    item_cursor_settle_frames = 12,
    item_window_open_confirm_frames = 4,
    item_window_close_confirm_frames = 2,
    two_choice_cursor_settle_frames = 54,
    two_choice_open_confirm_frames = 2,
    two_choice_close_confirm_frames = 2,
    shop_entry_delay_frames = 25,
    battle_entry_delay_frames = 12,
    status_entry_hold_frames = 20,
    dialog_wait_join_pause_frames = 6,
    dialog_wait_flush_idle_frames = 150,
    dialog_wait_min_text_chars = 2,
    save_warning_join_pause_frames = 8,
    save_warning_flush_idle_frames = 150,
    reward_grace_frames = 180,
    critical_health_repeat_frames = 600,
    dialog_arrow_row = 26,
    dialog_arrow_col_start = 24,
    dialog_arrow_col_end = 27,
}

local CURSOR_COL_ADDR = 0x00D8
local CURSOR_ROW_ADDR = 0x00D9
local WND_COL_POS_ADDR = 0x0097
local WND_ROW_POS_ADDR = 0x0098
local WND_DATA_PTR_LB_ADDR = 0x009F
local WND_DATA_PTR_UB_ADDR = 0x00A0
local SELECT_MARK_VALUE = 0x42
local DIALOG_ARROW_TILES = {
    [0x73] = true,
    [0x74] = true,
    [0x75] = true,
    [0x76] = true,
    [0x77] = true,
    [0x78] = true,
    [0x79] = true,
    [0x7A] = true,
    [0x7B] = true,
    [0x7C] = true,
    [0x7D] = true,
    [0x7E] = true,
    [0x7F] = true,
}

local EXP_ADDR = 0x00BA
local GOLD_ADDR = 0x00BC
local MAGIC_KEYS_ADDR = 0x00BF
local HERBS_ADDR = 0x00C0
local ITEM_SLOT_ADDRS = { 0x00C1, 0x00C2, 0x00C3, 0x00C4 }
local PLAYER_HP_ADDR = 0x00C5
local PLAYER_MP_ADDR = 0x00C6
local PLAYER_LEVEL_ADDR = 0x00C7
local EQUIPMENT_ADDR = 0x00BE
local PLAYER_MAX_HP_ADDR = 0x00CA
local PLAYER_MAX_MP_ADDR = 0x00CB
local SPELL_FLAGS_A_ADDR = 0x00CE
local SPELL_FLAGS_B_ADDR = 0x00CF
local DIALOG_TEMP_ADDR = 0x00DE
local DISP_NAME0_ADDR = 0x00B5
local DISP_NAME4_ADDR = 0x64C6
local CONTEXT_ADDR = 0x0096
local TEMP_BUFFER_ADDR = 0x64CA
local WND_NAME_INDEX_ADDR = 0x6504

local VALID_SAVE_ADDRS = { 0x6035, 0x6036, 0x6037 }
local VALID_SAVE_VALUE = 0xC8
local FILLED_MENU_WND_COL = 0x04
local FILLED_MENU_WND_ROW = 0x08
local FILLED_MENU_WND_DATA_PTR = 0xB20B
local FULL_MENU_WND_COL = 0x04
local FULL_MENU_WND_ROW = 0x08
local FULL_MENU_WND_DATA_PTR = 0xB249
local ADVENTURE_LOG_WND_COL = 0x0A
local ADVENTURE_LOG_WND_ROW = 0x12
local USED_LOG_WND_COL = 0x06
local USED_LOG_WND_ROW = 0x0C
local YES_NO_WND_COL = 0x14
local YES_NO_WND_ROW = 0x06
local MESSAGE_SPEED_WND_COL = 0x08
local MESSAGE_SPEED_WND_ROW = 0x0E
local NAME_ENTRY_WND_COL = 0x04
local NAME_ENTRY_WND_ROW = 0x0A
local SAVE_SLOT_BASE_ADDRS = { 0x6068, 0x61A8, 0x62E8 }
local SAVE_SLOT_NAME_OFFSET = 0x0F
local SAVE_SLOT_NAME_LENGTH = 8

local FULL_MENU_ITEMS = {
    [0] = "Continue a Quest",
    [1] = "Change Message Speed",
    [2] = "Begin a New Quest",
    [3] = "Copy a Quest",
    [4] = "Erase a Quest",
}

local FILLED_MENU_ITEMS = {
    [0] = "Continue a Quest",
    [1] = "Change Message Speed",
    [2] = "Erase a Quest",
}

local MESSAGE_SPEED_ITEMS = {
    [0] = "Fast",
    [1] = "Normal",
    [2] = "Slow",
}

local YES_NO_ITEMS = {
    [0] = "Yes",
    [1] = "No",
}

local NAME_GRID = {
    { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K" },
    { "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V" },
    { "W", "X", "Y", "Z", "-", "'", ",", "?", "(", ")" },
    { "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k" },
    { "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v" },
    { "w", "x", "y", "z", ",", ".", "BACK", "BACK", false, "END", "END" },
}
local NAME_GRID_WIDTH = 11

local COMMAND_GRID = {
    { "Talk", "Spell" },
    { "Status", "Item" },
    { "Stairs", "Door" },
    { "Search", "Take" },
}

local BATTLE_MENU_GRID = {
    { "Fight", "Spell" },
    { "Run", "Item" },
}
local ASSIST_MULTIPLIER_OPTIONS = {
    { label = "0x", value = 0.0 },
    { label = "0.5x", value = 0.5 },
    { label = "1x", value = 1.0 },
    { label = "2x", value = 2.0 },
    { label = "4x", value = 4.0 },
}
local ASSIST_MENU_ITEMS = {
    { id = "battles_enabled", label = "Random Encounters", values = { "On", "Off" } },
    { id = "exp_multiplier", label = "Experience Boost", values = ASSIST_MULTIPLIER_OPTIONS },
    { id = "gold_multiplier", label = "Gold Boost", values = ASSIST_MULTIPLIER_OPTIONS },
    { id = "pathfinding_filter_enabled", label = "Pathfinding Filter", values = { "Off", "On" } },
    { id = "critical_health_warnings_enabled", label = "Health Warning", values = { "Off", "On" } },
    { id = "hazard_warnings_enabled", label = "Damage Tile Warning", values = { "Off", "On" } },
}
local ASSIST_DEFAULT_SETTINGS = {
    battles_enabled = true,
    critical_health_warnings_enabled = true,
    hazard_warnings_enabled = true,
    exp_multiplier = 3,
    gold_multiplier = 3,
    pathfinding_filter_enabled = false,
}
local SPEED_ROWS = {
    [22] = "Fast",
    [24] = "Normal",
    [26] = "Slow",
}

local STATUS_ROWS = {
    { key = "name", row = 7, label = "NAME", kind = "text" },
    { key = "strength", row = 9, label = "STRENGTH", kind = "ram", addr = 0x00C8, spoken = "Strength" },
    { key = "agility", row = 11, label = "AGILITY", kind = "ram", addr = 0x00C9, spoken = "Agility" },
    { key = "max_hp", row = 13, label = "MAXIMUM HP", kind = "ram", addr = 0x00CA, spoken = "Maximum HP" },
    { key = "max_mp", row = 15, label = "MAXIMUM MP", kind = "ram", addr = 0x00CB, spoken = "Maximum MP" },
    { key = "attack", row = 17, label = "ATTACK POWER", kind = "ram", addr = 0x00CC, spoken = "Attack power" },
    { key = "defense", row = 19, label = "DEFENSE POWER", kind = "ram", addr = 0x00CD, spoken = "Defense power" },
    { key = "weapon", row = 21, label = "WEAPON", kind = "equipment" },
    { key = "armor", row = 23, label = "ARMOR", kind = "equipment" },
    { key = "shield", row = 25, label = "SHIELD", kind = "equipment" },
}

local STATUS_RESERVED_KEYS = { "Insert", "Delete" }
local STATUS_WINDOW_COL_POS = 0x0A
local STATUS_WINDOW_ROW_POS = 0x06
local STATUS_WINDOW_DATA_PTR = 0xAFC7
local CONTROLLER_BUTTON_B = 0x02
local CONTROLLER_BUTTON_SELECT = 0x04
local YES_NO_WINDOW_COL_POS = 0x0A
local YES_NO_WINDOW_ROW_POS = 0x04
local SPELL_WINDOW_COL_POS = 0x12
local SPELL_WINDOW_ROW_POS = 0x04
local ITEM_WINDOW_COL_POS = 0x12
local ITEM_WINDOW_ROW_POS = 0x06

local ITEM_NAMES = {
    [0x0] = "Empty",
    [0x1] = "Torch",
    [0x2] = "Fairy Water",
    [0x3] = "Wings",
    [0x4] = "Dragon's Scale",
    [0x5] = "Fairy Flute",
    [0x6] = "Fighter's Ring",
    [0x7] = "Erdrick's Token",
    [0x8] = "Gwaelin's Love",
    [0x9] = "Cursed Belt",
    [0xA] = "Silver Harp",
    [0xB] = "Death Necklace",
    [0xC] = "Stones of Sunlight",
    [0xD] = "Staff of Rain",
    [0xE] = "Rainbow Drop",
    [0xF] = "Herb",
}

local PLAYER_INV_DATA_PTR = 0xB0CC
local SHOP_INV_DATA_PTR = 0xB0DA
local YES_NO_1_DATA_PTR = 0xB0EB
local BUY_SELL_DATA_PTR = 0xB0FB
local COMMAND_NONCOMBAT_DATA_PTR = 0xB054
local COMMAND_COMBAT_DATA_PTR = 0xB095
local CONTEXT_FIELD = 0xFF
local CONTEXT_BATTLE = 0x00

local SHOP_ITEM_NAMES = {
    [0x00] = "Bamboo Pole",
    [0x01] = "Club",
    [0x02] = "Copper Sword",
    [0x03] = "Hand Axe",
    [0x04] = "Broad Sword",
    [0x05] = "Flame Sword",
    [0x06] = "Erdrick's Sword",
    [0x07] = "Clothes",
    [0x08] = "Leather Armor",
    [0x09] = "Chain Mail",
    [0x0A] = "Half Plate",
    [0x0B] = "Full Plate",
    [0x0C] = "Magic Armor",
    [0x0D] = "Erdrick's Armor",
    [0x0E] = "Small Shield",
    [0x0F] = "Large Shield",
    [0x10] = "Silver Shield",
    [0x11] = "Herb",
    [0x12] = "Magic Key",
    [0x13] = "Torch",
    [0x14] = "Fairy Water",
    [0x15] = "Wings",
    [0x16] = "Dragon's Scale",
}

local SHOP_ITEM_COSTS = {
    [0x00] = 10,
    [0x01] = 60,
    [0x02] = 180,
    [0x03] = 560,
    [0x04] = 1500,
    [0x05] = 9800,
    [0x06] = 2,
    [0x07] = 20,
    [0x08] = 70,
    [0x09] = 300,
    [0x0A] = 1000,
    [0x0B] = 3000,
    [0x0C] = 7700,
    [0x0D] = 2,
    [0x0E] = 90,
    [0x0F] = 800,
    [0x10] = 14800,
    [0x11] = 24,
    [0x12] = 53,
    [0x13] = 8,
    [0x14] = 38,
    [0x15] = 70,
    [0x16] = 20,
}

local SHOP_LISTS_BY_DIALOG_TEMP = {
    [0x00] = { 0x02, 0x03, 0x0A, 0x0B, 0x0E },
    [0x01] = { 0x00, 0x01, 0x02, 0x07, 0x08, 0x0E },
    [0x02] = { 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0F },
    [0x03] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0F },
    [0x04] = { 0x03, 0x04, 0x0B, 0x0C },
    [0x05] = { 0x05, 0x10 },
    [0x06] = { 0x02, 0x03, 0x04, 0x0A, 0x0B, 0x0C },
    [0x07] = { 0x11, 0x13, 0x16, 0x15 },
    [0x08] = { 0x11, 0x13, 0x16 },
    [0x09] = { 0x11, 0x13, 0x16 },
    [0x0A] = { 0x11, 0x13 },
    [0x0B] = { 0x16, 0x15 },
}

local WEAPON_VALUES = {
    [0x00] = "None",
    [0x20] = "Bamboo Pole",
    [0x40] = "Club",
    [0x60] = "Copper Sword",
    [0x80] = "Hand Axe",
    [0xA0] = "Broad Sword",
    [0xC0] = "Flame Sword",
    [0xE0] = "Erdrick's Sword",
}

local ARMOR_VALUES = {
    [0x00] = "None",
    [0x04] = "Clothes",
    [0x08] = "Leather Armor",
    [0x0C] = "Chain Mail",
    [0x10] = "Half Plate Armor",
    [0x14] = "Full Plate Armor",
    [0x18] = "Magic Armor",
    [0x1C] = "Erdrick's Armor",
}

local SHIELD_VALUES = {
    [0x00] = "None",
    [0x01] = "Small Shield",
    [0x02] = "Large Shield",
    [0x03] = "Silver Shield",
}

local SPELL_LABELS = {
    HEAL = "Heal",
    HURT = "Hurt",
    SLEEP = "Sleep",
    RADIANT = "Radiant",
    STOPSPELL = "Stopspell",
    OUTSIDE = "Outside",
    RETURN = "Return",
    REPEL = "Repel",
    HEALMORE = "Healmore",
    HURTMORE = "Hurtmore",
}

local SPELL_ORDER = {
    { addr = SPELL_FLAGS_A_ADDR, mask = 0x01, label = "Heal" },
    { addr = SPELL_FLAGS_A_ADDR, mask = 0x02, label = "Hurt" },
    { addr = SPELL_FLAGS_A_ADDR, mask = 0x04, label = "Sleep" },
    { addr = SPELL_FLAGS_A_ADDR, mask = 0x08, label = "Radiant" },
    { addr = SPELL_FLAGS_A_ADDR, mask = 0x10, label = "Stopspell" },
    { addr = SPELL_FLAGS_A_ADDR, mask = 0x20, label = "Outside" },
    { addr = SPELL_FLAGS_A_ADDR, mask = 0x40, label = "Return" },
    { addr = SPELL_FLAGS_A_ADDR, mask = 0x80, label = "Repel" },
    { addr = SPELL_FLAGS_B_ADDR, mask = 0x01, label = "Healmore" },
    { addr = SPELL_FLAGS_B_ADDR, mask = 0x02, label = "Hurtmore" },
}

local SPELL_COSTS = {
    Heal = 4,
    Hurt = 2,
    Sleep = 2,
    Radiant = 3,
    Stopspell = 2,
    Outside = 6,
    Return = 8,
    Repel = 2,
    Healmore = 10,
    Hurtmore = 5,
}

local state = {
    speech_sequence = 0,
    last_spoken = "",
    last_key = "",
    last_source = "",
    last_screen = "",
    last_menu_signature = "",
    command_entry_pending = false,
    entry_delay_frames_left = 0,
    menu_settle_frames_left = 0,
    command_window_open = false,
    command_window_entry_pending = false,
    command_cursor_settle_frames_left = 0,
    spell_window_open = false,
    spell_window_entry_pending = false,
    spell_cursor_settle_frames_left = 0,
    spell_window_seen_frames = 0,
    spell_window_missing_frames = 0,
    item_window_open = false,
    item_window_entry_pending = false,
    item_cursor_settle_frames_left = 0,
    item_window_seen_frames = 0,
    item_window_missing_frames = 0,
    yes_no_window_open = false,
    yes_no_window_entry_pending = false,
    yes_no_cursor_settle_frames_left = 0,
    yes_no_window_seen_frames = 0,
    yes_no_window_missing_frames = 0,
    observed_key = "",
    observed_text = "",
    observed_frames = 0,
    last_gameplay_menu_signature = "",
    gameplay_menu_settle_frames_left = 0,
    last_selection = "",
    last_keyboard = {},
    last_controller_0047 = 0,
    status_rows = {},
    status_virtual_cursor_index = 1,
    status_active = false,
    status_entry_hold_frames = 0,
    dialog_wait_chars = {},
    dialog_wait_last_char_key = nil,
    dialog_wait_last_stream_char_index = nil,
    dialog_wait_last_char_frame = -9999,
    dialog_wait_last_pattern = nil,
    dialog_wait_missing_pattern_frames = 0,
    dialog_wait_arrow_visible = false,
    dialog_wait_last_arrow_visible = false,
    dialog_wait_last_text = "",
    save_warning_chars = {},
    save_warning_last_key = nil,
    save_warning_last_index = nil,
    save_warning_last_char_frame = -9999,
    save_warning_stage = 0,
    save_warning_stage_frame = -9999,
    pending_action = "",
    pending_source_slot = nil,
    pending_source_name = nil,
    pending_target_slot = nil,
    pending_target_name = nil,
    pending_prompt_spoken = false,
    assist_menu_open = false,
    assist_menu_index = 1,
    assist_settings = {
        battles_enabled = ASSIST_DEFAULT_SETTINGS.battles_enabled,
        critical_health_warnings_enabled = ASSIST_DEFAULT_SETTINGS.critical_health_warnings_enabled,
        hazard_warnings_enabled = ASSIST_DEFAULT_SETTINGS.hazard_warnings_enabled,
        exp_multiplier = ASSIST_DEFAULT_SETTINGS.exp_multiplier,
        gold_multiplier = ASSIST_DEFAULT_SETTINGS.gold_multiplier,
        pathfinding_filter_enabled = ASSIST_DEFAULT_SETTINGS.pathfinding_filter_enabled,
    },
    prev_exp = nil,
    prev_gold = nil,
    reward_window_frames = 0,
    exp_reward_adjusted = false,
    gold_reward_adjusted = false,
    critical_health_announced = false,
    critical_health_pending = false,
    critical_health_last_frame = -9999,
    random_encounter_patch_applied = false,
    random_encounter_patch_warned = false,
}

local function write_speech_payload(message)
    local file = io.open(CONFIG.speech_file, "w")
    if not file then
        return
    end
    state.speech_sequence = state.speech_sequence + 1
    file:write(string.format("%d|%s", state.speech_sequence, message))
    file:close()
end

local function file_exists(path)
    local file = io.open(path, "r")
    if file then
        file:close()
        return true
    end
    return false
end

local function load_assist_settings()
    if not file_exists(CONFIG.assist_settings_file) then
        return
    end

    local chunk, err = loadfile(CONFIG.assist_settings_file)
    if not chunk then
        console.log("[dragon-warrior-access] failed to load assist settings: " .. tostring(err))
        return
    end

    local ok, data = pcall(chunk)
    if not ok or type(data) ~= "table" then
        console.log("[dragon-warrior-access] assist settings file did not return a table.")
        return
    end

    if type(data.battles_enabled) == "boolean" then
        state.assist_settings.battles_enabled = data.battles_enabled
    end

    if type(data.critical_health_warnings_enabled) == "boolean" then
        state.assist_settings.critical_health_warnings_enabled = data.critical_health_warnings_enabled
    end

    if type(data.hazard_warnings_enabled) == "boolean" then
        state.assist_settings.hazard_warnings_enabled = data.hazard_warnings_enabled
    end

    if type(data.pathfinding_filter_enabled) == "boolean" then
        state.assist_settings.pathfinding_filter_enabled = data.pathfinding_filter_enabled
    end

    for _, key in ipairs({ "exp_multiplier", "gold_multiplier" }) do
        local value = tonumber(data[key])
        if value and value >= 1 and value <= #ASSIST_MULTIPLIER_OPTIONS then
            state.assist_settings[key] = math.floor(value)
        end
    end
end

local function persist_assist_settings()
    local file = io.open(CONFIG.assist_settings_file, "w")
    if not file then
        console.log("[dragon-warrior-access] failed to save assist settings.")
        return false
    end

    file:write("return {\n")
    file:write(string.format("    battles_enabled = %s,\n", state.assist_settings.battles_enabled and "true" or "false"))
    file:write(string.format("    critical_health_warnings_enabled = %s,\n", state.assist_settings.critical_health_warnings_enabled and "true" or "false"))
    file:write(string.format("    hazard_warnings_enabled = %s,\n", state.assist_settings.hazard_warnings_enabled and "true" or "false"))
    file:write(string.format("    exp_multiplier = %d,\n", state.assist_settings.exp_multiplier or ASSIST_DEFAULT_SETTINGS.exp_multiplier))
    file:write(string.format("    gold_multiplier = %d,\n", state.assist_settings.gold_multiplier or ASSIST_DEFAULT_SETTINGS.gold_multiplier))
    file:write(string.format("    pathfinding_filter_enabled = %s,\n", state.assist_settings.pathfinding_filter_enabled and "true" or "false"))
    file:write("}\n")
    file:close()
    return true
end

local function sync_random_encounter_patch()
    local target = state.assist_settings.battles_enabled
        and CONFIG.random_encounter_patch_original
        or CONFIG.random_encounter_patch_disabled
    local ok_read, current = pcall(memory.read_u8, CONFIG.random_encounter_patch_addr, "System Bus")
    if not ok_read then
        if not state.random_encounter_patch_warned then
            state.random_encounter_patch_warned = true
        end
        return
    end

    if current ~= CONFIG.random_encounter_patch_original and current ~= CONFIG.random_encounter_patch_disabled then
        if not state.random_encounter_patch_warned then
            state.random_encounter_patch_warned = true
        end
        return
    end

    state.random_encounter_patch_warned = false
    state.random_encounter_patch_applied = (current == CONFIG.random_encounter_patch_disabled)
    if current == target then
        return
    end

    local ok_write = pcall(memory.write_u8, CONFIG.random_encounter_patch_addr, target, "System Bus")
    if not ok_write then
        if not state.random_encounter_patch_warned then
            state.random_encounter_patch_warned = true
        end
        return
    end

    local ok_verify, updated = pcall(memory.read_u8, CONFIG.random_encounter_patch_addr, "System Bus")
    if ok_verify and updated == target then
        state.random_encounter_patch_applied = (target == CONFIG.random_encounter_patch_disabled)
        state.random_encounter_patch_warned = false
    elseif not state.random_encounter_patch_warned then
        state.random_encounter_patch_warned = true
    end
end

local function speak(message, interrupt)
    if not message or message == "" then
        return
    end
    if interrupt == false and message == state.last_spoken then
        return
    end
    console.log("[dragon-warrior-access] " .. message)
    write_speech_payload(message)
    state.last_spoken = message
end

load_assist_settings()

local function normalize_text(text)
    text = text or ""
    text = text:gsub("%s+", " ")
    text = text:gsub("businesshere", "business here")
    text = text:gsub("Businesshere", "Business here")
    text = text:gsub("pointshave", "points have")
    text = text:gsub("Pointshave", "Points have")
    text = text:gsub("tothy", "to thy")
    text = text:gsub("Tothy", "To thy")
    text = text:gsub("raisethy", "raise thy")
    text = text:gsub("Raisethy", "Raise thy")
    text = text:gsub("GOLDincreases", "GOLD increases")
    text = text:gsub("goldincreases", "gold increases")
    text = text:gsub("Experienceincreases", "Experience increases")
    text = text:gsub("experienceincreases", "experience increases")
    text = text:gsub("increasesby", "increases by")
    text = text:gsub("(thy)%s*(gold)%s*(increases)%s*(by)", "%1 %2 %3 %4")
    text = text:gsub("(Thy)%s*(gold)%s*(increases)%s*(by)", "%1 %2 %3 %4")
    text = text:gsub("%.%.+", ".")
    text = text:gsub("%s+([%.!,%?;:])", "%1")
    text = text:gsub("([%.!,%?;:])([%a%d])", "%1 %2")
    text = text:gsub("^%s+", "")
    text = text:gsub("%s+$", "")
    return text
end

local function canonical_text(text)
    return string.upper(normalize_text(text or ""))
end

local function bit_is_set(value, mask)
    return math.floor(value / mask) % 2 == 1
end

local function format_spell_announcement(spell)
    local cost = SPELL_COSTS[spell]
    if not cost then
        return spell
    end
    return string.format("%s, %d MP", spell, cost)
end

local function decode_byte(b)
    if b >= 0x00 and b <= 0x09 then
        return string.char(string.byte("0") + b)
    end
    if b >= 0x0A and b <= 0x23 then
        return string.char(string.byte("a") + (b - 0x0A))
    end
    if b == 0x2C then return "I" end
    if b >= 0x24 and b <= 0x3D then
        return string.char(string.byte("A") + (b - 0x24))
    end
    if b == 0x3E or b == 0x3F then return "\"" end
    if b == 0x40 or b == 0x50 or b == 0x51 or b == 0x53 or b == 0x54 then return "'" end
    if b == 0x41 then return "*" end
    if b == 0x42 then return "C" end
    if b == 0x44 then return ":" end
    if b == 0x45 then return ".." end
    if b == 0x46 or b == 0x47 or b == 0x52 then return "." end
    if b == 0x48 or b == 0x53 then return "," end
    if b == 0x49 then return "-" end
    if b == 0x4B then return "?" end
    if b == 0x4C then return "!" end
    if b == 0x4D then return ";" end
    if b == 0x4E then return ")" end
    if b == 0x4F then return "(" end
    if b == 0x54 then return "!" end
    if b == 0x55 then return "?" end
    if b == 0x57 or b == 0x5F or b == 0x60 or b == 0x62 or b == 0x68 or b == 0x77 or b == 0x9F then return " " end
    return nil
end

local function decode_pair(b, next_b)
    if b ~= 0x50 then
        return nil, 1
    end
    if next_b and next_b >= 0x24 and next_b <= 0x3D then
        return string.char(string.byte("A") + (next_b - 0x24)), 2
    end
    return nil, 1
end

local function decode_row(base, start_col, end_col)
    local chars = {}
    local col = start_col
    while col <= end_col do
        local addr = base + col
        local b = memory.read_u8(addr)
        local next_b = nil
        if col < end_col then
            next_b = memory.read_u8(addr + 1)
        end
        local pair_text, step = decode_pair(b, next_b)
        if pair_text then
            chars[#chars + 1] = pair_text
            col = col + step
        else
            local ch = decode_byte(b)
            if ch then
                chars[#chars + 1] = ch
            end
            col = col + 1
        end
    end
    return normalize_text(table.concat(chars))
end
local function decode_bytes(bytes)
    local chars = {}
    local i = 1
    while i <= #bytes do
        local b = bytes[i]
        local next_b = bytes[i + 1]
        local pair_text, step = decode_pair(b, next_b)
        if pair_text then
            chars[#chars + 1] = pair_text
            i = i + step
        else
            local ch = decode_byte(b)
            if ch then
                chars[#chars + 1] = ch
            end
            i = i + 1
        end
    end
    return normalize_text(table.concat(chars))
end


local function read_row_text(row, start_col, end_col)
    memory.usememorydomain("CIRAM (nametables)")
    local text = decode_row(row * 32, start_col, end_col)
    memory.usememorydomain("System Bus")
    return text
end

local function keyboard_pressed(keys, name)
    return keys[name] and not state.last_keyboard[name]
end

local function has_flag(value, mask)
    return (value % (mask * 2)) >= mask
end

local function controller_button_pressed(mask)
    local current = mainmemory.read_u8(0x0047)
    return has_flag(current, mask) and not has_flag(state.last_controller_0047 or 0, mask)
end

local function any_pressed(keys, names)
    for _, name in ipairs(names) do
        if keyboard_pressed(keys, name) then
            return true
        end
    end
    return false
end

local function shift_held(keys)
    return keys.LeftShift
        or keys.RightShift
        or keys.Shift
        or keys.ShiftKey
        or keys.LShiftKey
        or keys.RShiftKey
end

local function shift_modified_pressed(keys, name)
    return shift_held(keys) and keyboard_pressed(keys, name)
end

local function suppress_gameplay_input()
    if not joypad or not joypad.set then
        return
    end

    pcall(function()
        joypad.set({
            Up = false,
            Down = false,
            Left = false,
            Right = false,
            A = false,
            B = false,
            Start = false,
            Select = false,
            Power = false,
            Reset = false,
        }, 1)
    end)
end

local function suppress_directional_gameplay_input()
    if not joypad or not joypad.set then
        return
    end

    pcall(function()
        joypad.set({
            Up = false,
            Down = false,
            Left = false,
            Right = false,
        }, 1)
    end)
end

local function read_dialog_arrow_visible()
    memory.usememorydomain("CIRAM (nametables)")
    local arrow_base = CONFIG.dialog_arrow_row * 32
    local visible = false
    for col = CONFIG.dialog_arrow_col_start, CONFIG.dialog_arrow_col_end do
        local tile = memory.read_u8(arrow_base + col)
        if DIALOG_ARROW_TILES[tile] then
            visible = true
            break
        end
    end
    memory.usememorydomain("System Bus")
    return visible
end

local function read_dialog_wait_pattern()
    local bytes = {}
    for addr = 0x01EF, 0x01F7 do
        bytes[addr] = mainmemory.read_u8(addr)
    end

    local wait_addr = nil
    for addr = 0x01EF, 0x01F7 do
        if bytes[addr] == 0xFB then
            wait_addr = addr
            break
        end
    end

    if not wait_addr or wait_addr + 2 > 0x01F7 then
        return nil
    end

    local char_addr = wait_addr + 2
    local char_byte = bytes[char_addr]
    local char = decode_byte(char_byte)
    if not char then
        return nil
    end

    return {
        wait_addr = wait_addr,
        char_addr = char_addr,
        char_byte = char_byte,
        char = char,
        dialog_step = mainmemory.read_u8(0x00D2),
        dialog_page = mainmemory.read_u8(0x00D3),
        stream_char_index = mainmemory.read_u8(0x0301),
    }
end

local function current_dialog_wait_text()
    return normalize_text(table.concat(state.dialog_wait_chars))
end

local function clear_dialog_wait_capture()
    state.dialog_wait_chars = {}
    state.dialog_wait_last_char_key = nil
    state.dialog_wait_last_stream_char_index = nil
    state.dialog_wait_last_char_frame = -9999
    state.dialog_wait_last_pattern = nil
    state.dialog_wait_missing_pattern_frames = 0
end

local function current_save_warning_text()
    return normalize_text(table.concat(state.save_warning_chars))
end

local function clear_save_warning_capture()
    state.save_warning_chars = {}
    state.save_warning_last_key = nil
    state.save_warning_last_index = nil
    state.save_warning_last_char_frame = -9999
end

local function maybe_speak_save_warning_text()
    local normalized = normalize_text(current_save_warning_text())

    if state.save_warning_stage == 0 and normalized:find("Please push RESET", 1, true) then
        speak("Please push RESET, hold it in, then turn off the POWER.", false)
        state.save_warning_stage = 1
        state.save_warning_stage_frame = emu.framecount()
        clear_save_warning_capture()
        return true
    end

    if state.save_warning_stage == 1
        and (normalized:find("Imperial Scroll of Honor", 1, true)
            or normalized:find("If you turn", 1, true)
            or normalized:find("f you turn", 1, true))
    then
        speak("If you turn the power off first, the Imperial Scroll of Honor containing your heroic deeds may be lost.", false)
        state.save_warning_stage = 2
        state.save_warning_stage_frame = emu.framecount()
        clear_save_warning_capture()
        return true
    end

    return false
end

local function flush_dialog_wait_capture()
    local text = current_dialog_wait_text()
    if #text < CONFIG.dialog_wait_min_text_chars then
        clear_dialog_wait_capture()
        return
    end
    state.dialog_wait_last_text = text
    speak(text, false)
    clear_dialog_wait_capture()
end

local function capture_dialog_wait_step()
    local pattern = read_dialog_wait_pattern()
    state.dialog_wait_last_pattern = pattern
    if not pattern then
        state.dialog_wait_missing_pattern_frames = state.dialog_wait_missing_pattern_frames + 1
        return
    end

    state.dialog_wait_missing_pattern_frames = 0

    local char_key = string.format("%02X:%02X:%02X:%02X",
        pattern.dialog_page,
        pattern.wait_addr,
        pattern.char_addr,
        pattern.stream_char_index
    )

    if char_key == state.dialog_wait_last_char_key then
        return
    end

    local previous_index = state.dialog_wait_last_stream_char_index
    local current_index = pattern.stream_char_index
    local index_gap = nil
    if previous_index ~= nil then
        index_gap = (current_index - previous_index) % 256
    end

    local previous_col = previous_index and (previous_index % 0x20) or nil
    local current_col = current_index % 0x20
    local likely_hidden_wrap_space = index_gap == 2
        and previous_col ~= nil
        and current_col == 0
        and previous_col >= 0x1D

    if likely_hidden_wrap_space and #state.dialog_wait_chars > 0 then
        local prev_char = state.dialog_wait_chars[#state.dialog_wait_chars]
        if prev_char and prev_char:match("[%w]") and pattern.char:match("^[%w]") then
            state.dialog_wait_chars[#state.dialog_wait_chars + 1] = " "
        end
    end

    state.dialog_wait_chars[#state.dialog_wait_chars + 1] = pattern.char
    state.dialog_wait_last_char_key = char_key
    state.dialog_wait_last_stream_char_index = current_index
    state.dialog_wait_last_char_frame = emu.framecount()
end

local function capture_save_warning_step()
    local char_byte = mainmemory.read_u8(0x0302)
    local char = decode_byte(char_byte)
    if not char then
        return
    end

    local index = mainmemory.read_u8(0x0301)
    local key = string.format("%02X:%02X", index, char_byte)
    if key == state.save_warning_last_key then
        return
    end

    local previous_index = state.save_warning_last_index
    local index_gap = nil
    if previous_index ~= nil then
        index_gap = (index - previous_index) % 256
    end

    local previous_col = previous_index and (previous_index % 0x20) or nil
    local current_col = index % 0x20
    local likely_hidden_wrap_space = index_gap == 2
        and previous_col ~= nil
        and current_col == 0
        and previous_col >= 0x1D

    if likely_hidden_wrap_space and #state.save_warning_chars > 0 then
        local prev_char = state.save_warning_chars[#state.save_warning_chars]
        if prev_char and prev_char:match("[%w]") and char:match("^[%w]") then
            state.save_warning_chars[#state.save_warning_chars + 1] = " "
        end
    end

    if #state.save_warning_chars == 0 and not char:match("[%a]") then
        return
    end

    state.save_warning_chars[#state.save_warning_chars + 1] = char
    state.save_warning_last_key = key
    state.save_warning_last_index = index
    state.save_warning_last_char_frame = emu.framecount()
end

local function update_dialog_wait_live()
    state.dialog_wait_arrow_visible = read_dialog_arrow_visible()
    capture_dialog_wait_step()

    local text = current_dialog_wait_text()
    local idle = emu.framecount() - state.dialog_wait_last_char_frame
    local arrow_appeared = state.dialog_wait_arrow_visible and not state.dialog_wait_last_arrow_visible
    local dialog_step = mainmemory.read_u8(0x00D2)

    if text ~= "" then
        if arrow_appeared then
            flush_dialog_wait_capture()
        elseif state.dialog_wait_missing_pattern_frames >= CONFIG.dialog_wait_join_pause_frames then
            flush_dialog_wait_capture()
        elseif idle >= CONFIG.dialog_wait_flush_idle_frames then
            flush_dialog_wait_capture()
        elseif idle >= CONFIG.dialog_wait_join_pause_frames and dialog_step == 0 then
            flush_dialog_wait_capture()
        end
    end

    local active = text ~= ""
        or state.dialog_wait_last_pattern ~= nil
        or (state.dialog_wait_missing_pattern_frames < CONFIG.dialog_wait_join_pause_frames
            and state.dialog_wait_last_char_frame > -9999)

    state.dialog_wait_last_arrow_visible = state.dialog_wait_arrow_visible
    return active
end

local function update_save_warning_live(dialog_wait_active)
    if dialog_wait_active then
        clear_save_warning_capture()
        return false
    end

    capture_save_warning_step()

    local text = current_save_warning_text()
    local idle = emu.framecount() - state.save_warning_last_char_frame
    if text ~= "" then
        if maybe_speak_save_warning_text() then
            return true
        end
        if idle >= CONFIG.save_warning_flush_idle_frames then
            clear_save_warning_capture()
        elseif idle >= CONFIG.save_warning_join_pause_frames then
            clear_save_warning_capture()
        end
    end

    if state.save_warning_stage > 0
        and emu.framecount() - state.save_warning_stage_frame >= 600
    then
        state.save_warning_stage = 0
        state.save_warning_stage_frame = -9999
    end

    return state.save_warning_stage > 0 or text ~= ""
end

local function read_window_col_pos()
    return mainmemory.read_u8(WND_COL_POS_ADDR)
end

local function read_window_row_pos()
    return mainmemory.read_u8(WND_ROW_POS_ADDR)
end

local function read_context()
    return mainmemory.read_u8(CONTEXT_ADDR)
end

local function read_window_data_ptr()
    local lo = mainmemory.read_u8(WND_DATA_PTR_LB_ADDR)
    local hi = mainmemory.read_u8(WND_DATA_PTR_UB_ADDR)
    return lo + (hi * 0x100)
end

local function read_cursor_col()
    return mainmemory.read_u8(CURSOR_COL_ADDR)
end

local function read_cursor_row()
    return mainmemory.read_u8(CURSOR_ROW_ADDR)
end

local function clear_current_announcement_state()
    state.last_source = ""
    state.observed_key = ""
    state.observed_text = ""
    state.observed_frames = 0
    state.last_key = ""
    state.last_gameplay_menu_signature = ""
    state.gameplay_menu_settle_frames_left = 0
    state.command_entry_pending = false
    state.entry_delay_frames_left = 0
end

local function read_u16_le(address)
    local lo = mainmemory.read_u8(address)
    local hi = mainmemory.read_u8(address + 1)
    return lo + (hi * 0x100)
end

local function write_u16_le(address, value)
    local clamped = math.max(0, math.min(0xFFFF, math.floor(value or 0)))
    local lo = clamped % 0x100
    local hi = math.floor(clamped / 0x100)
    mainmemory.write_u8(address, lo)
    mainmemory.write_u8(address + 1, hi)
    return clamped
end

local function assist_multiplier_value(setting_id)
    for _, item in ipairs(ASSIST_MENU_ITEMS) do
        if item.id == setting_id then
            local option_index = state.assist_settings[setting_id] or 1
            local option = item.values and item.values[option_index] or nil
            if type(option) == "table" then
                return tonumber(option.value) or 1.0
            end
            return tonumber(option) or 1.0
        end
    end
    return 1.0
end

local function reset_reward_cycle()
    state.reward_window_frames = CONFIG.reward_grace_frames
    state.exp_reward_adjusted = false
    state.gold_reward_adjusted = false
end

local function update_reward_window()
    if state.reward_window_frames > 0 then
        state.reward_window_frames = state.reward_window_frames - 1
        if state.reward_window_frames == 0 then
            state.exp_reward_adjusted = false
            state.gold_reward_adjusted = false
        end
    end
end

local function apply_reward_adjustment(address, delta, setting_id)
    if delta <= 0 then
        return read_u16_le(address)
    end

    local multiplier = assist_multiplier_value(setting_id)
    local adjusted_delta = math.max(0, math.floor((delta * multiplier) + 0.0001))
    local total = read_u16_le(address)
    return write_u16_le(address, total + (adjusted_delta - delta))
end

local function update_reward_modifiers()
    local current_exp = read_u16_le(EXP_ADDR)
    local current_gold = read_u16_le(GOLD_ADDR)

    if state.prev_exp == nil or state.prev_gold == nil then
        state.prev_exp = current_exp
        state.prev_gold = current_gold
        return
    end

    local exp_delta = current_exp - state.prev_exp
    local gold_delta = current_gold - state.prev_gold

    if exp_delta > 0 and not state.exp_reward_adjusted then
        reset_reward_cycle()
        current_exp = apply_reward_adjustment(EXP_ADDR, exp_delta, "exp_multiplier")
        state.exp_reward_adjusted = true
    end

    if state.reward_window_frames > 0 and gold_delta > 0 and not state.gold_reward_adjusted then
        current_gold = apply_reward_adjustment(GOLD_ADDR, gold_delta, "gold_multiplier")
        state.gold_reward_adjusted = true
    end

    state.prev_exp = current_exp
    state.prev_gold = current_gold
end

local function build_status_row_message(spec)
    if spec.kind == "ram" then
        return string.format("%s: %d.", spec.spoken, mainmemory.read_u8(spec.addr))
    end

    if spec.key == "name" then
        local bytes = {}
        memory.usememorydomain("System Bus")
        for offset = 0, 3 do
            bytes[#bytes + 1] = memory.read_u8(DISP_NAME0_ADDR + offset)
        end
        for offset = 0, 3 do
            bytes[#bytes + 1] = memory.read_u8(DISP_NAME4_ADDR + offset)
        end
        local value = normalize_text(decode_bytes(bytes))
        return value ~= "" and string.format("Name: %s.", value) or "Name."
    end

    if spec.kind == "equipment" then
        local equipment = mainmemory.read_u8(EQUIPMENT_ADDR)
        local weapon_value = math.floor(equipment / 0x20) * 0x20
        local armor_value = math.floor((equipment % 0x20) / 0x04) * 0x04
        local shield_value = equipment % 0x04

        if spec.key == "weapon" then
            return string.format("Weapon: %s.", WEAPON_VALUES[weapon_value] or "Unknown")
        end
        if spec.key == "armor" then
            return string.format("Armor: %s.", ARMOR_VALUES[armor_value] or "Unknown")
        end
        if spec.key == "shield" then
            return string.format("Shield: %s.", SHIELD_VALUES[shield_value] or "Unknown")
        end
    end

    return nil
end

local function build_status_rows()
    local rows = {}
    for _, spec in ipairs(STATUS_ROWS) do
        rows[#rows + 1] = {
            key = spec.key,
            text = build_status_row_message(spec),
        }
    end
    return rows
end

local function status_window_open()
    return read_window_col_pos() == STATUS_WINDOW_COL_POS
        and read_window_row_pos() == STATUS_WINDOW_ROW_POS
        and read_window_data_ptr() == STATUS_WINDOW_DATA_PTR
end

local function current_status_row_message()
    local entry = state.status_rows[state.status_virtual_cursor_index]
    if not entry then
        return nil
    end
    return entry.text
end

local function announce_current_status_row()
    local text = current_status_row_message()
    if text then
        speak(text, true)
    end
end

local function open_status_rows(rows)
    state.status_active = true
    state.status_rows = rows
    state.status_virtual_cursor_index = 1
    state.status_entry_hold_frames = CONFIG.status_entry_hold_frames
end

local function close_status_rows()
    state.status_active = false
    state.status_rows = {}
    state.status_virtual_cursor_index = 1
    state.status_entry_hold_frames = 0
end

local function handle_status_navigation(keys)
    if not state.status_active then
        return false
    end

    if state.status_entry_hold_frames > 0 then
        state.status_entry_hold_frames = state.status_entry_hold_frames - 1
        if state.status_entry_hold_frames == 0 then
            announce_current_status_row()
        end
        return true
    end

    if any_pressed(keys, { "Down", "DpadDown", "X1 DpadDown", "P1 Down" })
        and state.status_virtual_cursor_index < #state.status_rows
    then
        state.status_virtual_cursor_index = state.status_virtual_cursor_index + 1
        announce_current_status_row()
        return true
    end

    if any_pressed(keys, { "Up", "DpadUp", "X1 DpadUp", "P1 Up" })
        and state.status_virtual_cursor_index > 1
    then
        state.status_virtual_cursor_index = state.status_virtual_cursor_index - 1
        announce_current_status_row()
        return true
    end

    if any_pressed(keys, { "Right", "DpadRight", "X1 DpadRight", "P1 Right" })
        and #state.status_rows > 0
    then
        state.status_virtual_cursor_index = #state.status_rows
        announce_current_status_row()
        return true
    end

    if any_pressed(keys, { "Left", "DpadLeft", "X1 DpadLeft", "P1 Left" })
        and #state.status_rows > 0
    then
        state.status_virtual_cursor_index = 1
        announce_current_status_row()
        return true
    end

    return false
end

local function consume_reserved_status_keys(keys)
    if not state.status_active then
        return false
    end

    for _, key_name in ipairs(STATUS_RESERVED_KEYS) do
        if keyboard_pressed(keys, key_name) then
            return true
        end
    end

    return false
end

local function update_status_window(keys)
    local rows = build_status_rows()

    if status_window_open() and not state.status_active then
        open_status_rows(rows)
        clear_current_announcement_state()
        speak("Status window.", true)
        return true
    end

    if state.status_active then
        if not status_window_open() then
            speak("status window closed", true)
            close_status_rows()
            clear_current_announcement_state()
            return true
        end

        if #rows > 0 then
            state.status_rows = rows
        end
        if state.status_virtual_cursor_index > #state.status_rows then
            state.status_virtual_cursor_index = #state.status_rows
        end
        if state.status_virtual_cursor_index < 1 then
            state.status_virtual_cursor_index = 1
        end

        if state.assist_menu_open then
            return true
        end

        local handled_navigation = handle_status_navigation(keys)
        if not handled_navigation then
            consume_reserved_status_keys(keys)
        end
        return true
    end

    return false
end

local function read_name_field()
    memory.usememorydomain("System Bus")
    local length = memory.read_u8(WND_NAME_INDEX_ADDR)
    if length > 8 then
        length = 8
    end

    local bytes = {}
    for offset = 0, length - 1 do
        bytes[#bytes + 1] = memory.read_u8(TEMP_BUFFER_ADDR + offset)
    end
    memory.usememorydomain("System Bus")

    local name = decode_bytes(bytes)
    if name == "" then
        return "blank"
    end
    return name
end

local function read_saved_log_name(slot_index)
    local base_addr = SAVE_SLOT_BASE_ADDRS[slot_index + 1]
    if not base_addr then
        return nil
    end

    memory.usememorydomain("System Bus")
    local stored_bytes = {}
    for offset = 0, SAVE_SLOT_NAME_LENGTH - 1 do
        stored_bytes[#stored_bytes + 1] = memory.read_u8(base_addr + SAVE_SLOT_NAME_OFFSET + offset)
    end
    memory.usememorydomain("System Bus")

    local bytes = {
        stored_bytes[4], stored_bytes[3], stored_bytes[2], stored_bytes[1],
        stored_bytes[8], stored_bytes[7], stored_bytes[6], stored_bytes[5],
    }

    local name = decode_bytes(bytes)
    if name == "" then
        return "blank"
    end
    return name
end

local function build_visible_log_slots(want_used_slots)
    local slots = {}
    memory.usememorydomain("System Bus")
    for slot_index, addr in ipairs(VALID_SAVE_ADDRS) do
        local is_used = memory.read_u8(addr) == VALID_SAVE_VALUE
        if is_used == want_used_slots then
            slots[#slots + 1] = slot_index - 1
        end
    end
    memory.usememorydomain("System Bus")
    return slots
end

local function clear_pending_action()
    state.pending_action = ""
    state.pending_source_slot = nil
    state.pending_source_name = nil
    state.pending_target_slot = nil
    state.pending_target_name = nil
    state.pending_prompt_spoken = false
end

local function update_pending_action_from_menu_label(label)
    if label == "Erase a Quest" then
        state.pending_action = "erase"
        state.pending_source_slot = nil
        state.pending_source_name = nil
        state.pending_target_slot = nil
        state.pending_target_name = nil
        state.pending_prompt_spoken = false
    elseif label == "Copy a Quest" then
        state.pending_action = "copy-source"
        state.pending_source_slot = nil
        state.pending_source_name = nil
        state.pending_target_slot = nil
        state.pending_target_name = nil
        state.pending_prompt_spoken = false
    elseif label == "Change Message Speed" then
        state.pending_action = "change-message-speed"
        state.pending_source_slot = nil
        state.pending_source_name = nil
        state.pending_target_slot = nil
        state.pending_target_name = nil
        state.pending_prompt_spoken = false
    elseif label == "Continue a Quest" then
        state.pending_action = "continue"
        state.pending_source_slot = nil
        state.pending_source_name = nil
        state.pending_target_slot = nil
        state.pending_target_name = nil
        state.pending_prompt_spoken = false
    elseif label == "Begin a New Quest" then
        state.pending_action = "new-quest"
        state.pending_source_slot = nil
        state.pending_source_name = nil
        state.pending_target_slot = nil
        state.pending_target_name = nil
        state.pending_prompt_spoken = false
    end
end

local function maybe_prompt_for_yes_no()
    if state.pending_prompt_spoken then
        return nil
    end

    if state.pending_action == "erase" and state.pending_source_slot ~= nil then
        state.pending_prompt_spoken = true
        return string.format("Erase Adventure Log %d: %s?", state.pending_source_slot + 1, state.pending_source_name or "blank")
    end

    if state.pending_action == "copy-confirm"
        and state.pending_source_slot ~= nil
        and state.pending_target_slot ~= nil then
        state.pending_prompt_spoken = true
        return string.format(
            "Copy Adventure Log %d: %s to Adventure Log %d?",
            state.pending_source_slot + 1,
            state.pending_source_name or "blank",
            state.pending_target_slot + 1
        )
    end

    return nil
end

local function read_name_selection_from_cursor()
    local cursor_col = read_cursor_col()
    local cursor_row = read_cursor_row()
    local selection_index = cursor_row * NAME_GRID_WIDTH + cursor_col
    local label = nil

    if selection_index >= 0x00 and selection_index <= 0x19 then
        label = string.char(string.byte("A") + selection_index)
    elseif selection_index >= 0x1A and selection_index <= 0x20 then
        local symbols = {
            [0x1A] = "-",
            [0x1B] = "'",
            [0x1C] = "!",
            [0x1D] = "?",
            [0x1E] = "(",
            [0x1F] = ")",
            [0x20] = "SPACE",
        }
        label = symbols[selection_index]
    elseif selection_index >= 0x21 and selection_index <= 0x3A then
        label = string.char(string.byte("a") + (selection_index - 0x21))
    elseif selection_index == 0x3B then
        label = ","
    elseif selection_index == 0x3C then
        label = "."
    elseif selection_index == 0x3D then
        label = "BACK"
    elseif selection_index >= 0x3E then
        label = "END"
    end

    if not label then
        return nil, cursor_col, cursor_row
    end
    return label, cursor_col, cursor_row
end

local function speakable_name_selection(selection)
    if selection and #selection == 1 and selection:match("%u") then
        return "Cap " .. selection
    end
    local punctuation_names = {
        ["'"] = "apostrophe",
        ["-"] = "hyphen",
        [":"] = "colon",
        ["!"] = "exclamation point",
        ["?"] = "question mark",
        ["("] = "open parenthesis",
        [")"] = "close parenthesis",
        [","] = "comma",
        ["."] = "period",
    }
    if punctuation_names[selection] then
        return punctuation_names[selection]
    end
    return selection
end

local function read_opening_game_menu_item()
    local wnd_col = read_window_col_pos()
    local wnd_row = read_window_row_pos()
    local wnd_data_ptr = read_window_data_ptr()

    if wnd_col == FILLED_MENU_WND_COL
        and wnd_row == FILLED_MENU_WND_ROW
        and wnd_data_ptr == FILLED_MENU_WND_DATA_PTR then
        local cursor_row = read_cursor_row()
        local label = FILLED_MENU_ITEMS[cursor_row]
        if label then
            update_pending_action_from_menu_label(label)
            return label,
                string.format("opening:%02X:%02X:%04X:filled-menu:%02X", wnd_col, wnd_row, wnd_data_ptr, cursor_row),
                "filled-menu"
        end
    end

    if wnd_col == FULL_MENU_WND_COL
        and wnd_row == FULL_MENU_WND_ROW
        and wnd_data_ptr == FULL_MENU_WND_DATA_PTR then
        local cursor_row = read_cursor_row()
        local label = FULL_MENU_ITEMS[cursor_row]
        if label then
            update_pending_action_from_menu_label(label)
            return label,
                string.format("opening:%02X:%02X:%04X:full-menu:%02X", wnd_col, wnd_row, wnd_data_ptr, cursor_row),
                "full-menu"
        end
    end

    if wnd_col == 0x04
        and wnd_row == 0x08
        and mainmemory.read_u8(WND_DATA_PTR_LB_ADDR) == 0xA8
        and mainmemory.read_u8(WND_DATA_PTR_UB_ADDR) == 0xB2 then
        update_pending_action_from_menu_label("Begin a New Quest")
        return "Begin a New Quest",
            "opening:04:08:A8:B2:solo-new-quest",
            "solo-new-quest"
    end

    if wnd_col == ADVENTURE_LOG_WND_COL and wnd_row == ADVENTURE_LOG_WND_ROW then
        local cursor_row = read_cursor_row()
        local visible_slots = build_visible_log_slots(false)
        local slot_index = visible_slots[cursor_row + 1]
        if slot_index ~= nil then
            local label = string.format("Adventure Log %d", slot_index + 1)
            if state.pending_action == "copy-source" then
                state.pending_target_slot = slot_index
                state.pending_target_name = nil
                state.pending_prompt_spoken = false
            elseif state.pending_action == "new-quest" then
                state.pending_target_slot = slot_index
                state.pending_prompt_spoken = false
            end
            return label,
                string.format("opening:%02X:%02X:adventure-log:%02X:%02X", wnd_col, wnd_row, cursor_row, slot_index),
                "adventure-log"
        end
    end

    if wnd_col == USED_LOG_WND_COL and wnd_row == USED_LOG_WND_ROW then
        local cursor_row = read_cursor_row()
        local visible_slots = build_visible_log_slots(true)
        local slot_index = visible_slots[cursor_row + 1]
        local name = slot_index ~= nil and read_saved_log_name(slot_index) or nil
        if slot_index ~= nil and name then
            if state.pending_action == "erase" then
                state.pending_source_slot = slot_index
                state.pending_source_name = name
                state.pending_prompt_spoken = false
            elseif state.pending_action == "copy-source" then
                state.pending_source_slot = slot_index
                state.pending_source_name = name
                state.pending_prompt_spoken = false
            elseif state.pending_action == "change-message-speed" or state.pending_action == "continue" then
                state.pending_source_slot = slot_index
                state.pending_source_name = name
            end
            return string.format("Adventure Log %d: %s", slot_index + 1, name),
                string.format("opening:%02X:%02X:used-log:%02X:%02X:%s", wnd_col, wnd_row, cursor_row, slot_index, name),
                "used-log"
        end
    end

    if wnd_col == MESSAGE_SPEED_WND_COL and wnd_row == MESSAGE_SPEED_WND_ROW then
        local cursor_row = read_cursor_row()
        local label = MESSAGE_SPEED_ITEMS[cursor_row]
        if label then
            return label, string.format("opening:%02X:%02X:message-speed:%02X", wnd_col, wnd_row, cursor_row), "message-speed"
        end
    end

    if wnd_col == YES_NO_WND_COL and wnd_row == YES_NO_WND_ROW then
        local cursor_row = read_cursor_row()
        local label = YES_NO_ITEMS[cursor_row]
        if label then
            return label, string.format("opening:%02X:%02X:yes-no:%02X", wnd_col, wnd_row, cursor_row), "yes-no"
        end
    end

    if wnd_col == NAME_ENTRY_WND_COL and wnd_row == NAME_ENTRY_WND_ROW then
        local selection, cursor_col, cursor_row = read_name_selection_from_cursor()
        if not selection then
            return string.format("Unknown selection at column %d row %d.", cursor_col, cursor_row),
                string.format("opening:%02X:%02X:name:unknown:%02X:%02X", wnd_col, wnd_row, cursor_col, cursor_row),
                "name-entry"
        end

        local spoken_selection = speakable_name_selection(selection)
        local movement_key = string.format("opening:%02X:%02X:name-selection:%02X:%02X:%s", wnd_col, wnd_row, cursor_col, cursor_row, selection)
        if selection ~= state.last_selection then
            state.last_selection = selection
            return spoken_selection, movement_key, "name-entry"
        end

        return spoken_selection, movement_key, "name-entry"
    end

    return nil, nil, nil
end

local function current_menu_signature(screen)
    if not screen then
        return ""
    end

    return string.format("%02X:%02X:%04X:%s", read_window_col_pos(), read_window_row_pos(), read_window_data_ptr(), screen)
end

local function read_command_menu_item()
    if read_window_col_pos() ~= 0x0C or read_window_row_pos() ~= 0x02 then
        return nil, nil
    end

    if read_window_data_ptr() ~= COMMAND_NONCOMBAT_DATA_PTR then
        return nil, nil
    end

    if state.command_cursor_settle_frames_left > 0 then
        return nil, nil
    end

    local col = mainmemory.read_u8(CURSOR_COL_ADDR)
    local row = mainmemory.read_u8(CURSOR_ROW_ADDR)
    if col > 1 or row > 3 then
        return nil, nil
    end

    local row_data = COMMAND_GRID[row + 1]
    local item = row_data and row_data[col + 1] or nil
    if not item then
        return nil, nil
    end
    return item, string.format("command:%d:%d:%d:%d", read_window_col_pos(), read_window_row_pos(), col, row)
end

local function read_battle_menu_item()
    if read_window_col_pos() ~= 0x0C or read_window_row_pos() ~= 0x02 then
        return nil, nil
    end

    if read_window_data_ptr() ~= COMMAND_COMBAT_DATA_PTR then
        return nil, nil
    end

    local col = mainmemory.read_u8(CURSOR_COL_ADDR)
    local row = mainmemory.read_u8(CURSOR_ROW_ADDR)
    if col > 1 or row > 1 then
        return nil, nil
    end

    local row_data = BATTLE_MENU_GRID[row + 1]
    local item = row_data and row_data[col + 1] or nil
    if not item then
        return nil, nil
    end

    return item, string.format("battle:%d:%d:%d:%d", read_window_col_pos(), read_window_row_pos(), col, row)
end

local function decode_inventory_slots()
    local slots = {}
    for _, addr in ipairs(ITEM_SLOT_ADDRS) do
        local value = mainmemory.read_u8(addr)
        local high = math.floor(value / 0x10)
        local low = value % 0x10
        slots[#slots + 1] = low
        slots[#slots + 1] = high
    end
    return slots
end

local function build_visible_inventory_items()
    local items = {}
    local herb_count = mainmemory.read_u8(HERBS_ADDR)
    local key_count = mainmemory.read_u8(MAGIC_KEYS_ADDR)

    if herb_count > 0 then
        items[#items + 1] = string.format("Herb %d", herb_count)
    end

    if key_count > 0 then
        items[#items + 1] = string.format("Magic Key %d", key_count)
    end

    for slot_index, item_id in ipairs(decode_inventory_slots()) do
        if item_id ~= 0x0 then
            items[#items + 1] = ITEM_NAMES[item_id] or string.format("Unknown %X", item_id)
        end
    end

    return items
end

local function spell_window_open()
    return read_window_col_pos() == SPELL_WINDOW_COL_POS
        and read_window_row_pos() == SPELL_WINDOW_ROW_POS
end

local function item_window_open()
    return read_window_col_pos() == ITEM_WINDOW_COL_POS
        and read_window_row_pos() == ITEM_WINDOW_ROW_POS
end

local function yes_no_window_open()
    return read_window_col_pos() == YES_NO_WINDOW_COL_POS
        and read_window_row_pos() == YES_NO_WINDOW_ROW_POS
end

local function read_item_menu_item()
    if not state.item_window_open then
        return nil, nil
    end

    if state.item_cursor_settle_frames_left > 0 then
        return nil, nil
    end

    local visible_items = build_visible_inventory_items()
    if #visible_items == 0 then
        return nil, nil
    end

    local row = mainmemory.read_u8(CURSOR_ROW_ADDR)
    local selected = visible_items[row + 1] or visible_items[1]
    if selected then
        return selected, string.format("item:%d:%s", row, selected)
    end

    return nil, nil
end

local function current_shop_inventory_list()
    return SHOP_LISTS_BY_DIALOG_TEMP[mainmemory.read_u8(DIALOG_TEMP_ADDR)]
end

local function read_shop_inventory_item()
    local wnd_data_ptr = read_window_data_ptr()
    if wnd_data_ptr ~= SHOP_INV_DATA_PTR and wnd_data_ptr ~= PLAYER_INV_DATA_PTR then
        return nil, nil
    end

    local row = mainmemory.read_u8(CURSOR_ROW_ADDR)

    if wnd_data_ptr == PLAYER_INV_DATA_PTR then
        local visible_items = build_visible_inventory_items()
        if #visible_items == 0 then
            return nil, nil
        end
        local selected = visible_items[row + 1] or visible_items[1]
        if not selected then
            return nil, nil
        end
        return selected, string.format("shop-sell:%d:%s", row, canonical_text(selected))
    end

    local shop_list = current_shop_inventory_list()
    if not shop_list or #shop_list == 0 then
        return nil, nil
    end

    local item_id = shop_list[row + 1] or shop_list[1]
    local item_name = SHOP_ITEM_NAMES[item_id]
    if not item_name then
        return nil, nil
    end

    local cost = SHOP_ITEM_COSTS[item_id]
    local spoken = item_name
    if cost and cost > 0 then
        spoken = string.format("%s: %d gold", item_name, cost)
    end

    return spoken, string.format("shop-buy:%d:%s", row, canonical_text(spoken))
end

local function read_yes_no_item()
    if not state.yes_no_window_open then
        return nil, nil
    end

    if state.yes_no_cursor_settle_frames_left > 0 then
        return nil, nil
    end

    local row = mainmemory.read_u8(CURSOR_ROW_ADDR)
    local wnd_data_ptr = read_window_data_ptr()

    if wnd_data_ptr == BUY_SELL_DATA_PTR then
        return row == 0 and "Buy" or "Sell", string.format("two-choice:%d:BUYSELL", row)
    end

    if wnd_data_ptr == YES_NO_1_DATA_PTR then
        return row == 0 and "Yes" or "No", string.format("two-choice:%d:YESNO", row)
    end

    return row == 0 and "Yes" or "No", string.format("two-choice:%d:YESNO", row)
end

local function read_spell_menu_item()
    local learned_spells = {}
    for _, spec in ipairs(SPELL_ORDER) do
        if bit_is_set(mainmemory.read_u8(spec.addr), spec.mask) then
            learned_spells[#learned_spells + 1] = spec.label
        end
    end

    if not state.spell_window_open then
        return nil, nil
    end

    if state.spell_cursor_settle_frames_left > 0 then
        return nil, nil
    end

    if #learned_spells == 0 then
        return nil, nil
    end

    local row = mainmemory.read_u8(CURSOR_ROW_ADDR)
    local selected = learned_spells[row + 1] or learned_spells[1]
    if not selected then
        return nil, nil
    end
    return selected, string.format("spell:%d:%s", row, selected)
end

local function assist_menu_item()
    return ASSIST_MENU_ITEMS[state.assist_menu_index]
end

local function assist_setting_label(item)
    if not item then
        return nil
    end

    if item.id == "battles_enabled" then
        return state.assist_settings.battles_enabled and "On" or "Off"
    end

    if item.id == "critical_health_warnings_enabled" then
        return state.assist_settings.critical_health_warnings_enabled and "On" or "Off"
    end

    if item.id == "hazard_warnings_enabled" then
        return state.assist_settings.hazard_warnings_enabled and "On" or "Off"
    end

    if item.id == "pathfinding_filter_enabled" then
        return state.assist_settings.pathfinding_filter_enabled and "On" or "Off"
    end

    local option_index = state.assist_settings[item.id] or 1
    local option = item.values and item.values[option_index] or nil
    if type(option) == "table" then
        return option.label
    end
    return tostring(option or "")
end

local function format_assist_menu_item(item)
    if not item then
        return "No assist menu item."
    end
    return string.format("%s: %s.", item.label, assist_setting_label(item))
end

local function open_assist_menu()
    state.assist_menu_open = true
    if state.assist_menu_index < 1 or state.assist_menu_index > #ASSIST_MENU_ITEMS then
        state.assist_menu_index = 1
    end
end

local function close_assist_menu()
    if not state.assist_menu_open then
        return
    end
    state.assist_menu_open = false
    speak("Accessibility menu closed.", true)
end

local function announce_assist_menu_item()
    speak(format_assist_menu_item(assist_menu_item()), true)
end

local function cycle_assist_menu_selection(delta)
    if not state.assist_menu_open then
        return
    end
    local previous = state.assist_menu_index
    state.assist_menu_index = state.assist_menu_index + delta
    if state.assist_menu_index < 1 then
        state.assist_menu_index = 1
    elseif state.assist_menu_index > #ASSIST_MENU_ITEMS then
        state.assist_menu_index = #ASSIST_MENU_ITEMS
    end
    if state.assist_menu_index == previous then
        return
    end
    announce_assist_menu_item()
end

local function adjust_assist_menu_value(delta)
    local item = assist_menu_item()
    if not item then
        return
    end

    if item.id == "battles_enabled" then
        local previous = state.assist_settings[item.id]
        if delta > 0 then
            state.assist_settings[item.id] = true
        elseif delta < 0 then
            state.assist_settings[item.id] = false
        end
        if state.assist_settings[item.id] == previous then
            return
        end
        persist_assist_settings()
        speak(assist_setting_label(item) .. ".", true)
        return
    end

    if item.id == "critical_health_warnings_enabled"
        or item.id == "hazard_warnings_enabled"
        or item.id == "pathfinding_filter_enabled" then
        local previous = state.assist_settings[item.id]
        if delta > 0 then
            state.assist_settings[item.id] = true
        elseif delta < 0 then
            state.assist_settings[item.id] = false
        end
        if state.assist_settings[item.id] == previous then
            return
        end
        persist_assist_settings()
        speak(assist_setting_label(item) .. ".", true)
        return
    end

    local current = state.assist_settings[item.id] or 1
    local count = #(item.values or {})
    if count == 0 then
        return
    end

    local previous = current
    current = current + delta
    if current < 1 then
        current = 1
    elseif current > count then
        current = count
    end
    if current == previous then
        return
    end
    state.assist_settings[item.id] = current
    persist_assist_settings()
    speak(assist_setting_label(item) .. ".", true)
end

local function handle_assist_menu(keys)
    if keyboard_pressed(keys, "Delete") then
        if state.assist_menu_open then
            close_assist_menu()
        else
            open_assist_menu()
            speak(
                "Accessibility menu. Use the arrow keys to navigate, press escape to close. "
                    .. format_assist_menu_item(assist_menu_item()),
                true
            )
        end
        return true
    end

    if not state.assist_menu_open then
        return false
    end

    if keyboard_pressed(keys, "Escape") then
        close_assist_menu()
        return true
    end

    if any_pressed(keys, { "Down", "DpadDown", "X1 DpadDown", "P1 Down" }) then
        cycle_assist_menu_selection(1)
        return true
    end

    if any_pressed(keys, { "Up", "DpadUp", "X1 DpadUp", "P1 Up" }) then
        cycle_assist_menu_selection(-1)
        return true
    end

    if any_pressed(keys, { "Right", "DpadRight", "X1 DpadRight", "P1 Right" }) then
        adjust_assist_menu_value(1)
        return true
    end

    if any_pressed(keys, { "Left", "DpadLeft", "X1 DpadLeft", "P1 Left" }) then
        adjust_assist_menu_value(-1)
        return true
    end

    if keyboard_pressed(keys, "Insert") then
        announce_assist_menu_item()
        return true
    end

    return true
end

local function read_player_stats()
    return {
        level = mainmemory.read_u8(PLAYER_LEVEL_ADDR),
        hp = mainmemory.read_u8(PLAYER_HP_ADDR),
        mp = mainmemory.read_u8(PLAYER_MP_ADDR),
        max_hp = mainmemory.read_u8(PLAYER_MAX_HP_ADDR),
        max_mp = mainmemory.read_u8(PLAYER_MAX_MP_ADDR),
        experience = read_u16_le(EXP_ADDR),
        gold = read_u16_le(GOLD_ADDR),
    }
end

local function player_is_critical_health(stats)
    if not stats or not stats.hp or not stats.max_hp then
        return false
    end
    if stats.max_hp <= 0 then
        return false
    end
    return stats.hp <= (math.floor(stats.max_hp / 4) + 1)
end

local function update_critical_health_warning(blocked)
    if state.assist_settings.critical_health_warnings_enabled == false then
        state.critical_health_announced = false
        state.critical_health_pending = false
        return false
    end

    local stats = read_player_stats()
    local critical = player_is_critical_health(stats)

    if not critical then
        state.critical_health_announced = false
        state.critical_health_pending = false
        return false
    end

    if not state.critical_health_announced then
        state.critical_health_pending = true
    end

    if blocked then
        return false
    end

    local frame = emu.framecount()
    if state.critical_health_pending
        and frame - state.critical_health_last_frame >= CONFIG.critical_health_repeat_frames
    then
        speak("Warning: Critical health.", true)
        state.critical_health_announced = true
        state.critical_health_pending = false
        state.critical_health_last_frame = frame
        return true
    end

    return false
end

local function handle_shortcuts(keys)
    if handle_assist_menu(keys) then
        return
    end

    if keyboard_pressed(keys, "H") then
        local stats = read_player_stats()
        speak(
            string.format(
                "Level %d. HP %d of %d. MP %d of %d. Experience %d.",
                stats.level,
                stats.hp,
                stats.max_hp,
                stats.mp,
                stats.max_mp,
                stats.experience
            ),
            true
        )
    elseif keyboard_pressed(keys, "G") then
        local stats = read_player_stats()
        speak(string.format("%d gold.", stats.gold), true)
    end
end

local function current_gameplay_menu_signature(source)
    if not source or source == "" then
        return ""
    end
    return string.format("%02X:%02X:%04X:%s", read_window_col_pos(), read_window_row_pos(), read_window_data_ptr(), source)
end

local function read_non_gameplay_item()
    return read_opening_game_menu_item()
end

local function announce_non_gameplay_current(keys)
    local text, key, screen = read_non_gameplay_item()
    local menu_signature = current_menu_signature(screen)

    if menu_signature ~= state.last_menu_signature then
        state.last_menu_signature = menu_signature
        if menu_signature ~= "" then
            state.menu_settle_frames_left = CONFIG.menu_settle_frames
        else
            state.menu_settle_frames_left = 0
        end
    end

    if screen ~= state.last_screen then
        state.last_screen = screen or ""
        state.last_key = ""
        if screen == "name-entry" then
            speak("Enter a name. To review the name, press Home.", true)
            state.entry_delay_frames_left = CONFIG.name_entry_delay_frames
            state.last_key = key or ""
        elseif screen == "solo-new-quest" then
            speak("Begin a New Quest", true)
            state.last_key = key or ""
        elseif screen == "message-speed" then
            speak("Which message speed do you want?", true)
            state.entry_delay_frames_left = CONFIG.message_speed_delay_frames
            state.last_key = key or ""
        elseif screen == "adventure-log"
            and state.pending_action == "copy-source"
            and state.pending_source_slot ~= nil
            and state.pending_target_slot ~= nil then
            speak("Choose a destination.", true)
            state.entry_delay_frames_left = CONFIG.copy_destination_delay_frames
            state.last_key = ""
            state.pending_action = "copy-confirm"
        elseif screen == "yes-no" then
            local prompt = maybe_prompt_for_yes_no()
            if prompt then
                speak(prompt, true)
                state.entry_delay_frames_left = CONFIG.yes_no_prompt_delay_frames
                state.last_key = ""
            end
        elseif not screen then
            state.last_key = ""
            state.entry_delay_frames_left = 0
        end
    end

    if screen == "name-entry" and keyboard_pressed(keys, "Home") then
        speak(string.format("Entered name: %s", read_name_field()), true)
        state.last_key = key or ""
        return
    end

    if state.entry_delay_frames_left > 0 then
        state.entry_delay_frames_left = state.entry_delay_frames_left - 1
        return
    end

    if state.menu_settle_frames_left > 0 then
        state.menu_settle_frames_left = state.menu_settle_frames_left - 1
        return
    end

    if text and key ~= state.last_key then
        speak(text, true)
        state.last_key = key
    elseif not text then
        state.last_key = ""
    end
end

local function announce_current()
    if state.status_active then
        return
    end

    local text, key = read_battle_menu_item()
    local source = "battle"

    if not text then
        text, key = read_spell_menu_item()
        source = "spell-menu"
    end

    if not text then
        text, key = read_item_menu_item()
        source = "item-menu"
    end

    if not text then
        text, key = read_shop_inventory_item()
        source = "shop-menu"
    end

    if not text then
        text, key = read_yes_no_item()
        source = "yes-no"
    end

    if not text then
        text, key = read_command_menu_item()
        source = "command"
    end

    if not text then
        state.last_gameplay_menu_signature = ""
        state.gameplay_menu_settle_frames_left = 0
        if state.last_source ~= "" then
            state.last_source = ""
            state.command_entry_pending = false
            state.observed_key = ""
            state.observed_text = ""
            state.observed_frames = 0
            state.last_key = ""
        end
        return
    end

    local menu_signature = current_gameplay_menu_signature(source)
    if menu_signature ~= state.last_gameplay_menu_signature then
        state.last_gameplay_menu_signature = menu_signature
        state.gameplay_menu_settle_frames_left = CONFIG.menu_settle_frames
    end

    if source == "command" and state.command_window_entry_pending then
        state.last_source = ""
        state.command_window_entry_pending = false
    end

    if source == "spell-menu" and state.spell_window_entry_pending then
        state.last_source = ""
        state.spell_window_entry_pending = false
    end

    if source == "item-menu" and state.item_window_entry_pending then
        state.last_source = ""
        state.item_window_entry_pending = false
    end

    if source == "yes-no" and state.yes_no_window_entry_pending then
        state.last_source = ""
        state.yes_no_window_entry_pending = false
    end

    if state.last_source ~= source then
        state.last_source = source
        state.command_entry_pending = true
        if source == "battle" then
            state.entry_delay_frames_left = CONFIG.battle_entry_delay_frames
        elseif source == "shop-menu" then
            state.entry_delay_frames_left = CONFIG.shop_entry_delay_frames
        else
            state.entry_delay_frames_left = 0
        end
        state.observed_key = ""
        state.observed_text = ""
        state.observed_frames = 0
        state.last_key = ""
    end

    if key == state.observed_key and text == state.observed_text then
        state.observed_frames = state.observed_frames + 1
    else
        state.observed_key = key
        state.observed_text = text
        state.observed_frames = 1
    end

    if state.gameplay_menu_settle_frames_left > 0 then
        state.gameplay_menu_settle_frames_left = state.gameplay_menu_settle_frames_left - 1
        return
    end

    if state.command_entry_pending and state.entry_delay_frames_left > 0 then
        state.entry_delay_frames_left = state.entry_delay_frames_left - 1
        return
    end

    if state.observed_frames >= CONFIG.stable_frames and key ~= state.last_key then
        if state.command_entry_pending then
            state.command_entry_pending = false
            state.entry_delay_frames_left = 0
            if source == "battle" then
                speak(text)
            elseif source == "spell-menu" then
                speak(format_spell_announcement(text))
            elseif source == "item-menu" then
                speak(string.format("Item window: %s", text))
            elseif source == "shop-menu" then
                speak(text)
            elseif source == "yes-no" then
                speak(text)
            else
                speak(string.format("Command menu: %s", text))
            end
        else
            if source == "spell-menu" then
                speak(format_spell_announcement(text))
            else
                speak(text)
            end
        end
        state.last_key = key
    end
end

local function gameplay_menu_active()
    if state.command_window_open or state.spell_window_open or state.item_window_open or state.yes_no_window_open then
        return true
    end

    if read_window_col_pos() == 0x0C and read_window_row_pos() == 0x02 then
        return true
    end

    local wnd_data_ptr = read_window_data_ptr()
    return wnd_data_ptr == SHOP_INV_DATA_PTR
        or wnd_data_ptr == PLAYER_INV_DATA_PTR
        or wnd_data_ptr == BUY_SELL_DATA_PTR
        or wnd_data_ptr == YES_NO_1_DATA_PTR
end

speak("Dragon Warrior access loaded successfully.", true)

local function run_frame()
    local keys = _G.DWA_MENU_KEYS or input.get()
    load_assist_settings()
    sync_random_encounter_patch()
    update_reward_window()
    update_reward_modifiers()
    handle_shortcuts(keys)
    if state.assist_menu_open then
        suppress_gameplay_input()
    elseif state.status_active then
        suppress_directional_gameplay_input()
    end

    local command_window_open = (read_window_col_pos() == 0x0C and read_window_row_pos() == 0x02)
    if command_window_open ~= state.command_window_open then
        state.command_window_open = command_window_open
        if command_window_open then
            state.command_window_entry_pending = true
            state.command_cursor_settle_frames_left = CONFIG.command_cursor_settle_frames
            clear_current_announcement_state()
        else
            state.command_window_entry_pending = false
            state.command_cursor_settle_frames_left = 0
        end
    end

    local spell_open_now = spell_window_open()
    if spell_open_now then
        state.spell_window_seen_frames = state.spell_window_seen_frames + 1
        state.spell_window_missing_frames = 0
    else
        state.spell_window_missing_frames = state.spell_window_missing_frames + 1
        state.spell_window_seen_frames = 0
    end

    if not state.spell_window_open and state.spell_window_seen_frames >= CONFIG.spell_window_open_confirm_frames then
        state.spell_window_open = true
        state.spell_window_entry_pending = true
        state.spell_cursor_settle_frames_left = CONFIG.spell_cursor_settle_frames
        clear_current_announcement_state()
        speak("Spell window", true)
    elseif state.spell_window_open and state.spell_window_missing_frames >= CONFIG.spell_window_close_confirm_frames then
        state.spell_window_open = false
        speak("Spell window closed", true)
        clear_current_announcement_state()
        state.spell_window_entry_pending = false
        state.spell_cursor_settle_frames_left = 0
    end

    local item_open_now = item_window_open()
    if item_open_now then
        state.item_window_seen_frames = state.item_window_seen_frames + 1
        state.item_window_missing_frames = 0
    else
        state.item_window_missing_frames = state.item_window_missing_frames + 1
        state.item_window_seen_frames = 0
    end

    if not state.item_window_open and state.item_window_seen_frames >= CONFIG.item_window_open_confirm_frames then
        state.item_window_open = true
        state.item_window_entry_pending = true
        state.item_cursor_settle_frames_left = CONFIG.item_cursor_settle_frames
        clear_current_announcement_state()
    elseif state.item_window_open and state.item_window_missing_frames >= CONFIG.item_window_close_confirm_frames then
        state.item_window_open = false
        speak("Item window closed", true)
        clear_current_announcement_state()
        state.item_window_entry_pending = false
        state.item_cursor_settle_frames_left = 0
    end

    local yes_no_open_now = yes_no_window_open()
    if yes_no_open_now then
        state.yes_no_window_seen_frames = state.yes_no_window_seen_frames + 1
        state.yes_no_window_missing_frames = 0
    else
        state.yes_no_window_missing_frames = state.yes_no_window_missing_frames + 1
        state.yes_no_window_seen_frames = 0
    end

    if not state.yes_no_window_open and state.yes_no_window_seen_frames >= CONFIG.two_choice_open_confirm_frames then
        state.yes_no_window_open = true
        state.yes_no_window_entry_pending = true
        state.yes_no_cursor_settle_frames_left = CONFIG.two_choice_cursor_settle_frames
        clear_current_announcement_state()
    elseif state.yes_no_window_open and state.yes_no_window_missing_frames >= CONFIG.two_choice_close_confirm_frames then
        state.yes_no_window_open = false
        state.yes_no_window_entry_pending = false
        state.yes_no_cursor_settle_frames_left = 0
        clear_current_announcement_state()
    end

    if state.command_window_open and state.command_cursor_settle_frames_left > 0 then
        state.command_cursor_settle_frames_left = state.command_cursor_settle_frames_left - 1
    end

    if state.spell_window_open and state.spell_cursor_settle_frames_left > 0 then
        state.spell_cursor_settle_frames_left = state.spell_cursor_settle_frames_left - 1
    end

    if state.item_window_open and state.item_cursor_settle_frames_left > 0 then
        state.item_cursor_settle_frames_left = state.item_cursor_settle_frames_left - 1
    end

    if state.yes_no_window_open and state.yes_no_cursor_settle_frames_left > 0 then
        state.yes_no_cursor_settle_frames_left = state.yes_no_cursor_settle_frames_left - 1
    end

    local _, _, opening_screen = read_opening_game_menu_item()
    if opening_screen then
        if not state.assist_menu_open then
            announce_non_gameplay_current(keys)
        end
    else
        local status_active = update_status_window(keys)
        local dialog_wait_active = update_dialog_wait_live()
        local save_warning_active = update_save_warning_live(dialog_wait_active)
        local gameplay_active = gameplay_menu_active()
        local critical_health_active = update_critical_health_warning(
            state.assist_menu_open or status_active or dialog_wait_active or save_warning_active or gameplay_active
        )
        if state.assist_menu_open then
            -- Keep the assist menu quiet and independent from gameplay announcements.
        elseif gameplay_active and not critical_health_active then
            announce_current()
        elseif not status_active and not dialog_wait_active and not save_warning_active and not critical_health_active then
            announce_non_gameplay_current(keys)
        end
    end
    state.last_keyboard = keys
    state.last_controller_0047 = mainmemory.read_u8(0x0047)
end

local function toggle_assist_menu()
    if state.assist_menu_open then
        close_assist_menu()
    else
        open_assist_menu()
        speak(
            "Accessibility menu. Use the arrow keys to navigate, press escape to close. "
                .. format_assist_menu_item(assist_menu_item()),
            true
        )
    end
end

local function speak_status_summary()
    local stats = read_player_stats()
    speak(
        string.format(
            "Level %d. HP %d of %d. MP %d of %d. Experience %d.",
            stats.level,
            stats.hp,
            stats.max_hp,
            stats.mp,
            stats.max_mp,
            stats.experience
        ),
        true
    )
end

local function speak_gold_amount()
    local stats = read_player_stats()
    speak(string.format("%d gold.", stats.gold), true)
end

return {
    run_frame = run_frame,
    toggle_assist_menu = toggle_assist_menu,
    speak_status_summary = speak_status_summary,
    speak_gold_amount = speak_gold_amount,
    is_non_gameplay_active = function()
        local _, _, screen = read_non_gameplay_item()
        return screen ~= nil
    end,
    is_assist_menu_open = function()
        return state.assist_menu_open
    end,
}






































