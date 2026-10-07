local SCRIPT_PATH = debug.getinfo(1, "S").source:sub(2)
local ROOT_DIR = SCRIPT_PATH:match("^(.*)[/\\][^/\\]+$") or "."
ROOT_DIR = ROOT_DIR:gsub("\\", "/")
local DATA_DIR = ROOT_DIR .. "/Data"

local SPEECH_FILE = DATA_DIR .. "/wL5cF9pH7jXdQ.txt"
local SOUND_COMMAND_FILE = DATA_DIR .. "/sound_bridge_command.txt"
local SETTINGS_FILE = DATA_DIR .. "/Kx7m9pQ2zR4nB.lua"
local CRASH_LOG_FILE = DATA_DIR .. "/runtime_crash.log"

local ADDR = {
    cur_level = 0x0010,
    room_id = 0x00EB,
    game_mode = 0x0012,
    ladder_slot = 0x0064,  -- non-zero = slot index of active ladder object
    bombs = 0x0658,
    rupees = 0x066D,
    keys = 0x066E,
    heart_values = 0x066F,
    heart_partial = 0x0670,
    selected_item_slot = 0x0656,
    items_base = 0x0657,
    bow = 0x065A,
    candle = 0x065B,
    food = 0x065D,
    potion = 0x065E,
    boomerang = 0x0674,
    magic_boomerang = 0x0675,
    triforce_pieces = 0x0671,  -- bitmask, bit N = dungeon N+1 cleared
    map_flags = 0x0668,        -- dungeon maps owned (bitmask)
    compass_flags = 0x0667,    -- dungeon compasses owned (bitmask)
    obj_x_base = 0x0070,
    obj_y_base = 0x0084,
    obj_dir_base = 0x0098,
    obj_shove_dir_base = 0x00C0,
    obj_shove_distance_base = 0x00D3,
    obj_type_base = 0x034F,
    obj_pos_frac_base = 0x03A8,
    obj_input_dir_base = 0x03F8,
    active_boulders = 0x0515,
    play_area_tiles = 0x6530,
}

local DROP_ITEM_OBJ_TYPE = 0x60
local PUSH_BLOCK_OBJ_TYPE = 0x68  -- tile object, not an enemy. Freezing it pins it in place and breaks pushing.
local GRAVESTONE_OBJ_TYPE = 0x65  -- pushable gravestone (e.g. C2 Magical Sword Cave reveal). Same issue as push block.
-- Reveal targets that share enemy slots. Freezing any of these can
-- break the player's ability to interact with them via the intended
-- mechanic (push for rocks/gravestones, bomb for walls, burn for
-- trees). Even targets that don't physically move (rock wall, tree)
-- get skipped to be safe -- their state byte changes when destroyed,
-- and we don't want to risk pinning anything that the game might
-- update mid-interaction.
local REVEAL_TARGET_TYPES = {
    [0x62] = true,  -- pushable rock (needs Power Bracelet)
    [0x63] = true,  -- bombable rock wall
    [0x64] = true,  -- burnable tree
    [0x65] = true,  -- pushable gravestone (also tracked via GRAVESTONE_OBJ_TYPE)
}

-- Object types that get DELETED (zeroed) when freeze_enemies is on,
-- not just position-pinned. Pinning a Wall Master in place is a soft-
-- lock risk: if Link spawns inside one's grab radius, the freeze
-- holds it on top of him and there's no way to walk away because
-- it's blocking the tile. Boulders and fire have the same problem --
-- a frozen boulder mid-air or a frozen fire tile in a doorway
-- creates a permanent obstacle. Cleaner to delete these types
-- entirely while freeze is active so the player can navigate the
-- room without surprises. Independent of suppress_boulders /
-- suppress_fire toggles, which work the same way but can be enabled
-- without freezing everything else.
local FREEZE_KILL_TYPES = {
    [0x1F] = true,  -- boulder spawner (Death Mountain)
    [0x20] = true,  -- boulder projectile
    [0x27] = true,  -- Wallmaster
    [0x3F] = true,  -- GuardFire
    [0x40] = true,  -- StandingFire
}

local FILE_SCREEN_MODES = {
    [0x00] = true,  -- Title/transitory
    [0x01] = true,  -- Selection Screen
    [0x0E] = true,  -- Registration
    [0x0F] = true,  -- Elimination
}

-- Death/save/retry screen state.
--
-- This is actually TWO distinct screens that look continuous:
--
--   1. GAME OVER screen (GameMode 0x11): heart-drain animation,
--      "GAME OVER" text and fanfare. No menu interaction here.
--
--   2. Continue/Save/Retry menu (GameMode 0x08, post-death): the
--      actual interactive menu. The cursor position IS the submode
--      -- $0013 holds:
--        0 = Continue, 1 = Save, 2 = Retry
--      Pressing Select advances 0->1->2->0 cyclically. Pressing
--      Start commits and submode jumps to 0x80, then game loads.
--
-- We treat "in the death menu" as: mode 0x08 AND we recently came
-- from mode 0x11. That's how we distinguish post-death menu from
-- normal mode 0x08 (which also occurs during gameplay transitions).
local GAME_OVER_MODE = 0x11
local POST_DEATH_MENU_MODE = 0x08
local CURSOR_LABELS = { [0]="Continue", [1]="Save", [2]="Retry" }
local GAME_OVER_SPEECH_DELAY = 30  -- frames to wait before announcing
local game_over_pending = false
local game_over_speech_frame = -1
local game_over_announced = false
local in_death_menu = false
local death_menu_announced = false
local death_menu_last_cursor = -1

local ENDING_PEACE_CHAR_INDEX = 0x0413
local ENDING_TEXTBOX_CHAR_PTR_LO = 0x045F
local ENDING_FLASH_LONG_TIMER = 0x004D
local prev_peace_char_index = 0
local prev_textbox_char_ptr_lo = 0
local ending_zelda_speech_announced = false
local ending_credits_announced = false
local ending_the_end_announced = false

local DEFAULT_SETTINGS = {
    coordinate_format = "numeric",
    dungeon_coordinate_format = "numeric",
    footsteps_enabled = true,
    footstep_volume = 70,
    wall_bumps_enabled = true,
    wall_bump_volume = 80,
    enemy_radar_enabled = true,
    enemy_radar_volume = 90,
    item_beacon_enabled = true,
    item_beacon_volume = 80,
    item_drop_linger_mode = "normal",
    enemy_auto_lock_enabled = true,
    freeze_enemies_enabled = false,
    suppress_boulders_enabled = false,
    suppress_fire_enabled = false,
    suppress_wall_master_enabled = false,
}

local function normalize_volume(value)
    value = math.floor(((tonumber(value) or 10) / 10) + 0.5) * 10
    if value < 10 then value = 10 end
    if value > 100 then value = 100 end
    return value
end

local function normalize_settings(settings)
    settings = settings or {}
    local linger_mode = tostring(settings.item_drop_linger_mode or DEFAULT_SETTINGS.item_drop_linger_mode)
    if linger_mode ~= "normal" and linger_mode ~= "x2" and linger_mode ~= "x4" and linger_mode ~= "never" then
        linger_mode = DEFAULT_SETTINGS.item_drop_linger_mode
    end
    local coord_format = tostring(settings.coordinate_format or DEFAULT_SETTINGS.coordinate_format)
    if coord_format ~= "numeric" and coord_format ~= "spreadsheet" then
        coord_format = DEFAULT_SETTINGS.coordinate_format
    end
    -- Same validation for the dungeon-specific format. Falls back to
    -- the overworld value if missing entirely (legacy save files
    -- written before this setting existed) so behavior stays
    -- consistent rather than reverting to the hardcoded default.
    local dungeon_format = tostring(
        settings.dungeon_coordinate_format
        or settings.coordinate_format
        or DEFAULT_SETTINGS.dungeon_coordinate_format)
    if dungeon_format ~= "numeric" and dungeon_format ~= "spreadsheet" then
        dungeon_format = DEFAULT_SETTINGS.dungeon_coordinate_format
    end
    return {
        coordinate_format = coord_format,
        dungeon_coordinate_format = dungeon_format,
        footsteps_enabled = settings.footsteps_enabled ~= false,
        footstep_volume = normalize_volume(settings.footstep_volume or DEFAULT_SETTINGS.footstep_volume),
        wall_bumps_enabled = settings.wall_bumps_enabled ~= false,
        wall_bump_volume = normalize_volume(settings.wall_bump_volume or DEFAULT_SETTINGS.wall_bump_volume),
        enemy_radar_enabled = settings.enemy_radar_enabled ~= false,
        enemy_radar_volume = normalize_volume(settings.enemy_radar_volume or DEFAULT_SETTINGS.enemy_radar_volume),
        item_beacon_enabled = settings.item_beacon_enabled ~= false,
        item_beacon_volume = normalize_volume(settings.item_beacon_volume or DEFAULT_SETTINGS.item_beacon_volume),
        item_drop_linger_mode = linger_mode,
        enemy_auto_lock_enabled = settings.enemy_auto_lock_enabled ~= false,
        freeze_enemies_enabled = settings.freeze_enemies_enabled == true,
        suppress_boulders_enabled = settings.suppress_boulders_enabled == true,
        suppress_fire_enabled = settings.suppress_fire_enabled == true,
        suppress_wall_master_enabled = settings.suppress_wall_master_enabled == true,
    }
end

local function save_settings(settings)
    settings = normalize_settings(settings)
    local file = io.open(SETTINGS_FILE, "w")
    if not file then
        console.log("[zelda1] failed to save settings")
        return settings
    end
    file:write("return {\n")
    file:write(string.format("    coordinate_format = %q,\n", settings.coordinate_format))
    file:write(string.format("    dungeon_coordinate_format = %q,\n", settings.dungeon_coordinate_format))
    file:write(string.format("    footsteps_enabled = %s,\n", settings.footsteps_enabled and "true" or "false"))
    file:write(string.format("    footstep_volume = %d,\n", settings.footstep_volume))
    file:write(string.format("    wall_bumps_enabled = %s,\n", settings.wall_bumps_enabled and "true" or "false"))
    file:write(string.format("    wall_bump_volume = %d,\n", settings.wall_bump_volume))
    file:write(string.format("    enemy_radar_enabled = %s,\n", settings.enemy_radar_enabled and "true" or "false"))
    file:write(string.format("    enemy_radar_volume = %d,\n", settings.enemy_radar_volume))
    file:write(string.format("    item_beacon_enabled = %s,\n", settings.item_beacon_enabled and "true" or "false"))
    file:write(string.format("    item_beacon_volume = %d,\n", settings.item_beacon_volume))
    file:write(string.format("    item_drop_linger_mode = %q,\n", settings.item_drop_linger_mode))
    file:write(string.format("    enemy_auto_lock_enabled = %s,\n", settings.enemy_auto_lock_enabled and "true" or "false"))
    file:write(string.format("    freeze_enemies_enabled = %s,\n", settings.freeze_enemies_enabled and "true" or "false"))
    file:write(string.format("    suppress_boulders_enabled = %s,\n", settings.suppress_boulders_enabled and "true" or "false"))
    file:write(string.format("    suppress_fire_enabled = %s,\n", settings.suppress_fire_enabled and "true" or "false"))
    file:write(string.format("    suppress_wall_master_enabled = %s,\n", settings.suppress_wall_master_enabled and "true" or "false"))
    file:write("}\n")
    file:close()
    return settings
end

local function load_settings()
    local chunk = loadfile(SETTINGS_FILE)
    if not chunk then return save_settings(DEFAULT_SETTINGS) end
    local ok, data = pcall(chunk)
    if not ok or type(data) ~= "table" then return save_settings(DEFAULT_SETTINGS) end
    return normalize_settings(data)
end

local sequence = 0
local sound_sequence = 0
local previous_keys = {}
local settings = load_settings()

local menus = assert(loadfile(DATA_DIR .. "/eZ4cF8mY3rDpL.lua"))()
local navigation = assert(loadfile(DATA_DIR .. "/nT9wK2bX6jHsR.lua"))()

local function read_u8(address)
    return mainmemory.read_u8(address) or 0
end

local function write_u8(address, value)
    mainmemory.write_u8(address, value)
end

local function write_speech(message)
    if not message or message == "" then return end
    local file = io.open(SPEECH_FILE, "w")
    if not file then
        console.log("[zelda1] failed to open speech file")
        return
    end
    sequence = sequence + 1
    file:write(string.format("%d|%s", sequence, tostring(message)))
    -- Explicit flush before close. Lua's io.close() should flush
    -- implicitly, but on Windows the OS file cache can still hold
    -- the bytes briefly before committing to disk -- which means
    -- NVDA's polling reader might see stale content for a frame or
    -- two. flush() forces the OS write through immediately.
    file:flush()
    file:close()
end

local function append_crash_log(message)
    local file = io.open(CRASH_LOG_FILE, "a")
    if not file then
        console.log("[zelda1] failed to open crash log")
        return
    end
    file:write(os.date("[%Y-%m-%d %H:%M:%S] "))
    file:write(tostring(message or ""))
    file:write("\n")
    file:close()
end

local function emit_sound_command(command, ...)
    local file = io.open(SOUND_COMMAND_FILE, "a")
    if not file then
        console.log("[zelda1] failed to open sound command file")
        return
    end
    local parts = { tostring(command) }
    for index = 1, select("#", ...) do
        parts[#parts + 1] = tostring(select(index, ...))
    end
    sound_sequence = sound_sequence + 1
    file:write(string.format("%d|%s\n", sound_sequence, table.concat(parts, "|")))
    file:close()
end

local function just_pressed(keys, key_name)
    return keys[key_name] and not previous_keys[key_name]
end

local function shift_held(keys)
    return keys.LeftShift
        or keys.RightShift
        or keys.Shift
        or keys.ShiftKey
        or keys.LShiftKey
        or keys.RShiftKey
end

local function just_pressed_without_shift(keys, key_name)
    return just_pressed(keys, key_name) and not shift_held(keys)
end

local function just_pressed_any(keys, key_names)
    for _, key_name in ipairs(key_names) do
        if just_pressed(keys, key_name) then return true end
    end
    return false
end

local function clamp(value, minimum, maximum)
    if value < minimum then return minimum end
    if value > maximum then return maximum end
    return value
end

local function current_rupees_phrase()
    local rupees = read_u8(ADDR.rupees)
    return rupees == 1 and "1 Rupee" or string.format("%d Rupees", rupees)
end

local function current_bombs_phrase()
    local bombs = read_u8(ADDR.bombs)
    return bombs == 1 and "1 Bomb" or string.format("%d Bombs", bombs)
end

local function current_keys_phrase()
    local keys = read_u8(ADDR.keys)
    return keys == 1 and "1 Key" or string.format("%d Keys", keys)
end

local function format_current_heart_phrase(whole_hearts, partial, max_hearts)
    if partial == 0 then return tostring(whole_hearts) end
    if partial < 0x80 then
        if whole_hearts == 0 then return "half" end
        return string.format("%d and a half", whole_hearts)
    end
    return tostring(math.min(whole_hearts + 1, max_hearts))
end

local function current_health_phrase()
    local heart_values = read_u8(ADDR.heart_values)
    local heart_partial = read_u8(ADDR.heart_partial)
    local current_full = heart_values % 16
    local max_hearts = math.floor(heart_values / 16) + 1
    if current_full < 0 then current_full = 0 end
    if max_hearts < 0 then max_hearts = 0 end
    local current_text = format_current_heart_phrase(current_full, heart_partial, max_hearts)
    local heart_label = max_hearts == 1 and "heart" or "hearts"
    return string.format("%s of %d %s", current_text, max_hearts, heart_label)
end

local function current_a_item_phrase()
    local sword = read_u8(ADDR.items_base)
    if sword >= 3 then return "Magic Sword" end
    if sword == 2 then return "White Sword" end
    if sword == 1 then return "Wood Sword" end
    return "No Sword"
end

local function current_b_item_phrase()
    local slot = read_u8(ADDR.selected_item_slot)
    if slot == 0 then
        if read_u8(ADDR.magic_boomerang) ~= 0 then return "Magic Boomerang" end
        if read_u8(ADDR.boomerang) ~= 0 then return "Boomerang" end
        return "No B Item"
    end
    if slot == 1 then return "Bombs" end
    if slot == 2 then
        if read_u8(ADDR.bow) == 0 then return "No B Item" end
        if read_u8(ADDR.items_base + 2) >= 2 then return "Bow & Silver Arrows" end
        return "Bow & Arrows"
    end
    if slot == 4 then
        if read_u8(ADDR.candle) >= 2 then return "Red Candle" end
        if read_u8(ADDR.candle) == 1 then return "Blue Candle" end
        return "Candle"
    end
    if slot == 5 then return "Recorder" end
    if slot == 6 then
        if read_u8(ADDR.food) ~= 0 then return "Bait" end
        return "Food"
    end
    if slot == 7 then
        if read_u8(ADDR.potion) >= 2 then return "Red Potion" end
        if read_u8(ADDR.potion) == 1 then return "Blue Potion" end
        return "Potion"
    end
    if slot == 8 then return "Magic Wand" end
    if slot == 15 then
        if read_u8(ADDR.potion) >= 2 then return "Red Potion" end
        if read_u8(ADDR.potion) == 1 then return "Blue Potion" end
        return "Letter"
    end
    return "No B Item"
end

local function current_triforce_phrase()
    local tf = read_u8(ADDR.triforce_pieces)
    if tf == 0 then return "No Triforce pieces." end
    if tf == 0xFF then return "All 8 Triforce pieces. Zelda awaits." end
    local dungeon_names = { "Eagle", "Moon", "Manji", "Snake", "Lizard", "Dragon", "Demon", "Lion" }
    local pieces = {}
    local masks = { 1, 2, 4, 8, 16, 32, 64, 128 }
    for i = 1, 8 do
        if tf % (masks[i] * 2) >= masks[i] then
            pieces[#pieces + 1] = dungeon_names[i]
        end
    end
    return string.format("%d of 8 Triforce: %s.", #pieces, table.concat(pieces, ", "))
end

-- Non-selectable top-row inventory items for PageUp/PageDown cycling
local TOP_ROW_ITEMS = {
    { label = "Raft",          addr = 0x0660, check = function(v) return v ~= 0 end },
    { label = "Book of Magic", addr = 0x0661, check = function(v) return v ~= 0 end },
    { label = "Ring",          addr = 0x0662, check = function(v) return v ~= 0 end,
      phrase = function()
          local v = read_u8(0x0662)
          if v == 2 then return "Red Ring" end
          if v == 1 then return "Blue Ring" end
          return "No Ring"
      end },
    { label = "Stepladder",    addr = 0x0663, check = function(v) return v ~= 0 end },
    { label = "Master Key",    addr = 0x0664, check = function(v) return v ~= 0 end },
    { label = "Power Bracelet",addr = 0x0665, check = function(v) return v ~= 0 end },
}

-- Dungeon names indexed by level. Mirrors Dungeons.lua DUNGEON_NAMES so we
-- can label the current dungeon's map and compass treasures without going
-- through the navigation module.
local DUNGEON_NAMES = {
    [1]="Eagle",   [2]="Moon",   [3]="Manji",
    [4]="Snake",   [5]="Lizard", [6]="Dragon",
    [7]="Demon",   [8]="Lion",   [9]="Death Mountain",
}

-- Returns a list of map/compass treasure entries for the dungeon Link is
-- currently in. Empty list when on the overworld -- those treasures are
-- dungeon-specific so they don't appear elsewhere. Inserted into the
-- treasure cycle alongside the static TOP_ROW_ITEMS list when Page Up /
-- Page Down is pressed in the inventory menu.
--
-- L1-L8 share two bitmask bytes:
--   0x0667 = compass bitmask, bit (level-1) set means owned
--   0x0668 = map bitmask, bit (level-1) set means owned
-- L9 has its own dedicated bytes:
--   0x0669 = compass for L9 (nonzero means owned)
--   0x066A = map for L9 (nonzero means owned)
local function current_dungeon_treasures()
    local level = read_u8(ADDR.cur_level)
    if level < 1 or level > 9 then return {} end

    local has_map, has_compass
    if level == 9 then
        has_compass = read_u8(0x0669) ~= 0
        has_map     = read_u8(0x066A) ~= 0
    else
        local mask = 2 ^ (level - 1)  -- bit 0 = L1, bit 1 = L2, ...
        local compass_byte = read_u8(0x0667)
        local map_byte     = read_u8(0x0668)
        has_compass = (compass_byte % (mask * 2)) >= mask
        has_map     = (map_byte     % (mask * 2)) >= mask
    end

    local name    = DUNGEON_NAMES[level] or ("Dungeon " .. level)
    local entries = {}
    if has_map then
        entries[#entries+1] = { label = string.format("Map, Dungeon %d, %s", level, name) }
    end
    if has_compass then
        entries[#entries+1] = { label = string.format("Compass, Dungeon %d, %s", level, name) }
    end
    return entries
end

local top_row_cursor = 1
local last_b_slot = -1
local state_frame = 0

-- Subscreen state -- Dragon Warrior style seen/missing frame counters
local subscreen_open = false
local subscreen_entry_pending = false
local subscreen_settle_frames_left = 0
local SUBSCREEN_SETTLE_FRAMES = 45
local subscreen_close_announced = false
local subscreen_seen_stable = false

write_speech("Zelda 1 Access loaded.")
emit_sound_command("reset")

local gameplay_active = false
local ENEMY_SLOT_FIRST = 1
local ENEMY_SLOT_LAST = 0x0B
local frozen_enemy_positions = {}
local item_linger_state = {}
local last_room_signature = ""

local function room_signature()
    return string.format("%02X:%02X", read_u8(ADDR.cur_level), read_u8(ADDR.room_id))
end

local function reset_enemy_control_state()
    frozen_enemy_positions = {}
end

local function reset_item_linger_state()
    item_linger_state = {}
end

local function apply_enemy_controls()
    local signature = room_signature()
    if signature ~= last_room_signature then
        reset_enemy_control_state()
        last_room_signature = signature
    end

    if not settings.freeze_enemies_enabled then
        reset_enemy_control_state()
        return
    end

    -- Freeze: lock each enemy slot's position every frame.
    -- Also stop boulders on Death Mountain.
    write_u8(ADDR.active_boulders, 0)

    -- Soft-lock guard: delete Wall Masters, boulders, and fire
    -- BEFORE the position-pin loop. Pinning these in place can
    -- trap the player (Wall Master grabbing Link, boulder blocking
    -- a corridor, fire blocking a doorway). Zeroing the type byte
    -- removes them from the slot so the rest of the freeze logic
    -- skips them naturally.
    for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
        local obj_type = read_u8(ADDR.obj_type_base + slot)
        if FREEZE_KILL_TYPES[obj_type] then
            write_u8(ADDR.obj_type_base + slot, 0)
            frozen_enemy_positions[slot] = nil
        end
    end
    -- LadderSlot at $0064 holds the slot index of the active ladder
    -- object when one is deployed (e.g. Link stepping onto a dock-hop
    -- gap on F16 with the stepladder). Reading it once per frame so we
    -- can skip that slot in the freeze loop -- otherwise we pin the
    -- ladder in place and Link can't walk onto it.
    local ladder_slot = read_u8(ADDR.ladder_slot)
    for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
        local obj_type = read_u8(ADDR.obj_type_base + slot)
        if obj_type == 0 then
            frozen_enemy_positions[slot] = nil
        else
            local frozen = frozen_enemy_positions[slot]
            if not frozen or frozen.obj_type ~= obj_type then
                frozen = {
                    obj_type = obj_type,
                    x = read_u8(ADDR.obj_x_base + slot),
                    y = read_u8(ADDR.obj_y_base + slot),
                }
                frozen_enemy_positions[slot] = frozen
            end
            -- Skip non-enemy slot occupants. Dropped items (0x60), push
            -- block (0x68), and reveal targets (rocks/walls/trees/graves
            -- 0x62-0x65) all share the enemy slots but freezing them
            -- breaks game mechanics. The active ladder (LadderSlot != 0)
            -- likewise must be free to advance state.
            if obj_type ~= DROP_ITEM_OBJ_TYPE
                    and obj_type ~= PUSH_BLOCK_OBJ_TYPE
                    and not REVEAL_TARGET_TYPES[obj_type]
                    and (ladder_slot == 0 or slot ~= ladder_slot) then
                write_u8(ADDR.obj_x_base + slot, frozen.x)
                write_u8(ADDR.obj_y_base + slot, frozen.y)
                write_u8(ADDR.obj_input_dir_base + slot, 0)
                write_u8(ADDR.obj_dir_base + slot, 0)
                write_u8(ADDR.obj_shove_dir_base + slot, 0)
                write_u8(ADDR.obj_shove_distance_base + slot, 0)
                write_u8(ADDR.obj_pos_frac_base + slot, 0)
            end
        end
    end
end

local function apply_targeted_suppressions()
    -- Per-frame suppressions for specific environmental hazards.
    -- Independent of freeze_enemies_enabled -- these can be toggled
    -- on alone for accessibility / map-learning purposes.
    --
    -- Boulders: Death Mountain falling rocks come in two object
    -- types -- 0x1F (the spawner that throws individual rocks) and
    -- 0x20 (the individual rolling Boulder projectile). Zeroing both
    -- in the enemy slot table removes them from the playfield.
    -- Also clearing the $0515 ActiveBoulders counter so the spawner
    -- doesn't immediately reseed.
    --
    -- Fire (object types 0x3F GuardFire and 0x40 StandingFire):
    -- the static fire enemies in dungeons. Zeroing their type byte
    -- removes them from the slot. Unlike regular enemies these are
    -- placed objects, not spawned by the room state machine, so
    -- removing them doesn't break room-clear or item logic.
    if settings.suppress_boulders_enabled then
        write_u8(ADDR.active_boulders, 0)
        for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
            local obj_type = read_u8(ADDR.obj_type_base + slot)
            if obj_type == 0x1F or obj_type == 0x20 then
                write_u8(ADDR.obj_type_base + slot, 0)
            end
        end
    end

    if settings.suppress_fire_enabled then
        for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
            local obj_type = read_u8(ADDR.obj_type_base + slot)
            if obj_type == 0x3F or obj_type == 0x40 then
                write_u8(ADDR.obj_type_base + slot, 0)
            end
        end
    end

    -- Wall Master suppression. Wall Masters (object type 0x37) reach
    -- out from the wall, grab Link, and warp him back to the dungeon
    -- entrance. Fully sighted players can dodge them; without vision
    -- they're frequent and frustrating, and on top of that the
    -- freeze-enemies feature CAN'T pin them safely (a frozen Wall
    -- Master gripping Link is a hard soft-lock). Standalone toggle
    -- so the player can suppress them without freezing everything
    -- else. Independent of freeze_enemies, which kills them as well.
    if settings.suppress_wall_master_enabled then
        for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
            local obj_type = read_u8(ADDR.obj_type_base + slot)
            if obj_type == 0x27 then
                write_u8(ADDR.obj_type_base + slot, 0)
            end
        end
    end
end

local function apply_item_drop_linger()
    local mode = settings.item_drop_linger_mode
    if mode == "normal" then
        reset_item_linger_state()
        return
    end

    for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
        local obj_type = read_u8(ADDR.obj_type_base + slot)
        if obj_type ~= DROP_ITEM_OBJ_TYPE then
            item_linger_state[slot] = nil
        else
            local lifetime_addr = ADDR.obj_pos_frac_base + slot
            local lifetime = read_u8(lifetime_addr)
            local track = item_linger_state[slot] or { last_lifetime = lifetime, cancel_phase = 0 }
            if mode == "never" then
                if lifetime > 0 and lifetime < 0xEF then
                    write_u8(lifetime_addr, 0xEF)
                    lifetime = 0xEF
                end
            elseif lifetime > 0 and lifetime < 0xF0 and track.last_lifetime == (lifetime + 1) then
                local cycle = mode == "x4" and 4 or 2
                track.cancel_phase = (track.cancel_phase + 1) % cycle
                if track.cancel_phase ~= 0 then
                    local restored = lifetime + 1
                    if restored > 0xEF then restored = 0xEF end
                    write_u8(lifetime_addr, restored)
                    lifetime = restored
                end
            end
            track.last_lifetime = lifetime
            item_linger_state[slot] = track
        end
    end
end

local runtime_error_announced = false

while true do
    local ok, err = xpcall(function()
        state_frame = state_frame + 1
        local keys = input.get() or {}
        local mode = read_u8(ADDR.game_mode)
        local in_file_screen = FILE_SCREEN_MODES[mode] == true

        -- Death-sequence handler. Two phases:
        --
        -- Phase 1: GAME OVER (mode 0x11) -- announce after a 30-frame
        --   delay so the speech doesn't step on the death sound
        --   effect at the moment of impact. Once the delay elapses
        --   we say "Game over." and arm in_death_menu so the next
        --   mode-0x08 transition is recognized as the post-death
        --   menu rather than normal gameplay.
        if mode == GAME_OVER_MODE then
            if not game_over_announced and not game_over_pending then
                game_over_pending = true
                game_over_speech_frame = state_frame + GAME_OVER_SPEECH_DELAY
            end
            if game_over_pending and state_frame >= game_over_speech_frame then
                game_over_pending = false
                game_over_announced = true
                in_death_menu = true
                death_menu_announced = false
                death_menu_last_cursor = -1
                write_speech("Game over.")
            end
        else
            game_over_announced = false
            game_over_pending = false
            game_over_speech_frame = -1
        end

        -- Phase 2: Continue/Save/Retry menu (mode 0x08 post-death).
        --   The cursor IS submode $0013 (0=Continue, 1=Save, 2=Retry).
        --   Pressing Select cycles 0->1->2->0. Pressing Start sends
        --   submode to 0x80 (commit) which we use to disarm.
        if in_death_menu and mode == POST_DEATH_MENU_MODE then
            local submode = read_u8(0x0013)
            if submode >= 0x80 then
                in_death_menu = false
                death_menu_announced = false
                death_menu_last_cursor = -1
            else
                if not death_menu_announced then
                    death_menu_announced = true
                    death_menu_last_cursor = submode
                    write_speech(CURSOR_LABELS[submode] or "Continue")
                elseif submode ~= death_menu_last_cursor and CURSOR_LABELS[submode] then
                    death_menu_last_cursor = submode
                    write_speech(CURSOR_LABELS[submode])
                end
            end
        elseif in_death_menu and mode ~= GAME_OVER_MODE
                and mode ~= POST_DEATH_MENU_MODE then
            -- Game has moved on past the death sequence entirely.
            in_death_menu = false
            death_menu_announced = false
            death_menu_last_cursor = -1
        end

        -- Ending sequence watcher.
        local cur_peace = read_u8(ENDING_PEACE_CHAR_INDEX)
        local cur_textbox_ptr = read_u8(ENDING_TEXTBOX_CHAR_PTR_LO)
        local cur_flash_timer = read_u8(ENDING_FLASH_LONG_TIMER)
        local cur_level_v = read_u8(ADDR.cur_level)

        if cur_peace ~= prev_peace_char_index
                and prev_peace_char_index == 0
                and cur_peace == 1 then
            if mode == 0x13
                    and cur_flash_timer ~= 0
                    and not ending_the_end_announced then
                ending_the_end_announced = true
                write_speech("Finally, peace returns to Hyrule. This ends the story.")
            end
        end
        prev_peace_char_index = cur_peace

        if cur_textbox_ptr ~= prev_textbox_char_ptr_lo
                and prev_textbox_char_ptr_lo == 0
                and cur_textbox_ptr ~= 0
                and mode == 0x13
                and not ending_zelda_speech_announced then
            ending_zelda_speech_announced = true
            write_speech("Thanks Link, you're the hero of Hyrule.")
        end
        prev_textbox_char_ptr_lo = cur_textbox_ptr

        if mode == 0x05 and cur_level_v ~= 9
                and (ending_zelda_speech_announced
                     or ending_credits_announced
                     or ending_the_end_announced) then
            ending_zelda_speech_announced = false
            ending_credits_announced = false
            ending_the_end_announced = false
        end

        local ctx = {
            keys = keys,
            settings = settings,
            read_u8 = read_u8,
            write_u8 = write_u8,
            write_speech = write_speech,
            emit_sound_command = emit_sound_command,
            save_settings = function(updated)
                settings = save_settings(updated)
                return settings
            end,
            just_pressed = function(key_name)
                return just_pressed_without_shift(keys, key_name)
            end,
            just_pressed_any = function(key_names)
                return just_pressed_any(keys, key_names) and not shift_held(keys)
            end,
        }

        menus.update(ctx)

        -- Subscreen detection via 0x00E1 (Item Menu Scrolling Animation)
        -- Non-zero while inventory menu is animating open, open, or animating closed.
        -- Select pause does NOT set this -- only Start/inventory does.
        local menu_scroll = mainmemory.read_u8(0x00E1)
        local subscreen_visible_now = (menu_scroll ~= 0x00)

        if subscreen_visible_now and not subscreen_open then
            -- Menu opening
            subscreen_open = true
            subscreen_close_announced = false
            subscreen_entry_pending = true
            subscreen_settle_frames_left = SUBSCREEN_SETTLE_FRAMES
            last_b_slot = -1
            top_row_cursor = 1
            write_speech("Inventory Menu.")
        elseif not subscreen_visible_now and subscreen_open then
            -- Menu fully closed
            write_speech("Inventory Menu Closed.")
            subscreen_open = false
            subscreen_entry_pending = false
            subscreen_settle_frames_left = 0
            subscreen_close_announced = false
            last_b_slot = -1
        end

        if subscreen_open and subscreen_settle_frames_left > 0 then
            subscreen_settle_frames_left = subscreen_settle_frames_left - 1
        end

        if subscreen_open and subscreen_entry_pending and subscreen_settle_frames_left == 0 then
            subscreen_entry_pending = false
            last_b_slot = read_u8(ADDR.selected_item_slot)
            write_speech(current_b_item_phrase())
        end

        if not menus.is_accessibility_open() then
            if subscreen_open and not subscreen_entry_pending then
                local slot = read_u8(ADDR.selected_item_slot)
                if slot ~= last_b_slot then
                    last_b_slot = slot
                    write_speech(current_b_item_phrase())
                end
                if just_pressed(keys, "PageUp") or just_pressed(keys, "PageDown") then
                    local owned = {}
                    for _, item in ipairs(TOP_ROW_ITEMS) do
                        if item.check(read_u8(item.addr)) then
                            owned[#owned + 1] = item
                        end
                    end
                    -- Append dungeon-specific treasures (map/compass) only
                    -- when Link is currently in a dungeon. They're omitted
                    -- on the overworld and inside caves.
                    for _, item in ipairs(current_dungeon_treasures()) do
                        owned[#owned + 1] = item
                    end
                    if #owned == 0 then
                        write_speech("No treasures.")
                    else
                        if just_pressed(keys, "PageUp") then
                            top_row_cursor = top_row_cursor - 1
                            if top_row_cursor < 1 then top_row_cursor = #owned end
                        else
                            top_row_cursor = top_row_cursor + 1
                            if top_row_cursor > #owned then top_row_cursor = 1 end
                        end
                        local item = owned[top_row_cursor]
                        local phrase = item.phrase and item.phrase() or item.label
                        write_speech(phrase)
                    end
                elseif just_pressed(keys, "Home") then
                    write_speech(current_triforce_phrase())
                elseif just_pressed(keys, "Insert") then
                    -- Insert on the inventory screen: announce which
                    -- quest the player is currently on. The quest
                    -- number is stored per save slot at $062D + slot.
                    -- Quest 0 in RAM = Quest 1, Quest 1 in RAM = Quest 2.
                    local slot  = read_u8(0x0016)
                    local quest = read_u8(0x062D + slot)
                    write_speech(string.format("Quest %d.", quest + 1))
                elseif just_pressed(keys, "End") then
                    -- End on the inventory screen: announce shield
                    -- status. Critical accessibility cue -- Like-Likes
                    -- can swallow Link's Magical Shield with no audio
                    -- feedback, leaving him reverted to the wooden
                    -- shield without the player knowing. Reading
                    -- $0676 (InvMagicShield) directly: 0 = wooden
                    -- (default / shield eaten / never bought),
                    -- non-zero = magical.
                    if read_u8(0x0676) ~= 0 then
                        write_speech("Magical Shield.")
                    else
                        write_speech("Wood Shield.")
                    end
                end
            end

            if just_pressed(keys, "G") then
                write_speech(current_rupees_phrase())
            elseif just_pressed_any(keys, { "B" }) then
                -- B = bomb count. Mnemonic match. Was previously K.
                write_speech(current_bombs_phrase())
            elseif just_pressed_any(keys, { "K" }) then
                -- K = key count. Mnemonic match. Was previously J.
                write_speech(current_keys_phrase())
            elseif just_pressed(keys, "H") then
                write_speech(current_health_phrase())
            elseif just_pressed_any(keys, { "Oem1", "Semicolon", ";" }) then
                write_speech(current_b_item_phrase())
            elseif just_pressed_any(keys, { "Oem7", "Quote", "Apostrophe", "'" }) then
                -- ' = A button item (sword). Mnemonic for "A item" via
                -- the apostrophe key being right next to where you'd
                -- type. Was previously L.
                write_speech(current_a_item_phrase())
            end
        end

        if not in_file_screen and not menus.is_accessibility_open() and not subscreen_open then
            -- Skip heavy operations during room transitions (game_mode not 0x05 or 0x0B)
            if mode == 0x05 or mode == 0x0B then
                apply_enemy_controls()
                apply_targeted_suppressions()
                apply_item_drop_linger()
            end
            navigation.update(ctx)
            gameplay_active = true
        elseif gameplay_active and in_file_screen then
            navigation.deactivate(ctx)
            gameplay_active = false
        end

        previous_keys = keys
        runtime_error_announced = false
    end, debug.traceback)

    if not ok then
        -- Gate ALL diagnostics behind the flag so a per-frame error
        -- doesn't flood the BizHawk console (which trips its message
        -- cap and suppresses output, breaking script behavior).
        if not runtime_error_announced then
            -- Bypass append_crash_log (may fail silently) and write a
            -- direct simple line to a dedicated diagnostic file. One
            -- write per error session, so no flood risk.
            local diag = io.open(DATA_DIR .. "/zelda_runtime_error.log", "a")
            if diag then
                diag:write(os.date("[%Y-%m-%d %H:%M:%S] ") .. tostring(err) .. "\n")
                diag:close()
            end
            append_crash_log(err)
            console.log("[zelda1] runtime error: " .. tostring(err))
            write_speech("Runtime error. Check crash log.")
            runtime_error_announced = true
        end
        gameplay_active = false
        previous_keys = {}
    end

    emu.frameadvance()
end
