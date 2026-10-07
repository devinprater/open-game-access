local M = {}

local MODE = {
    file_select = 0x01,
    register = 0x0E,
    elimination = 0x0F,
}

local ADDR = {
    game_mode = 0x0012,
    current_selection = 0x0016,
    slot_active_base = 0x0633,
    names_base = 0x0638,
    save_slot_hearts_base = 0x0650,
    quest_numbers_base = 0x062D,
    death_counts_base = 0x0630,
    char_board_index = 0x041F,
    initialized_name_field = 0x0420,
    name_char_offset = 0x0421,
}

local CONFIG = {
    review_hotkey = "Home",
    menu_hotkey = "Delete",
    screen_title_delay_frames = 30,
}

local CHAR_MAP = {
    [0x00] = "0", [0x01] = "1", [0x02] = "2", [0x03] = "3", [0x04] = "4",
    [0x05] = "5", [0x06] = "6", [0x07] = "7", [0x08] = "8", [0x09] = "9",
    [0x0A] = "A", [0x0B] = "B", [0x0C] = "C", [0x0D] = "D", [0x0E] = "E",
    [0x0F] = "F", [0x10] = "G", [0x11] = "H", [0x12] = "I", [0x13] = "J",
    [0x14] = "K", [0x15] = "L", [0x16] = "M", [0x17] = "N", [0x18] = "O",
    [0x19] = "P", [0x1A] = "Q", [0x1B] = "R", [0x1C] = "S", [0x1D] = "T",
    [0x1E] = "U", [0x1F] = "V", [0x20] = "W", [0x21] = "X", [0x22] = "Y",
    [0x23] = "Z", [0x24] = " ", [0x28] = ",", [0x29] = "!", [0x2A] = "'",
    [0x2B] = "&", [0x2C] = ".", [0x62] = "-", [0x63] = "?",
}

local MODE_E_CHAR_MAP = {
    [0] = 0x0A, [1] = 0x0B, [2] = 0x0C, [3] = 0x0D, [4] = 0x0E, [5] = 0x0F, [6] = 0x10, [7] = 0x11,
    [8] = 0x12, [9] = 0x13, [10] = 0x14, [11] = 0x15, [12] = 0x16, [13] = 0x17, [14] = 0x18, [15] = 0x19,
    [16] = 0x1A, [17] = 0x1B, [18] = 0x1C, [19] = 0x1D, [20] = 0x1E, [21] = 0x1F, [22] = 0x20, [23] = 0x21,
    [24] = 0x22, [25] = 0x23, [26] = 0x62, [27] = 0x63, [28] = 0x28, [29] = 0x29, [30] = 0x2A, [31] = 0x2B,
    [32] = 0x2C, [33] = 0x00, [34] = 0x01, [35] = 0x02, [36] = 0x03, [37] = 0x04, [38] = 0x05, [39] = 0x06,
    [40] = 0x07, [41] = 0x08, [42] = 0x09, [43] = 0x24,
}

local state = {
    last_signature = nil,
    last_mode = nil,
    last_register_selection = nil,
    last_register_board_index = nil,
    last_screen_title = nil,
    screen_title_cooldown = 0,
    menu_open = false,
    menu_index = 1,
    -- Frames remaining to keep input blocked AFTER the menu closes.
    -- Without this, the close-key (Enter/Escape) is still physically
    -- held on the frame after close and the game registers it as a
    -- Start press / opens the inventory.
    input_block_frames = 0,
}

local menu_items = {
    {
        key = "coordinate_format",
        label = "Overworld coordinate format",
        kind = "choice",
        choices = {
            { value = "numeric", label = "Numeric" },
            { value = "spreadsheet", label = "Spreadsheet" },
        },
    },
    {
        key = "dungeon_coordinate_format",
        label = "Dungeon coordinate format",
        kind = "choice",
        choices = {
            { value = "numeric", label = "Numeric" },
            { value = "spreadsheet", label = "Spreadsheet" },
        },
    },
    { key = "footsteps_enabled", label = "Footsteps", kind = "toggle" },
    { key = "footstep_volume", label = "Footsteps volume", kind = "volume" },
    { key = "wall_bumps_enabled", label = "Wall bumps", kind = "toggle" },
    { key = "wall_bump_volume", label = "Wall bump volume", kind = "volume" },
    { key = "item_beacon_enabled", label = "Item beacons", kind = "toggle" },
    { key = "item_beacon_volume", label = "Item beacon volume", kind = "volume" },
    {
        key = "item_drop_linger_mode",
        label = "Item drop linger",
        kind = "choice",
        choices = {
            { value = "normal", label = "Normal" },
            { value = "x2", label = "Times 2" },
            { value = "x4", label = "Times 4" },
            { value = "never", label = "Never" },
        },
    },
    { key = "enemy_radar_enabled", label = "Enemy radar", kind = "toggle" },
    { key = "enemy_radar_volume", label = "Enemy radar volume", kind = "volume" },
    { key = "enemy_auto_lock_enabled", label = "Enemy auto lock", kind = "toggle" },
    { key = "freeze_enemies_enabled", label = "Freeze enemies", kind = "toggle" },
    -- The three suppression toggles below hide themselves while
    -- freeze_enemies is on. When freeze is active the engine kills
    -- boulders, fire, AND Wall Masters as a soft-lock guard, so
    -- exposing the standalone toggles for those same hazards would
    -- just confuse things -- their state has no effect. Showing them
    -- only when freeze is off keeps the menu honest.
    {
        key = "suppress_boulders_enabled",
        label = "Suppress boulders",
        kind = "toggle",
        visible_when = function(s) return not s.freeze_enemies_enabled end,
    },
    {
        key = "suppress_fire_enabled",
        label = "Suppress fire",
        kind = "toggle",
        visible_when = function(s) return not s.freeze_enemies_enabled end,
    },
    {
        key = "suppress_wall_master_enabled",
        label = "Suppress Wall Masters",
        kind = "toggle",
        visible_when = function(s) return not s.freeze_enemies_enabled end,
    },
}

-- Returns true if a menu item should be shown given current settings.
-- Items without a visible_when predicate are always shown.
local function item_visible(item, settings)
    if not item.visible_when then return true end
    return item.visible_when(settings) == true
end

-- Build a list of currently-visible item indices into menu_items, in
-- order. The menu's navigation and rendering iterate over this list
-- so hidden items are completely invisible to Up/Down/Left/Right.
local function visible_indices(settings)
    local out = {}
    for i, item in ipairs(menu_items) do
        if item_visible(item, settings) then
            out[#out + 1] = i
        end
    end
    return out
end

local function choice_label(item, current_value)
    for _, choice in ipairs(item.choices or {}) do
        if choice.value == current_value then
            return choice.label
        end
    end
    return tostring(current_value)
end

local function decode_name(ctx, slot_index)
    local start_address = ADDR.names_base + (slot_index * 8)
    local chars = {}
    for offset = 0, 7 do
        local value = ctx.read_u8(start_address + offset)
        chars[#chars + 1] = CHAR_MAP[value] or string.format("<%02X>", value)
    end
    return table.concat(chars):gsub("%s+$", "")
end

local function slot_heart_bytes(ctx, slot_index)
    local base = ADDR.save_slot_hearts_base + (slot_index * 2)
    return ctx.read_u8(base), ctx.read_u8(base + 1)
end

local function slot_heart_count(ctx, slot_index)
    local heart_values, heart_partial = slot_heart_bytes(ctx, slot_index)
    local encoded_count = math.floor(heart_values / 16)
    if heart_values == 0 then
        return 0, false
    end

    local containers = encoded_count + 1
    if heart_partial == 0 then
        return containers - 1, false
    elseif heart_partial < 0x80 then
        return containers - 1, true
    end
    return containers, false
end

local function hearts_phrase(ctx, slot_index)
    local hearts, has_half = slot_heart_count(ctx, slot_index)
    if has_half then
        if hearts == 0 then
            return "half a heart"
        end
        return string.format("%d and a half hearts", hearts)
    end
    if hearts == 1 then
        return "1 heart"
    end
    return string.format("%d hearts", hearts)
end

local function selection_label(selection)
    if selection == 0 then return "File 1"
    elseif selection == 1 then return "File 2"
    elseif selection == 2 then return "File 3"
    elseif selection == 3 then return "Register your name"
    elseif selection == 4 then return "Elimination mode" end
    return string.format("Unknown selection %d", selection)
end

local function screen_title(mode)
    if mode == MODE.file_select then return "File select"
    elseif mode == MODE.register then return "Register your name"
    elseif mode == MODE.elimination then return "Elimination mode" end
    return nil
end

local function register_slot_label(selection)
    if selection == 0 then return "File 1"
    elseif selection == 1 then return "File 2"
    elseif selection == 2 then return "File 3"
    elseif selection == 3 then return "End" end
    return string.format("Unknown selection %d", selection)
end

local function register_character_label(ctx, board_index)
    local selected = board_index or ctx.read_u8(ADDR.char_board_index)
    local map_value = MODE_E_CHAR_MAP[selected]
    if map_value == nil then
        return string.format("Character index %d", selected)
    end

    local decoded = CHAR_MAP[map_value] or string.format("<%02X>", map_value)
    local replacements = {
        [" "] = "space",
        ["-"] = "hyphen",
        ["?"] = "question mark",
        [","] = "comma",
        ["!"] = "exclamation mark",
        ["'"] = "apostrophe",
        ["&"] = "ampersand",
        ["."] = "period",
    }
    return replacements[decoded] or decoded
end

local function file_review_message(ctx, slot_index, options)
    options = options or {}
    local active = ctx.read_u8(ADDR.slot_active_base + slot_index)
    if active == 0 and not options.ignore_active_flag then
        return string.format("%s is empty.", selection_label(slot_index))
    end

    local name = decode_name(ctx, slot_index)
    if name == "" then
        name = "blank"
    end
    -- Quest is stored 0-indexed in RAM (0 = Quest 1, 1 = Quest 2)
    -- so we add 1 for the spoken form. Death count is a single
    -- byte per slot at $0630-$0632.
    local quest = ctx.read_u8(ADDR.quest_numbers_base + slot_index) + 1
    local deaths = ctx.read_u8(ADDR.death_counts_base + slot_index)
    local death_word = (deaths == 1) and "death" or "deaths"
    return string.format("%s, %s, %s, Quest %d, %d %s.",
        selection_label(slot_index), name, hearts_phrase(ctx, slot_index),
        quest, deaths, death_word)
end

local function selection_message(ctx)
    local selection = ctx.read_u8(ADDR.current_selection)
    if selection <= 2 then
        return file_review_message(ctx, selection)
    end
    return selection_label(selection)
end

local function elimination_message(ctx)
    local selection = ctx.read_u8(ADDR.current_selection)
    if selection <= 2 then
        return file_review_message(ctx, selection)
    end
    return register_slot_label(selection)
end

local function current_signature(ctx)
    local selection = ctx.read_u8(ADDR.current_selection)
    local mode = ctx.read_u8(ADDR.game_mode)

    if mode == MODE.file_select or mode == MODE.elimination then
        if selection <= 2 then
            local active = ctx.read_u8(ADDR.slot_active_base + selection)
            local name = decode_name(ctx, selection)
            local heart_values, heart_partial = slot_heart_bytes(ctx, selection)
            return table.concat({ mode, selection, active, name, heart_values, heart_partial }, "|")
        end
        return table.concat({ mode, selection }, "|")
    end

    if mode == MODE.register then
        local board_index = ctx.read_u8(ADDR.char_board_index)
        local initialized = ctx.read_u8(ADDR.initialized_name_field)
        local name_offset = ctx.read_u8(ADDR.name_char_offset)
        return table.concat({ mode, selection, board_index, initialized, name_offset }, "|")
    end

    return nil
end

local function current_item_phrase(settings)
    local item = menu_items[state.menu_index]
    if item.kind == "toggle" then
        return string.format("%s %s", item.label, settings[item.key] and "On" or "Off")
    elseif item.kind == "choice" then
        return string.format("%s %s", item.label, choice_label(item, settings[item.key]))
    end
    return string.format("%s %d", item.label, settings[item.key])
end

local function current_value_phrase(settings)
    local item = menu_items[state.menu_index]
    if item.kind == "toggle" then
        return settings[item.key] and "On" or "Off"
    elseif item.kind == "choice" then
        return choice_label(item, settings[item.key])
    end
    return tostring(settings[item.key])
end

local function announce_menu_item(ctx)
    ctx.write_speech(current_item_phrase(ctx.settings))
end

local function toggle_or_adjust_current(ctx, delta)
    local item = menu_items[state.menu_index]
    local settings = ctx.settings
    local changed = false

    if item.kind == "toggle" then
        local new_value = delta >= 0
        changed = settings[item.key] ~= new_value
        settings[item.key] = new_value
    elseif item.kind == "choice" then
        local choices = item.choices or {}
        local current_index = 1
        for index, choice in ipairs(choices) do
            if choice.value == settings[item.key] then
                current_index = index
                break
            end
        end
        local new_index = current_index + delta
        if new_index < 1 then new_index = 1 end
        if new_index > #choices then new_index = #choices end
        changed = new_index ~= current_index
        settings[item.key] = choices[new_index].value
    else
        local old_value = settings[item.key]
        local value = old_value + (delta * 10)
        if value < 10 then value = 10 end
        if value > 100 then value = 100 end
        changed = value ~= old_value
        settings[item.key] = value
    end

    if not changed then
        return
    end

    ctx.save_settings(settings)
    ctx.write_speech(current_value_phrase(settings))
end

-- Z1 game-internal pause flag. Writing 1 here pauses the game the same
-- way pressing Start does -- player physics, enemies, animations all
-- stop, but our Lua frame loop keeps running so we can still detect
-- the close hotkey. Writing 0 unpauses.
local ADDR_GAME_PAUSED = 0x00E0

local function update_accessibility_menu(ctx)
    if ctx.just_pressed(CONFIG.menu_hotkey)
            or (state.menu_open and ctx.just_pressed("Escape")) then
        state.menu_open = not state.menu_open
        if state.menu_open then
            mainmemory.write_u8(ADDR_GAME_PAUSED, 1)
            ctx.write_speech("Accessibility menu. " .. current_item_phrase(ctx.settings))
        else
            mainmemory.write_u8(ADDR_GAME_PAUSED, 0)
            ctx.write_speech("Accessibility menu closed.")
        end
        return true
    end

    if not state.menu_open then
        return false
    end

    -- While the menu is open, keep writing 1 to the pause flag every
    -- frame. This guards against the game clearing it on its own (e.g.
    -- if the player presses Start through some path we missed, the
    -- engine would set it back to 0).
    mainmemory.write_u8(ADDR_GAME_PAUSED, 1)

    if ctx.just_pressed("Up") then
        -- Navigate ONLY through visible items. Find current item's
        -- position in the visible list, step back, wrap.
        local visible = visible_indices(ctx.settings)
        if #visible > 0 then
            local pos = 1
            for i, idx in ipairs(visible) do
                if idx == state.menu_index then pos = i; break end
            end
            pos = ((pos - 2) % #visible) + 1
            state.menu_index = visible[pos]
            announce_menu_item(ctx)
        end
    elseif ctx.just_pressed("Down") then
        local visible = visible_indices(ctx.settings)
        if #visible > 0 then
            local pos = 1
            for i, idx in ipairs(visible) do
                if idx == state.menu_index then pos = i; break end
            end
            pos = (pos % #visible) + 1
            state.menu_index = visible[pos]
            announce_menu_item(ctx)
        end
    elseif ctx.just_pressed("Left") then
        toggle_or_adjust_current(ctx, -1)
    elseif ctx.just_pressed("Right") then
        toggle_or_adjust_current(ctx, 1)
    end

    -- If the cursor landed on a now-hidden item (e.g. user toggled
    -- freeze on while sitting on "Suppress fire"), snap back to the
    -- freeze toggle so the next Up/Down has somewhere sensible to go.
    if not item_visible(menu_items[state.menu_index], ctx.settings) then
        local visible = visible_indices(ctx.settings)
        if #visible > 0 then state.menu_index = visible[#visible] end
    end

    gui.text(8, 56, "Accessibility Menu")
    -- Render only visible items, packed top-down with no gaps so the
    -- on-screen list matches what Up/Down navigates through.
    local visible = visible_indices(ctx.settings)
    for row, idx in ipairs(visible) do
        local item = menu_items[idx]
        local prefix = idx == state.menu_index and ">" or " "
        local text
        if item.kind == "toggle" then
            text = string.format("%s %s: %s", prefix, item.label, ctx.settings[item.key] and "On" or "Off")
        elseif item.kind == "choice" then
            text = string.format("%s %s: %s", prefix, item.label, choice_label(item, ctx.settings[item.key]))
        else
            text = string.format("%s %s: %d", prefix, item.label, ctx.settings[item.key])
        end
        gui.text(8, 56 + (row * 16), text)
    end

    return true
end

function M.is_accessibility_open()
    return state.menu_open
end

function M.update(ctx)
    if update_accessibility_menu(ctx) then
        return
    end

    local mode = ctx.read_u8(ADDR.game_mode)
    local selection = ctx.read_u8(ADDR.current_selection)
    local title = screen_title(mode)

    if mode ~= state.last_mode then
        state.last_mode = mode
        state.last_signature = nil
        state.last_register_selection = nil
        state.last_register_board_index = nil
    end

    if title ~= state.last_screen_title then
        state.last_screen_title = title
        if title then
            ctx.write_speech(title)
            state.screen_title_cooldown = CONFIG.screen_title_delay_frames
        end
    end

    if state.screen_title_cooldown > 0 then
        state.screen_title_cooldown = state.screen_title_cooldown - 1
    end

    if mode == MODE.file_select then
        local signature = current_signature(ctx)
        if signature and signature ~= state.last_signature and state.screen_title_cooldown == 0 then
            state.last_signature = signature
            ctx.write_speech(selection_message(ctx))
        end
    elseif mode == MODE.register then
        local board_index = ctx.read_u8(ADDR.char_board_index)
        local initialized = ctx.read_u8(ADDR.initialized_name_field)
        local signature = current_signature(ctx)
        if signature and signature ~= state.last_signature and state.screen_title_cooldown == 0 then
            state.last_signature = signature
            if selection ~= state.last_register_selection then
                ctx.write_speech(register_slot_label(selection))
            elseif selection == 3 then
                ctx.write_speech(register_slot_label(selection))
            elseif initialized ~= 0 and board_index ~= state.last_register_board_index then
                ctx.write_speech(register_character_label(ctx, board_index))
            end
        end
        state.last_register_selection = selection
        state.last_register_board_index = board_index
    elseif mode == MODE.elimination then
        local signature = current_signature(ctx)
        if signature and signature ~= state.last_signature and state.screen_title_cooldown == 0 then
            state.last_signature = signature
            ctx.write_speech(elimination_message(ctx))
        end
    end

    if ctx.just_pressed(CONFIG.review_hotkey) then
        if mode == MODE.file_select and selection <= 2 then
            ctx.write_speech(file_review_message(ctx, selection))
        elseif mode == MODE.register and selection <= 2 then
            ctx.write_speech(file_review_message(ctx, selection, { ignore_active_flag = true }))
        elseif mode == MODE.elimination and selection <= 2 then
            ctx.write_speech(file_review_message(ctx, selection))
        end
    end
end

return M
