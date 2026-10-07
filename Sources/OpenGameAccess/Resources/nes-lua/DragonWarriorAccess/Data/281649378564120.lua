-- Stable base pathfinder engine snapshot.
-- Keep this file as the engine-side reference while Pathfinder_Access.lua carries merged accessibility behavior.

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
    learned_exit_file = DATA_DIR .. "/553908126744210.lua",
    assist_settings_file = DATA_DIR .. "/118437650298441.lua",
    map_data_file = DATA_DIR .. "/662904817335100.lua",
    map_reference_file = DATA_DIR .. "/771205948331642.lua",
    world_map_grid_file = DATA_DIR .. "/480126759334881.lua",
    previous_entity_hotkey = "PageUp",
    next_entity_hotkey = "PageDown",
    repeat_entity_hotkey = "Home",
    navigate_entity_hotkey = "End",
    map_name_hotkey = "Insert",
    stationary_confirm_frames = 45,
    take_confirm_frames = 12,
    recent_move_window_frames = 180,
    hazard_warning_cooldown_frames = 45,
    enable_border_exit_learning = false,
    use_persisted_learned_exits = false,
}

local TELEPORT_DIRECTIONS = {
    Up = { dx = 0, dy = -1, name = "north" },
    Down = { dx = 0, dy = 1, name = "south" },
    Left = { dx = -1, dy = 0, name = "west" },
    Right = { dx = 1, dy = 0, name = "east" },
}

local NPC_TYPE_NAMES = {
    [0] = "man",
    [1] = "red soldier",
    [2] = "grey soldier",
    [3] = "merchant",
    [4] = "king",
    [5] = "old man",
    [6] = "woman",
    [7] = "stationary guard",
}

local TILE_NAMES = {
    ["."] = "grass",
    [","] = "sand",
    ["w"] = "water",
    ["C"] = "chest",
    ["S"] = "stone",
    ["U"] = "stairs_up",
    ["B"] = "brick",
    ["D"] = "stairs_down",
    ["T"] = "trees",
    ["P"] = "swamp",
    ["F"] = "force_field",
    ["+"] = "door",
    ["W"] = "shop_sign",
    ["I"] = "inn_sign",
    ["="] = "bridge",
    ["#"] = "table",
}

local RAW_DUNGEON_TILE_NAMES = {
    [0x0] = "stone",
    [0x1] = "stairs_up",
    [0x2] = "brick",
    [0x3] = "stairs_down",
    [0x4] = "chest",
    [0x5] = "door",
    [0x6] = "princess",
    [0x7] = "blank",
    [0x8] = "stone",
    [0x9] = "stairs_up",
    [0xA] = "brick",
    [0xB] = "stairs_down",
    [0xC] = "chest",
    [0xD] = "door",
    [0xE] = "princess",
    [0xF] = "blank",
}

local RAW_TOWN_TILE_NAMES = {
    [0x0] = "grass",
    [0x1] = "sand",
    [0x2] = "water",
    [0x3] = "chest",
    [0x4] = "stone",
    [0x5] = "stairs_up",
    [0x6] = "brick",
    [0x7] = "stairs_down",
    [0x8] = "trees",
    [0x9] = "swamp",
    [0xA] = "force_field",
    [0xB] = "door",
    [0xC] = "shop_sign",
    [0xD] = "inn_sign",
    [0xE] = "bridge",
    [0xF] = "large_tile",
}

local RAW_INTERIOR_TILE_NAMES = {
    [0x0] = "grass",
    [0x1] = "sand",
    [0x2] = "water",
    [0x3] = "chest",
    [0x4] = "stone",
    [0x5] = "stairs_up",
    [0x6] = "brick",
    [0x7] = "stairs_down",
    [0x8] = "grass",
    [0x9] = "sand",
    [0xA] = "water",
    [0xB] = "chest",
    [0xC] = "stone",
    [0xD] = "stairs_up",
    [0xE] = "brick",
    [0xF] = "stairs_down",
}

local THRONE_ROOM_MAP_ID = 0x05
local SWAMP_CAVE_MAP_ID = 0x15
local WORLD_MAP_ID = 0x01
local CHARLOCK_CASTLE_MAP_ID = 0x02
local PLAYER_FLAGS_ADDR = 0x00DF
local MODSN_SPELLS_ADDR = 0x00CF
local F_LEFT_THROOM = 0x08
local F_PSG_FOUND = 0x04
local F_DONE_GWAELIN = 0x03
local F_RNBW_BRDG = 0x08

local WALKABLE = {
    grass = true,
    sand = true,
    brick = true,
    stairs_up = true,
    stairs_down = true,
    bridge = true,
    swamp = true,
    force_field = true,
    trees = true,
}

local STATIC_ENTITY_TILE_LABELS = {
    chest = "chest",
    door = "door",
    princess = "Princess Gwaelin",
    stairs_up = "stairs up",
    stairs_down = "stairs down",
}

local AUXILIARY_STATIC_TILE_LABELS = {
    table = "table",
    large_tile = "large tile",
}

local DELAYED_INTERACTION_LABELS_BY_POSITION = {}
local ACTIVE_DELAYED_INTERACTION_APPROACHES_BY_POSITION = {}
local HIDDEN_ITEM_LABELS_BY_POSITION = {}
local ACTIVE_WALKABLE_TERRAINS = WALKABLE
local ACTIVE_BORDER_BLOCKING = false
local ACTIVE_EXPLICIT_EXIT_ENTITIES = {}
local ACTIVE_EXCLUDED_LEARNED_EXIT_DIRECTIONS = {}
local ACTIVE_DELAYED_INTERACTION_CATEGORY_OVERRIDES = {}
local ACTIVE_HIDDEN_ITEM_CATEGORY = "treasure"
local ACTIVE_HIDDEN_ITEM_REQUIREMENTS_BY_POSITION = {}
local ACTIVE_FORCED_WALKABLE_POSITIONS = {}
local ACTIVE_EXCLUDED_ENTITY_POSITIONS = {}
local ACTIVE_BORDER_OPEN_POSITIONS = {}
local ACTIVE_DELAYED_INTERACTION_ORDER_BY_POSITION = {}
local ACTIVE_COUNTER_PRESENCE_RULES_BY_APPROACH = {}
local ACTIVE_NPCS_ENABLED = true
local ENTITY_CATEGORIES = {
    { id = "all", label = "All" },
    { id = "treasure", label = "Treasure Chests" },
    { id = "npcs", label = "NPC's" },
    { id = "other", label = "Other" },
    { id = "doors", label = "Doors" },
    { id = "services", label = "Services" },
    { id = "map_exits", label = "Map Exits" },
}

local state = {
    speech_sequence = 0,
    last_spoken = "",
    last_keyboard = {},
    current_map_id = nil,
    selected_entity_index = 1,
    selected_entity_key = nil,
    last_announced_map_id = nil,
    npc_tracks = {},
    route_cache = nil,
    pathfinding_filter_enabled = false,
    hazard_warnings_enabled = true,
    learned_exit_by_map = {},
    previous_player = nil,
    last_supported_tiles_by_map = {},
    removed_removable_keys = {},
    category_index = 1,
    last_take_trigger_active = false,
    pending_take_candidates = nil,
    pending_take_frames = 0,
    active_supported_map_id = nil,
    last_swamp_warning_key = nil,
    last_swamp_warning_frame = nil,
    last_counter_presence_key = nil,
    last_world_runtime_state_key = nil,
    cached_static_entities_key = nil,
    cached_static_entities = nil,
    cached_removable_entities_key = nil,
    cached_removable_entities = nil,
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

local function speak(message, interrupt)
    if not message or message == "" then
        return
    end
    if interrupt == false and message == state.last_spoken then
        return
    end
    console.log("[pathfinder-engine] " .. message)
    write_speech_payload(message)
    state.last_spoken = message
end

local function keyboard_pressed(keys, name)
    return keys[name] and not state.last_keyboard[name]
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

local function abs(value)
    if value < 0 then
        return -value
    end
    return value
end

local function file_exists(path)
    local file = io.open(path, "r")
    if file then
        file:close()
        return true
    end
    return false
end

local function lua_quote(text)
    local value = tostring(text or "")
    value = value:gsub("\\", "\\\\")
    value = value:gsub("\"", "\\\"")
    value = value:gsub("\r", "\\r")
    value = value:gsub("\n", "\\n")
    return "\"" .. value .. "\""
end

local function load_persisted_learned_exits()
    if not CONFIG.use_persisted_learned_exits then
        return {}
    end
    if not file_exists(CONFIG.learned_exit_file) then
        return {}
    end

    local chunk, err = loadfile(CONFIG.learned_exit_file)
    if not chunk then
        console.log("[pathfinder-engine] failed to load learned exits: " .. tostring(err))
        return {}
    end

    local ok, data = pcall(chunk)
    if not ok or type(data) ~= "table" then
        console.log("[pathfinder-engine] learned exits file did not return a table.")
        return {}
    end

    return data
end

local function load_assist_settings()
    if not file_exists(CONFIG.assist_settings_file) then
        return {}
    end

    local chunk, err = loadfile(CONFIG.assist_settings_file)
    if not chunk then
        console.log("[pathfinder-engine] failed to load assist settings: " .. tostring(err))
        return {}
    end

    local ok, data = pcall(chunk)
    if not ok or type(data) ~= "table" then
        console.log("[pathfinder-engine] assist settings file did not return a table.")
        return {}
    end

    return data
end

local function persist_pathfinding_filter_setting()
    local settings = load_assist_settings()
    settings.pathfinding_filter_enabled = state.pathfinding_filter_enabled == true

    local file = io.open(CONFIG.assist_settings_file, "w")
    if not file then
        console.log("[pathfinder-engine] failed to save assist settings.")
        return false
    end

    file:write("return {\n")
    file:write(string.format("    battles_enabled = %s,\n", settings.battles_enabled == false and "false" or "true"))
    file:write(string.format("    critical_health_warnings_enabled = %s,\n", settings.critical_health_warnings_enabled == false and "false" or "true"))
    file:write(string.format("    hazard_warnings_enabled = %s,\n", settings.hazard_warnings_enabled == false and "false" or "true"))
    file:write(string.format("    exp_multiplier = %d,\n", tonumber(settings.exp_multiplier) or 3))
    file:write(string.format("    gold_multiplier = %d,\n", tonumber(settings.gold_multiplier) or 3))
    file:write(string.format("    pathfinding_filter_enabled = %s,\n", settings.pathfinding_filter_enabled and "true" or "false"))
    file:write("}\n")
    file:close()
    return true
end

local function load_map_reference_data()
    if not file_exists(CONFIG.map_reference_file) then
        return {}, {}, {}
    end

    local chunk, err = loadfile(CONFIG.map_reference_file)
    if not chunk then
        console.log("[pathfinder-engine] failed to load map reference data: " .. tostring(err))
        return {}, {}, {}
    end

    local ok, data = pcall(chunk)
    if not ok or type(data) ~= "table" then
        console.log("[pathfinder-engine] map reference file did not return a table.")
        return {}, {}, {}
    end

    return data.chests_by_map or {}, data.map_names or {}, data.transitions_by_map or {}
end

local function load_map_data()
    if not file_exists(CONFIG.map_data_file) then
        return {}
    end

    local chunk, err = loadfile(CONFIG.map_data_file)
    if not chunk then
        console.log("[pathfinder-engine] failed to load map data: " .. tostring(err))
        return {}
    end

    local ok, data = pcall(chunk)
    if not ok or type(data) ~= "table" then
        console.log("[pathfinder-engine] map data file did not return a table.")
        return {}
    end

    return data.maps or {}
end

local function load_world_map_grid()
    if not file_exists(CONFIG.world_map_grid_file) then
        return nil
    end

    local chunk, err = loadfile(CONFIG.world_map_grid_file)
    if not chunk then
        console.log("[pathfinder-engine] failed to load world map grid: " .. tostring(err))
        return nil
    end

    local ok, data = pcall(chunk)
    if not ok or type(data) ~= "table" then
        console.log("[pathfinder-engine] world map grid file did not return a table.")
        return nil
    end

    return data
end

local function persist_learned_exits()
    if not CONFIG.use_persisted_learned_exits then
        return true
    end
    local lines = {
        "return {",
    }

    local map_ids = {}
    for map_id, exits_by_side in pairs(state.learned_exit_by_map or {}) do
        if exits_by_side and next(exits_by_side) ~= nil then
            map_ids[#map_ids + 1] = map_id
        end
    end
    table.sort(map_ids)

    for _, map_id in ipairs(map_ids) do
        local exits_by_side = state.learned_exit_by_map[map_id]
        if exits_by_side and next(exits_by_side) ~= nil then
            lines[#lines + 1] = string.format("    [%d] = {", map_id)
            for _, direction in ipairs({ "North", "East", "South", "West" }) do
                local exit = exits_by_side[direction]
                if exit then
                    lines[#lines + 1] = string.format("        %s = {", direction)
                    lines[#lines + 1] = string.format("            x = %d,", exit.x or 0)
                    lines[#lines + 1] = string.format("            y = %d,", exit.y or 0)
                    lines[#lines + 1] = string.format("            direction = %s,", lua_quote(exit.direction or direction))
                    lines[#lines + 1] = string.format("            destination_map_id = %d,", exit.destination_map_id or exit.source_map_id or 0)
                    lines[#lines + 1] = "        },"
                end
            end
            lines[#lines + 1] = "    },"
        end
    end

    lines[#lines + 1] = "}"

    local file, err = io.open(CONFIG.learned_exit_file, "w")
    if not file then
        console.log("[pathfinder-engine] failed to persist learned exits: " .. tostring(err))
        return false
    end

    file:write(table.concat(lines, "\n"))
    file:close()
    return true
end

state.learned_exit_by_map = load_persisted_learned_exits()
do
local assist_settings = load_assist_settings()
if type(assist_settings.pathfinding_filter_enabled) == "boolean" then
    state.pathfinding_filter_enabled = assist_settings.pathfinding_filter_enabled
end
if type(assist_settings.hazard_warnings_enabled) == "boolean" then
    state.hazard_warnings_enabled = assist_settings.hazard_warnings_enabled
end
end

local function make_key(x, y)
    return string.format("%d:%d", x or 0, y or 0)
end

local function removed_removable_key(type_name, x, y)
    return string.format("%s:%s", type_name, make_key(x, y))
end

local function current_supported_map_id(player)
    if player and map_is_supported(player.map_id) then
        return player.map_id
    end
    return state.active_supported_map_id
end

local function title_case_words(text)
    local parts = {}
    for part in string.gmatch(text or "", "%S+") do
        parts[#parts + 1] = part:sub(1, 1):upper() .. part:sub(2)
    end
    return table.concat(parts, " ")
end

local function combined_direction(dx, dy)
    local horizontal = nil
    local vertical = nil
    if dx <= -1 then
        horizontal = "West"
    elseif dx >= 1 then
        horizontal = "East"
    end
    if dy <= -1 then
        vertical = "North"
    elseif dy >= 1 then
        vertical = "South"
    end
    if vertical and horizontal then
        return vertical .. horizontal
    end
    return vertical or horizontal or "Here"
end

local tile_name_at
local tile_walkable
local tile_walkable_for_map
local resolved_removable_tile_name
local reset_route_cache

local function read_player_state()
    return {
        map_id = mainmemory.read_u8(0x0045),
        map_type = mainmemory.read_u8(0x0016),
        x = mainmemory.read_u8(0x003A),
        y = mainmemory.read_u8(0x003B),
        facing = mainmemory.read_u8(0x006C) or 0,
        magic_keys = mainmemory.read_u8(0x00BF) or 0,
        player_flags = mainmemory.read_u8(PLAYER_FLAGS_ADDR) or 0,
    }
end

local function facing_name(value)
    if value == 0 then
        return "north"
    elseif value == 1 then
        return "east"
    elseif value == 2 then
        return "south"
    elseif value == 3 then
        return "west"
    end
    return "unknown"
end

local function facing_delta(value)
    if value == 0 then
        return 0, -1
    elseif value == 1 then
        return 1, 0
    elseif value == 2 then
        return 0, 1
    elseif value == 3 then
        return -1, 0
    end
    return 0, 0
end

local RAW_MAP_DATA_BY_ID = load_map_data()
local WORLD_MAP_GRID = load_world_map_grid()

local function decode_map_definition(map_id, raw_map)
    if type(raw_map) ~= "table" then
        return nil
    end

    if type(raw_map.rows) == "table" then
        local rows = {}
        local width = 0
        local y = 0
        for _, row_text in ipairs(raw_map.rows) do
            if type(row_text) == "string" then
                local row = {}
                local x = 0
                for index = 1, #row_text do
                    local code = string.sub(row_text, index, index)
                    row[x] = TILE_NAMES[code]
                    x = x + 1
                end
                rows[y] = row
                width = math.max(width, x)
                y = y + 1
            end
        end
        return {
            map_id = map_id,
            name = raw_map.name or map_name_from_id and map_name_from_id(map_id) or string.format("Map %02X", map_id or 0),
            width = width,
            height = y,
            rows = rows,
        }
    end

    if type(raw_map.rows) == "string" then
        local rows = {}
        local width = 0
        local y = 0
        for line in string.gmatch(raw_map.rows, "[^\r\n]+") do
            local row_text = string.match(line, "^%d+:(.+)$")
            if row_text then
                local row = {}
                local x = 0
                for index = 1, #row_text do
                    local code = string.sub(row_text, index, index)
                    row[x] = TILE_NAMES[code]
                    x = x + 1
                end
                rows[y] = row
                width = math.max(width, x)
                y = y + 1
            end
        end
        return {
            map_id = map_id,
            name = raw_map.name or map_name_from_id and map_name_from_id(map_id) or string.format("Map %02X", map_id or 0),
            width = width,
            height = y,
            rows = rows,
        }
    end

    if type(raw_map.raw_dungeon_bytes) == "table" and tonumber(raw_map.width) and tonumber(raw_map.height) then
        local width = tonumber(raw_map.width)
        local height = tonumber(raw_map.height)
        local row_bytes = math.floor(width / 2)
        local rows = {}
        local raw_tile_names = RAW_DUNGEON_TILE_NAMES
        if raw_map.raw_tile_set == "town" then
            raw_tile_names = RAW_TOWN_TILE_NAMES
        elseif raw_map.raw_tile_set == "interior" then
            raw_tile_names = RAW_INTERIOR_TILE_NAMES
        end

        for y = 0, height - 1 do
            local row = {}
            local x = 0
            for byte_index = 0, row_bytes - 1 do
                local value = raw_map.raw_dungeon_bytes[(y * row_bytes) + byte_index + 1] or 0
                local left = math.floor(value / 0x10)
                local right = value % 0x10
                row[x] = raw_tile_names[left]
                row[x + 1] = raw_tile_names[right]
                x = x + 2
            end
            rows[y] = row
        end

        return {
            map_id = map_id,
            name = raw_map.name or map_name_from_id and map_name_from_id(map_id) or string.format("Map %02X", map_id or 0),
            width = width,
            height = height,
            rows = rows,
        }
    end

    return nil
end

local function decode_passability_definition(map_id, raw_map, decoded_map)
    if map_id == WORLD_MAP_ID or type(raw_map) ~= "table" or type(decoded_map) ~= "table" then
        return nil
    end

    if type(raw_map.passability_rows) == "string" then
        local rows = {}
        local y = 0
        for line in string.gmatch(raw_map.passability_rows, "[^\r\n]+") do
            local row_text = string.match(line, "^%d+:(.+)$")
            if row_text then
                local row = {}
                local x = 0
                for index = 1, #row_text do
                    local code = string.sub(row_text, index, index)
                    row[x] = (code == "1" or code == ".")
                    x = x + 1
                end
                rows[y] = row
                y = y + 1
            end
        end
        return rows
    end

    local walkable = (raw_map.terrain and raw_map.terrain.walkable) or WALKABLE
    local rows = {}
    for y = 0, decoded_map.height - 1 do
        local row = {}
        for x = 0, decoded_map.width - 1 do
            local tile_name = decoded_map.rows[y] and decoded_map.rows[y][x] or nil
            row[x] = walkable[tile_name] == true
        end
        rows[y] = row
    end
    return rows
end

local DECODED_MAPS_BY_ID = {}
local DECODED_PASSABILITY_BY_ID = {}
for map_id, raw_map in pairs(RAW_MAP_DATA_BY_ID) do
    DECODED_MAPS_BY_ID[map_id] = decode_map_definition(map_id, raw_map)
    DECODED_PASSABILITY_BY_ID[map_id] = decode_passability_definition(map_id, raw_map, DECODED_MAPS_BY_ID[map_id])
end

local TARGET_MAP = nil
local TARGET_PASSABILITY = nil

local function map_data_for_id(map_id)
    if map_id == WORLD_MAP_ID then
        return WORLD_MAP_GRID
    end
    return RAW_MAP_DATA_BY_ID[map_id]
end

local function decoded_map_for_id(map_id)
    return DECODED_MAPS_BY_ID[map_id]
end

local function decoded_passability_for_id(map_id)
    return DECODED_PASSABILITY_BY_ID[map_id]
end

local function map_is_supported(map_id)
    return map_id == WORLD_MAP_ID or decoded_map_for_id(map_id) ~= nil
end

local function set_active_supported_map(map_id)
    if not map_is_supported(map_id) then
        return false
    end

    state.active_supported_map_id = map_id
    TARGET_MAP = map_id == WORLD_MAP_ID and WORLD_MAP_GRID or decoded_map_for_id(map_id)
    TARGET_PASSABILITY = map_id == WORLD_MAP_ID and nil or decoded_passability_for_id(map_id)

    local raw_map = map_data_for_id(map_id) or {}
    DELAYED_INTERACTION_LABELS_BY_POSITION = raw_map.delayed_interaction_labels_by_position or {}
    ACTIVE_DELAYED_INTERACTION_APPROACHES_BY_POSITION = raw_map.delayed_interaction_approaches_by_position or {}
    HIDDEN_ITEM_LABELS_BY_POSITION = raw_map.hidden_item_labels_by_position or {}
    ACTIVE_WALKABLE_TERRAINS = (raw_map.terrain and raw_map.terrain.walkable) or WALKABLE
    ACTIVE_BORDER_BLOCKING = false
    ACTIVE_EXPLICIT_EXIT_ENTITIES = raw_map.explicit_exit_entities or {}
    ACTIVE_EXCLUDED_LEARNED_EXIT_DIRECTIONS = raw_map.exclude_learned_exit_directions or {}
    ACTIVE_DELAYED_INTERACTION_CATEGORY_OVERRIDES = raw_map.delayed_interaction_category_overrides or {}
    ACTIVE_HIDDEN_ITEM_CATEGORY = raw_map.hidden_item_category or "treasure"
    ACTIVE_HIDDEN_ITEM_REQUIREMENTS_BY_POSITION = raw_map.hidden_item_requirements_by_position or {}
    ACTIVE_FORCED_WALKABLE_POSITIONS = raw_map.forced_walkable_positions or {}
    ACTIVE_EXCLUDED_ENTITY_POSITIONS = raw_map.excluded_entity_positions or {}
    ACTIVE_BORDER_OPEN_POSITIONS = raw_map.border_open_positions or {}
    ACTIVE_DELAYED_INTERACTION_ORDER_BY_POSITION = raw_map.delayed_interaction_order_by_position or {}
    ACTIVE_COUNTER_PRESENCE_RULES_BY_APPROACH = raw_map.counter_presence_rules_by_approach or {}
    ACTIVE_NPCS_ENABLED = raw_map.enable_npcs ~= false
    return true
end

local function map_allows_world_map_exit_learning(map_id)
    local raw_map = map_data_for_id(map_id) or {}
    return raw_map.learn_world_map_exits == true
end

local function target_map_for_id(map_id)
    if map_id == WORLD_MAP_ID then
        return WORLD_MAP_GRID
    end
    return decoded_map_for_id(map_id)
end

local function map_allows_npc_counter_services(map_id)
    local raw_map = map_data_for_id(map_id) or {}
    return raw_map.enable_npc_counter_services ~= false
end

local function walkable_terrains_for_map(map_id)
    local raw_map = map_data_for_id(map_id) or {}
    return (raw_map.terrain and raw_map.terrain.walkable) or WALKABLE
end

function tile_name_at(x, y)
    if not TARGET_MAP then
        return nil
    end
    local row = TARGET_MAP.rows[y]
    return row and row[x] or nil
end

local function passability_at(x, y)
    if TARGET_PASSABILITY and TARGET_PASSABILITY[y] and TARGET_PASSABILITY[y][x] ~= nil then
        return TARGET_PASSABILITY[y][x] == true
    end
    return tile_walkable(tile_name_at(x, y))
end

local function passability_at_for_map(map_id, x, y)
    local passability = decoded_passability_for_id(map_id)
    if passability and passability[y] and passability[y][x] ~= nil then
        return passability[y][x] == true
    end
    return tile_walkable_for_map(map_id, tile_name_at_for_map(map_id, x, y))
end

local function resolved_passability_at(x, y)
    local resolved_tile = resolved_removable_tile_name(x, y)
    local original_tile = tile_name_at(x, y)
    if resolved_tile == "chest" then
        return true
    end
    if resolved_tile ~= original_tile then
        return tile_walkable(resolved_tile)
    end
    return passability_at(x, y)
end

local function build_tile_candidates(tile_name)
    if not TARGET_MAP then
        return {}
    end
    local candidates = {}
    for y = 0, TARGET_MAP.height - 1 do
        for x = 0, TARGET_MAP.width - 1 do
            if tile_name_at(x, y) == tile_name then
                candidates[#candidates + 1] = { x = x, y = y }
            end
        end
    end
    return candidates
end

local TREASURE_CHEST_LOCATIONS_BY_MAP, MAP_NAMES, TRANSITIONS_BY_MAP = load_map_reference_data()
local CHEST_CANDIDATES = {}
local DOOR_CANDIDATES = {}
local TRANSITION_LOOKUP_BY_MAP = {}

for map_id, transitions in pairs(TRANSITIONS_BY_MAP or {}) do
    local lookup = {}
    for _, transition in ipairs(transitions or {}) do
        lookup[string.format("%d,%d", transition.x or -1, transition.y or -1)] = transition
    end
    TRANSITION_LOOKUP_BY_MAP[map_id] = lookup
end

local function find_transition_at(map_id, x, y)
    local lookup = TRANSITION_LOOKUP_BY_MAP[map_id]
    if not lookup then
        return nil
    end
    return lookup[string.format("%d,%d", x or -1, y or -1)]
end

local function hidden_item_candidates()
    local candidates = {}
    for position_key, label in pairs(HIDDEN_ITEM_LABELS_BY_POSITION) do
        local x_text, y_text = string.match(position_key, "^(%-?%d+),(%-?%d+)$")
        local x = tonumber(x_text)
        local y = tonumber(y_text)
        if x and y then
            candidates[#candidates + 1] = {
                x = x,
                y = y,
                label = label or "hidden item",
            }
        end
    end

    table.sort(candidates, function(a, b)
        if a.y ~= b.y then
            return a.y < b.y
        end
        if a.x ~= b.x then
            return a.x < b.x
        end
        return (a.label or "") < (b.label or "")
    end)

    return candidates
end

local HIDDEN_ITEM_CANDIDATES = {}

local function inventory_items()
    local items = {}
    for address = 0x00C1, 0x00C4 do
        local value = mainmemory.read_u8(address) or 0
        local high = math.floor(value / 0x10)
        local low = value % 0x10
        items[#items + 1] = high
        items[#items + 1] = low
    end
    return items
end

local function inventory_contains(item_id)
    if item_id == nil then
        return false
    end
    for _, item in ipairs(inventory_items()) do
        if item == item_id then
            return true
        end
    end
    return false
end

local function ram_nibble_contains(requirement)
    if type(requirement) ~= "table" then
        return false
    end
    local target_value = requirement.value
    if target_value == nil then
        return false
    end
    for _, address in ipairs(requirement.addresses or {}) do
        local byte_value = mainmemory.read_u8(address) or 0
        local high = math.floor(byte_value / 0x10)
        local low = byte_value % 0x10
        if high == target_value or low == target_value then
            return true
        end
    end
    return false
end

local function ram_mask_set(requirement)
    if type(requirement) ~= "table" then
        return false
    end
    local address = requirement.address
    local mask = requirement.mask
    if address == nil or mask == nil then
        return false
    end
    local value = mainmemory.read_u8(address) or 0
    return math.floor(value / mask) % 2 == 1
end

local function ram_mask_equals(requirement)
    if type(requirement) ~= "table" then
        return false
    end
    local address = requirement.address
    local mask = requirement.mask
    local value = requirement.value
    if address == nil or mask == nil or value == nil then
        return false
    end
    if not bit or not bit.band then
        return false
    end
    local byte_value = mainmemory.read_u8(address) or 0
    return bit.band(byte_value, mask) == value
end

local function hidden_item_visible(hidden_item)
    if not hidden_item then
        return false
    end
    local requirement = ACTIVE_HIDDEN_ITEM_REQUIREMENTS_BY_POSITION[string.format("%d,%d", hidden_item.x or -1, hidden_item.y or -1)]
    if type(requirement) ~= "table" then
        return true
    end
    if requirement.require_inventory_item ~= nil and not inventory_contains(requirement.require_inventory_item) then
        return false
    end
    if type(requirement.require_inventory_item_any) == "table" then
        local found = false
        for _, item_id in ipairs(requirement.require_inventory_item_any) do
            if inventory_contains(item_id) then
                found = true
                break
            end
        end
        if not found then
            return false
        end
    end
    if requirement.ram_nibble_contains and ram_nibble_contains(requirement.ram_nibble_contains) then
        return false
    end
    if requirement.ram_mask_set and ram_mask_set(requirement.ram_mask_set) then
        return false
    end
    if requirement.ram_mask_equals and ram_mask_equals(requirement.ram_mask_equals) then
        return false
    end
    if requirement.inventory_item ~= nil and inventory_contains(requirement.inventory_item) then
        return false
    end
    if type(requirement.inventory_item_any) == "table" then
        for _, item_id in ipairs(requirement.inventory_item_any) do
            if inventory_contains(item_id) then
                return false
            end
        end
    end
    return true
end

local function refresh_active_map_candidates()
    local active_map_id = state.active_supported_map_id
    CHEST_CANDIDATES = TREASURE_CHEST_LOCATIONS_BY_MAP[active_map_id] or build_tile_candidates("chest")
    DOOR_CANDIDATES = build_tile_candidates("door")
    HIDDEN_ITEM_CANDIDATES = hidden_item_candidates()
end

local function apply_world_runtime_overrides()
    if state.active_supported_map_id ~= WORLD_MAP_ID or not WORLD_MAP_GRID or not WORLD_MAP_GRID.rows then
        return
    end
    local mods = mainmemory.read_u8(MODSN_SPELLS_ADDR) or 0
    local row = WORLD_MAP_GRID.rows[49]
    if row then
        if bit and bit.band and bit.band(mods, F_RNBW_BRDG) ~= 0 then
            row[64] = "bridge"
        else
            row[64] = "water"
        end
    end
end

local function world_runtime_state_key()
    if state.active_supported_map_id ~= WORLD_MAP_ID then
        return nil
    end
    local mods = mainmemory.read_u8(MODSN_SPELLS_ADDR) or 0
    local story = mainmemory.read_u8(0x00E4) or 0
    local inventory_signature = {}
    for _, item in ipairs(inventory_items()) do
        inventory_signature[#inventory_signature + 1] = string.format("%X", item or 0)
    end
    return string.format("%02X|%02X|%s", mods, story, table.concat(inventory_signature, ""))
end

tile_walkable = function(tile_name)
    return ACTIVE_WALKABLE_TERRAINS[tile_name] == true
end

tile_walkable_for_map = function(map_id, tile_name)
    local walkable = walkable_terrains_for_map(map_id)
    return walkable[tile_name] == true
end

local function forced_walkable_position(x, y)
    return ACTIVE_FORCED_WALKABLE_POSITIONS[string.format("%d,%d", x or -1, y or -1)] == true
end

local function border_open_position(x, y)
    return ACTIVE_BORDER_OPEN_POSITIONS[string.format("%d,%d", x or -1, y or -1)] == true
end

local function entity_position_excluded(type_name, x, y)
    local by_type = ACTIVE_EXCLUDED_ENTITY_POSITIONS[type_name]
    if type(by_type) ~= "table" then
        return false
    end
    return by_type[string.format("%d,%d", x or -1, y or -1)] == true
end

resolved_removable_tile_name = function(x, y)
    local tile_name = tile_name_at(x, y)
    if tile_name == "chest" and state.removed_removable_keys[removed_removable_key("chest", x, y)] then
        return "brick"
    end
    if tile_name == "door" and state.removed_removable_keys[removed_removable_key("door", x, y)] then
        return "brick"
    end
    if forced_walkable_position(x, y) then
        return "brick"
    end
    if tile_name == "princess" and state.active_supported_map_id == SWAMP_CAVE_MAP_ID then
        local flags = mainmemory.read_u8(PLAYER_FLAGS_ADDR) or 0
        if (flags % 0x04) ~= 0 then
            return "brick"
        end
    end
    return tile_name
end

local function take_trigger_active()
    local window_col = mainmemory.read_u8(0x0097) or 0
    local window_row = mainmemory.read_u8(0x0098) or 0
    local cursor_col = mainmemory.read_u8(0x00D8) or 0
    local cursor_row = mainmemory.read_u8(0x00D9) or 0
    local command_trigger = mainmemory.read_u8(0x0047) or 0

    return window_col == 0x0C
        and window_row == 0x02
        and cursor_col == 0x01
        and cursor_row == 0x03
        and command_trigger == 0x01
end

local function read_system_treasure_window()
    local bytes = {}
    for offset = 0, 0x0F do
        bytes[#bytes + 1] = memory.read_u8(0x601C + offset, "System Bus") or 0
    end
    return bytes
end

local function read_system_door_window()
    local bytes = {}
    for offset = 0, 0x0F do
        bytes[#bytes + 1] = memory.read_u8(0x600C + offset, "System Bus") or 0
    end
    return bytes
end

local function system_coordinate_window_contains(bytes, x, y)
    for slot = 0, 7 do
        local index = (slot * 2) + 1
        if bytes[index] == x and bytes[index + 1] == y then
            return true
        end
    end
    return false
end

local function sync_removed_chests_from_treasure_log(player)
    if not player or player.map_id ~= state.active_supported_map_id then
        return
    end

    local bytes = read_system_treasure_window()
    local changed = false
    for _, chest in ipairs(CHEST_CANDIDATES or {}) do
        if system_coordinate_window_contains(bytes, chest.x, chest.y) then
            local key = removed_removable_key("chest", chest.x, chest.y)
            if not state.removed_removable_keys[key] then
                state.removed_removable_keys[key] = true
                changed = true
            end
        end
    end
    if changed then
        reset_route_cache()
    end
end

local function sync_removed_doors_from_door_log(player)
    if not player or player.map_id ~= state.active_supported_map_id then
        return
    end

    local bytes = read_system_door_window()
    local changed = false
    for _, door in ipairs(DOOR_CANDIDATES or {}) do
        if system_coordinate_window_contains(bytes, door.x, door.y) then
            local key = removed_removable_key("door", door.x, door.y)
            if not state.removed_removable_keys[key] then
                state.removed_removable_keys[key] = true
                changed = true
            end
        end
    end
    if changed then
        reset_route_cache()
    end
end

local function apply_special_map_overrides(player)
    if not player or player.map_id ~= state.active_supported_map_id then
        return
    end

    local raw_map = map_data_for_id(player.map_id) or {}
    local changed = false
    for position_key, requirement in pairs(raw_map.special_item_chests_by_position or {}) do
        if type(requirement) == "table" and requirement.inventory_item ~= nil and inventory_contains(requirement.inventory_item) then
            local x_text, y_text = string.match(position_key, "^(%-?%d+),(%-?%d+)$")
            local x = tonumber(x_text)
            local y = tonumber(y_text)
            if x ~= nil and y ~= nil then
                local key = removed_removable_key("chest", x, y)
                if not state.removed_removable_keys[key] then
                    state.removed_removable_keys[key] = true
                    changed = true
                end
            end
        end
    end

    if player.map_id == THRONE_ROOM_MAP_ID then
        local flags = player.player_flags or 0
        local left_throne_room = math.floor(flags / F_LEFT_THROOM) % 2 == 1
        if left_throne_room then
            for _, chest in ipairs(CHEST_CANDIDATES or {}) do
                local chest_key = removed_removable_key("chest", chest.x, chest.y)
                if not state.removed_removable_keys[chest_key] then
                    state.removed_removable_keys[chest_key] = true
                    changed = true
                end
            end
            for _, door in ipairs(DOOR_CANDIDATES or {}) do
                local door_key = removed_removable_key("door", door.x, door.y)
                if not state.removed_removable_keys[door_key] then
                    state.removed_removable_keys[door_key] = true
                    changed = true
                end
            end
        end
    end

    if changed then
        reset_route_cache()
    end
end

local function confirm_taken_chest(player)
    if not player or player.map_id ~= state.active_supported_map_id then
        state.last_take_trigger_active = false
        state.pending_take_candidates = nil
        state.pending_take_frames = 0
        return
    end

    local take_active = take_trigger_active()
    if state.last_take_trigger_active and not take_active then
        local reference_player = state.previous_player
        if not reference_player or reference_player.map_id ~= state.active_supported_map_id then
            reference_player = player
        end

        local candidates = {}
        local dx, dy = facing_delta(reference_player.facing)

        candidates[#candidates + 1] = { x = reference_player.x, y = reference_player.y }
        candidates[#candidates + 1] = { x = player.x, y = player.y }
        candidates[#candidates + 1] = { x = reference_player.x + dx, y = reference_player.y + dy }
        state.pending_take_candidates = candidates
        state.pending_take_frames = CONFIG.take_confirm_frames
    end

    if state.pending_take_candidates and state.pending_take_frames > 0 then
        local treasure_bytes = read_system_treasure_window()

        local changed = false
        local still_pending = {}
        for _, candidate in ipairs(state.pending_take_candidates) do
            local key = removed_removable_key("chest", candidate.x, candidate.y)
            local chest_taken_in_system_log = system_coordinate_window_contains(treasure_bytes, candidate.x, candidate.y)

            if chest_taken_in_system_log and not state.removed_removable_keys[key] then
                state.removed_removable_keys[key] = true
                changed = true
            elseif not chest_taken_in_system_log then
                still_pending[#still_pending + 1] = candidate
            end
        end

        if changed then
            reset_route_cache()
        end

        state.pending_take_frames = state.pending_take_frames - 1
        if #still_pending == 0 or state.pending_take_frames <= 0 then
            state.pending_take_candidates = nil
            state.pending_take_frames = 0
        else
            state.pending_take_candidates = still_pending
        end
    end
    state.last_take_trigger_active = take_active
end

local function confirm_opened_door_in_front(player, previous_player)
    if not player or not previous_player then
        return
    end
    if player.map_id ~= state.active_supported_map_id or previous_player.map_id ~= state.active_supported_map_id then
        return
    end
    if previous_player.magic_keys == nil or player.magic_keys == nil then
        return
    end
    if player.magic_keys >= previous_player.magic_keys then
        return
    end

    local candidates = {}

    local dx, dy = facing_delta(previous_player.facing)
    candidates[#candidates + 1] = { x = previous_player.x + dx, y = previous_player.y + dy }
    candidates[#candidates + 1] = { x = previous_player.x, y = previous_player.y - 1 }
    candidates[#candidates + 1] = { x = previous_player.x + 1, y = previous_player.y }
    candidates[#candidates + 1] = { x = previous_player.x, y = previous_player.y + 1 }
    candidates[#candidates + 1] = { x = previous_player.x - 1, y = previous_player.y }

    local seen = {}
    for _, candidate in ipairs(candidates) do
        local key = make_key(candidate.x, candidate.y)
        if not seen[key] then
            seen[key] = true
            if tile_name_at(candidate.x, candidate.y) == "door" then
                state.removed_removable_keys[removed_removable_key("door", candidate.x, candidate.y)] = true
                reset_route_cache()
                return
            end
        end
    end
end

local function in_bounds(x, y)
    return TARGET_MAP ~= nil
        and x ~= nil
        and y ~= nil
        and x >= 0
        and y >= 0
        and x < TARGET_MAP.width
        and y < TARGET_MAP.height
end

local function in_bounds_for_map(map_id, x, y)
    local target_map = target_map_for_id(map_id)
    return target_map ~= nil
        and x ~= nil
        and y ~= nil
        and x >= 0
        and y >= 0
        and x < target_map.width
        and y < target_map.height
end

local function tile_name_at_for_map(map_id, x, y)
    local target_map = target_map_for_id(map_id)
    if not target_map then
        return nil
    end
    local row = target_map.rows[y]
    return row and row[x] or nil
end

local function border_exit_direction(x, y)
    if x == 0 then
        return "West"
    end
    if x == TARGET_MAP.width - 1 then
        return "East"
    end
    if y == 0 then
        return "North"
    end
    if y == TARGET_MAP.height - 1 then
        return "South"
    end
    return nil
end

local function border_exit_direction_for_map(map_id, x, y)
    local target_map = target_map_for_id(map_id)
    if not target_map then
        return nil
    end
    if x == 0 then
        return "West"
    end
    if x == target_map.width - 1 then
        return "East"
    end
    if y == 0 then
        return "North"
    end
    if y == target_map.height - 1 then
        return "South"
    end
    return nil
end

local function map_name_from_id(map_id)
    return MAP_NAMES[map_id] or string.format("Map %02X", map_id or 0)
end

local function border_exit_label(direction, destination_map_id)
    if destination_map_id ~= nil then
        return string.format(
            "%s exit to %s",
            string.lower(direction or "border"),
            string.lower(map_name_from_id(destination_map_id))
        )
    end
    return string.format("%s exit", string.lower(direction or "border"))
end

local function opposite_direction(direction)
    if direction == "North" then
        return "South"
    end
    if direction == "East" then
        return "West"
    end
    if direction == "South" then
        return "North"
    end
    if direction == "West" then
        return "East"
    end
    return nil
end

local function nearest_border_exit_tile(direction, x, y)
    local best_x = nil
    local best_y = nil
    local best_distance = nil

    if direction == "West" or direction == "East" then
        local target_x = direction == "West" and 0 or (TARGET_MAP.width - 1)
        for scan_y = 0, TARGET_MAP.height - 1 do
            if passability_at(target_x, scan_y) then
                local distance = abs((scan_y or 0) - (y or 0))
                if best_distance == nil or distance < best_distance then
                    best_x = target_x
                    best_y = scan_y
                    best_distance = distance
                end
            end
        end
    elseif direction == "North" or direction == "South" then
        local target_y = direction == "North" and 0 or (TARGET_MAP.height - 1)
        for scan_x = 0, TARGET_MAP.width - 1 do
            if passability_at(scan_x, target_y) then
                local distance = abs((scan_x or 0) - (x or 0))
                if best_distance == nil or distance < best_distance then
                    best_x = scan_x
                    best_y = target_y
                    best_distance = distance
                end
            end
        end
    end

    if best_x == nil or best_y == nil then
        return nil
    end

    return { x = best_x, y = best_y }
end

local function nearest_border_exit_tile_for_map(map_id, direction, x, y)
    local target_map = target_map_for_id(map_id)
    if not target_map then
        return nil
    end

    local best_x = nil
    local best_y = nil
    local best_distance = nil

    if direction == "West" or direction == "East" then
        local target_x = direction == "West" and 0 or (target_map.width - 1)
        for scan_y = 0, target_map.height - 1 do
            if passability_at_for_map(map_id, target_x, scan_y) then
                local distance = abs((scan_y or 0) - (y or 0))
                if best_distance == nil or distance < best_distance then
                    best_x = target_x
                    best_y = scan_y
                    best_distance = distance
                end
            end
        end
    elseif direction == "North" or direction == "South" then
        local target_y = direction == "North" and 0 or (target_map.height - 1)
        for scan_x = 0, target_map.width - 1 do
            if passability_at_for_map(map_id, scan_x, target_y) then
                local distance = abs((scan_x or 0) - (x or 0))
                if best_distance == nil or distance < best_distance then
                    best_x = scan_x
                    best_y = target_y
                    best_distance = distance
                end
            end
        end
    end

    if best_x == nil or best_y == nil then
        return nil
    end

    return { x = best_x, y = best_y }
end

local function nearest_border_direction_by_position_for_map(map_id, x, y)
    local target_map = target_map_for_id(map_id)
    if not target_map then
        return nil
    end

    local candidates = {
        { direction = "West", distance = abs((x or 0) - 0) },
        { direction = "East", distance = abs((target_map.width - 1) - (x or 0)) },
        { direction = "North", distance = abs((y or 0) - 0) },
        { direction = "South", distance = abs((target_map.height - 1) - (y or 0)) },
    }

    local best_direction = nil
    local best_distance = nil
    for _, candidate in ipairs(candidates) do
        local exit_tile = nearest_border_exit_tile_for_map(map_id, candidate.direction, x, y)
        if exit_tile and (best_distance == nil or candidate.distance < best_distance) then
            best_direction = candidate.direction
            best_distance = candidate.distance
        end
    end

    return best_direction
end

local function nearest_border_direction_by_position(x, y)
    local candidates = {
        { direction = "West", distance = abs((x or 0) - 0) },
        { direction = "East", distance = abs((TARGET_MAP.width - 1) - (x or 0)) },
        { direction = "North", distance = abs((y or 0) - 0) },
        { direction = "South", distance = abs((TARGET_MAP.height - 1) - (y or 0)) },
    }

    local best_direction = nil
    local best_distance = nil
    for _, candidate in ipairs(candidates) do
        local exit_tile = nearest_border_exit_tile(candidate.direction, x, y)
        if exit_tile and (best_distance == nil or candidate.distance < best_distance) then
            best_direction = candidate.direction
            best_distance = candidate.distance
        end
    end

    return best_direction
end

local function learned_exit_entities(player)
    local entities = {}
    if not CONFIG.use_persisted_learned_exits then
        return entities
    end

    local learned_by_side = state.learned_exit_by_map[player and player.map_id or -1] or {}
    for _, direction in ipairs({ "North", "East", "South", "West" }) do
        if ACTIVE_EXCLUDED_LEARNED_EXIT_DIRECTIONS[direction] then
            goto continue
        end
        local learned = learned_by_side[direction]
        if learned and in_bounds(learned.x, learned.y) and passability_at(learned.x, learned.y) then
            entities[#entities + 1] = {
                key = string.format("static:learned_exit:%d:%s:%d:%d", player.map_id or 0, string.lower(direction), learned.x, learned.y),
                kind = "static",
                type_name = "transition",
                label = border_exit_label(direction, learned.destination_map_id or learned.source_map_id),
                x = learned.x,
                y = learned.y,
                passable = true,
                selectable = true,
                exit_direction = direction,
            }
        end
        ::continue::
    end

    return entities
end

local function explicit_exit_entities(player)
    local entities = {}
    if not player then
        return entities
    end

    for _, exit_entity in ipairs(ACTIVE_EXPLICIT_EXIT_ENTITIES or {}) do
        entities[#entities + 1] = {
            key = string.format("static:explicit_exit:%d:%d:%d", player.map_id or 0, exit_entity.x, exit_entity.y),
            kind = "static",
            type_name = "transition",
            label = exit_entity.label or "exit",
            x = exit_entity.x,
            y = exit_entity.y,
            approach_x = exit_entity.approach_x,
            approach_y = exit_entity.approach_y,
            passable = false,
            selectable = true,
            exit_direction = exit_entity.exit_direction,
            destination_map_id = exit_entity.destination_map_id,
        }
    end

    return entities
end

local function is_explicit_exit_approach_tile(x, y)
    for _, exit_entity in ipairs(ACTIVE_EXPLICIT_EXIT_ENTITIES or {}) do
        if exit_entity.approach_x == x and exit_entity.approach_y == y then
            return true
        end
    end
    return false
end

local function is_learned_exit_tile(player, x, y)
    local learned_by_side = state.learned_exit_by_map[player and player.map_id or -1] or {}
    for _, direction in ipairs({ "North", "East", "South", "West" }) do
        if not ACTIVE_EXCLUDED_LEARNED_EXIT_DIRECTIONS[direction] then
            local learned = learned_by_side[direction]
            if learned and learned.x == x and learned.y == y then
                return true
            end
        end
    end
    return false
end

local function is_empty_npc_record(type_id, x, y, movement)
    return type_id == 0 and x == 0 and y == 0 and movement == 0
end

local function reset_npc_tracks()
    state.npc_tracks = {}
end

reset_route_cache = function()
    state.route_cache = nil
    state.cached_static_entities_key = nil
    state.cached_static_entities = nil
    state.cached_removable_entities_key = nil
    state.cached_removable_entities = nil
end

local function removed_removable_keys_signature()
    local keys = {}
    for key, enabled in pairs(state.removed_removable_keys or {}) do
        if enabled then
            keys[#keys + 1] = key
        end
    end
    table.sort(keys)
    return table.concat(keys, "|")
end

local function inventory_signature()
    local items = {}
    for _, item in ipairs(inventory_items()) do
        items[#items + 1] = string.format("%X", item or 0)
    end
    return table.concat(items, "")
end

local function current_runtime_flags_signature(map_id)
    local mods = mainmemory.read_u8(MODSN_SPELLS_ADDR) or 0
    local story = mainmemory.read_u8(0x00E4) or 0
    if map_id == CHARLOCK_CASTLE_MAP_ID then
        return string.format("%02X", mods)
    end
    if map_id == THRONE_ROOM_MAP_ID or map_id == SWAMP_CAVE_MAP_ID then
        local player_flags = mainmemory.read_u8(PLAYER_FLAGS_ADDR) or 0
        return string.format("%02X|%02X|%02X", mods, story, player_flags)
    end
    return string.format("%02X|%02X", mods, story)
end

local function static_entities_cache_key(player)
    return string.format(
        "%02X|%s|%s",
        player and player.map_id or 0,
        current_runtime_flags_signature(player and player.map_id or 0),
        removed_removable_keys_signature()
    )
end

local function removable_entities_cache_key(player)
    return string.format(
        "%02X|%s|%s|%s",
        player and player.map_id or 0,
        current_runtime_flags_signature(player and player.map_id or 0),
        inventory_signature(),
        removed_removable_keys_signature()
    )
end

local function count_keys(values)
    local total = 0
    for _, _ in pairs(values or {}) do
        total = total + 1
    end
    return total
end

local function update_npc_track(slot, x, y, movement)
    local frame = emu.framecount()
    local track = state.npc_tracks[slot]
    if not track then
        track = {
            first_seen_frame = frame,
            last_seen_frame = frame,
            last_x = x,
            last_y = y,
            move_events = 0,
            recent_move_frame = nil,
            unique_positions = {},
            live_x = x,
            live_y = y,
            cached_x = nil,
            cached_y = nil,
            cached_motion = nil,
        }
        state.npc_tracks[slot] = track
    end

    track.unique_positions[make_key(x, y)] = true
    track.last_seen_frame = frame
    track.live_x = x
    track.live_y = y

    if track.last_x ~= x or track.last_y ~= y then
        track.move_events = track.move_events + 1
        track.recent_move_frame = frame
        track.last_x = x
        track.last_y = y
    end

    return track
end

local function classify_motion(npc_type_id, track)
    if npc_type_id == 7 then
        return "stationary"
    end
    if not track then
        return "unknown"
    end

    local frame = emu.framecount()
    local frames_seen = frame - (track.first_seen_frame or frame) + 1
    local unique_positions = count_keys(track.unique_positions)
    local moved_recently = track.recent_move_frame and (frame - track.recent_move_frame) <= CONFIG.recent_move_window_frames

    if track.move_events == 0 and frames_seen >= CONFIG.stationary_confirm_frames then
        return "stationary"
    end
    if unique_positions >= 3 or (track.move_events >= 2 and moved_recently) then
        return "wandering"
    end
    if unique_positions == 2 or track.move_events == 1 then
        return "patrolling"
    end
    return "unknown"
end

local function cache_motion_position(track, motion, x, y)
    if not track then
        return
    end
    if motion == "stationary" or motion == "patrolling" then
        track.cached_motion = motion
        if track.cached_x == nil or track.cached_y == nil then
            track.cached_x = x
            track.cached_y = y
        end
    end
end

local function read_npcs(player)
    local npcs = {}
    if player.map_id ~= state.active_supported_map_id then
        return npcs
    end
    if player.map_id == WORLD_MAP_ID then
        return npcs
    end
    if not ACTIVE_NPCS_ENABLED then
        reset_npc_tracks()
        return npcs
    end

    for slot = 0, 19 do
        local base = 0x0051 + (slot * 3)
        local b1 = mainmemory.read_u8(base)
        local b2 = mainmemory.read_u8(base + 1)
        local b3 = mainmemory.read_u8(base + 2)

        local npc_type_id = math.floor(b1 / 0x20)
        local x = b1 % 0x20
        local facing = math.floor((b2 % 0x80) / 0x20)
        local y = b2 % 0x20
        local movement = b3

        if not is_empty_npc_record(npc_type_id, x, y, movement) then
            local track = update_npc_track(slot, x, y, movement)
            local motion = classify_motion(npc_type_id, track)
            cache_motion_position(track, motion, x, y)
            local display_x = x
            local display_y = y
            if (motion == "stationary" or motion == "patrolling")
                and track.cached_x ~= nil
                and track.cached_y ~= nil then
                display_x = track.cached_x
                display_y = track.cached_y
            end
            npcs[#npcs + 1] = {
                key = string.format("npc:%02d", slot),
                slot = slot,
                type_id = npc_type_id,
                type_name = NPC_TYPE_NAMES[npc_type_id] or ("npc type " .. tostring(npc_type_id)),
                x = display_x,
                y = display_y,
                facing = facing,
                motion = motion,
            }
        end
    end

    table.sort(npcs, function(a, b)
        return a.slot < b.slot
    end)
    return npcs
end

local function npc_label(npc)
    if npc.motion == "stationary" and npc.type_id == 7 then
        return "stationary guard"
    end
    if npc.motion == "stationary" then
        return "stationary " .. npc.type_name
    end
    if npc.motion == "patrolling" then
        return "patrolling " .. npc.type_name
    end
    if npc.motion == "wandering" then
        return "wandering " .. npc.type_name
    end
    return npc.type_name
end

local function entity_label_for_speech(entity)
    if entity and entity.kind == "delayed_interaction" then
        return title_case_words(entity.label or "Delayed Interaction")
    end
    if entity and (entity.kind == "static" or entity.kind == "removable") then
        return title_case_words(entity.label or entity.type_name or "entity")
    end
    return title_case_words(npc_label(entity))
end

local function entity_category(entity)
    if not entity then
        return "other"
    end
    if entity.kind == "delayed_interaction" then
        local override = ACTIVE_DELAYED_INTERACTION_CATEGORY_OVERRIDES[string.lower(entity.label or "")]
        if override then
            return override
        end
        return "services"
    end
    if entity.kind ~= "static" and entity.kind ~= "removable" then
        return "npcs"
    end
    if entity.type_name == "chest" then
        return "treasure"
    end
    if entity.type_name == "hidden_item" then
        return ACTIVE_HIDDEN_ITEM_CATEGORY
    end
    if entity.type_name == "door" then
        return "doors"
    end
    if entity.type_name == "transition" or entity.type_name == "stairs_up" or entity.type_name == "stairs_down" then
        return "map_exits"
    end
    if state.active_supported_map_id == WORLD_MAP_ID then
        if entity.type_name == "town"
            or entity.type_name == "castle"
            or entity.type_name == "cave"
            or entity.type_name == "shrine"
            or entity.type_name == "landmark"
        then
            return "map_exits"
        end
    end
    return "other"
end

local function active_category()
    return ENTITY_CATEGORIES[state.category_index or 1] or ENTITY_CATEGORIES[1]
end

local function category_label()
    local category = active_category()
    return category and category.label or "All"
end

local function category_matches(entity)
    local category = active_category()
    if not category or category.id == "all" then
        return true
    end
    return entity_category(entity) == category.id
end

local function filtered_entities(entities)
    if not entities or #entities == 0 then
        return {}
    end

    local filtered = {}
    for _, entity in ipairs(entities) do
        if category_matches(entity) then
            filtered[#filtered + 1] = entity
        end
    end
    return filtered
end

local function category_change_message(all_entities)
    local entities = filtered_entities(all_entities)
    if #entities == 0 then
        return string.format("Category: %s, No entities", category_label())
    end
    return string.format("Category: %s, %d entities", category_label(), #entities)
end

local function cycle_category(delta)
    local total = #ENTITY_CATEGORIES
    local next_index = (state.category_index or 1) + delta
    while next_index < 1 do
        next_index = next_index + total
    end
    while next_index > total do
        next_index = next_index - total
    end
    state.category_index = next_index
    state.selected_entity_index = 1
    state.selected_entity_key = nil
    reset_route_cache()
end

local function read_static_entities(player)
    local entities = {}
    if player.map_id ~= state.active_supported_map_id then
        return entities
    end

    local cache_key = static_entities_cache_key(player)
    if state.cached_static_entities_key == cache_key and state.cached_static_entities then
        return state.cached_static_entities
    end

    if player.map_id == WORLD_MAP_ID then
        for _, landmark in ipairs((TARGET_MAP and TARGET_MAP.landmarks) or {}) do
            entities[#entities + 1] = {
                key = string.format("landmark:%s", tostring(landmark.id or landmark.name or make_key(landmark.x, landmark.y))),
                kind = "static",
                type_name = landmark.kind or "landmark",
                label = landmark.name or landmark.kind or "landmark",
                x = landmark.x,
                y = landmark.y,
                passable = true,
                selectable = true,
            }
        end

        table.sort(entities, function(a, b)
            if a.y ~= b.y then
                return a.y < b.y
            end
            if a.x ~= b.x then
                return a.x < b.x
            end
            return a.label < b.label
        end)

        state.cached_static_entities_key = cache_key
        state.cached_static_entities = entities
        return entities
    end

    for y = 0, TARGET_MAP.height - 1 do
        for x = 0, TARGET_MAP.width - 1 do
            local tile_name = resolved_removable_tile_name(x, y)
            if player.map_id == CHARLOCK_CASTLE_MAP_ID and x == 10 and y == 1 then
                local mods = mainmemory.read_u8(MODSN_SPELLS_ADDR) or 0
                local secret_passage_found = bit and bit.band and bit.band(mods, F_PSG_FOUND) ~= 0 or false
                if not secret_passage_found then
                    goto continue
                end
            end
            if entity_position_excluded(tile_name, x, y) then
                goto continue
            end
            if tile_name == "chest" or tile_name == "door" then
                goto continue
            end
            local label = STATIC_ENTITY_TILE_LABELS[tile_name]
            if label and (tile_name == "stairs_up" or tile_name == "stairs_down") then
                local transition = find_transition_at(player.map_id, x, y)
                if transition and transition.destination_map_id ~= nil then
                    local transition_label = transition.label
                    local normalized_transition_label = string.lower((transition_label or ""):gsub("^%s+", ""):gsub("%s+$", ""))
                    if transition_label and normalized_transition_label ~= "" and normalized_transition_label ~= "stairs" then
                        label = transition.label
                    else
                        label = string.format("stairs to %s", string.lower(map_name_from_id(transition.destination_map_id)))
                    end
                end
            end
            if label then
                entities[#entities + 1] = {
                    key = string.format("static:%s:%d:%d", tile_name, x, y),
                    kind = "static",
                    type_name = tile_name,
                    label = label,
                    x = x,
                    y = y,
                    passable = passability_at(x, y),
                    selectable = STATIC_ENTITY_TILE_LABELS[tile_name] ~= nil,
                }
            end
            ::continue::
        end
    end

    for _, entity in ipairs(learned_exit_entities(player)) do
        entities[#entities + 1] = {
            key = entity.key,
            kind = entity.kind,
            type_name = entity.type_name,
            label = entity.label,
            x = entity.x,
            y = entity.y,
            passable = entity.passable,
            selectable = entity.selectable,
            exit_direction = entity.exit_direction,
        }
    end

    for _, entity in ipairs(explicit_exit_entities(player)) do
        entities[#entities + 1] = entity
    end

    table.sort(entities, function(a, b)
        if a.y ~= b.y then
            return a.y < b.y
        end
        if a.x ~= b.x then
            return a.x < b.x
        end
        return a.label < b.label
    end)

    state.cached_static_entities_key = cache_key
    state.cached_static_entities = entities
    return entities
end

local function read_removable_entities(player)
    local entities = {}
    if player.map_id ~= state.active_supported_map_id then
        return entities
    end

    local cache_key = removable_entities_cache_key(player)
    if state.cached_removable_entities_key == cache_key and state.cached_removable_entities then
        return state.cached_removable_entities
    end

    if player.map_id == WORLD_MAP_ID then
        for _, hidden_item in ipairs(HIDDEN_ITEM_CANDIDATES) do
            if hidden_item_visible(hidden_item) then
                entities[#entities + 1] = {
                    key = string.format("removable:hidden_item:%d:%d", hidden_item.x, hidden_item.y),
                    kind = "removable",
                    type_name = "hidden_item",
                    label = hidden_item.label,
                    x = hidden_item.x,
                    y = hidden_item.y,
                    passable = true,
                    selectable = true,
                }
            end
        end
        state.cached_removable_entities_key = cache_key
        state.cached_removable_entities = entities
        return entities
    end

    for _, chest in ipairs(CHEST_CANDIDATES) do
        if entity_position_excluded("chest", chest.x, chest.y) then
            goto continue_chest
        end
        if not state.removed_removable_keys[removed_removable_key("chest", chest.x, chest.y)] then
            entities[#entities + 1] = {
                key = string.format("removable:chest:%d:%d", chest.x, chest.y),
                kind = "removable",
                type_name = "chest",
                label = "chest",
                x = chest.x,
                y = chest.y,
                passable = true,
                selectable = true,
            }
        end
        ::continue_chest::
    end

    for _, door in ipairs(DOOR_CANDIDATES) do
        if entity_position_excluded("door", door.x, door.y) then
            goto continue_door
        end
        if not state.removed_removable_keys[removed_removable_key("door", door.x, door.y)] then
            entities[#entities + 1] = {
                key = string.format("removable:door:%d:%d", door.x, door.y),
                kind = "removable",
                type_name = "door",
                label = "door",
                x = door.x,
                y = door.y,
                passable = false,
                selectable = true,
            }
        end
        ::continue_door::
    end

    for _, hidden_item in ipairs(HIDDEN_ITEM_CANDIDATES) do
        if hidden_item_visible(hidden_item) then
            entities[#entities + 1] = {
                key = string.format("removable:hidden_item:%d:%d", hidden_item.x, hidden_item.y),
                kind = "removable",
                type_name = "hidden_item",
                label = hidden_item.label,
                x = hidden_item.x,
                y = hidden_item.y,
                passable = true,
                selectable = true,
            }
        end
    end

    table.sort(entities, function(a, b)
        if a.y ~= b.y then
            return a.y < b.y
        end
        if a.x ~= b.x then
            return a.x < b.x
        end
        return (a.label or "") < (b.label or "")
    end)

    state.cached_removable_entities_key = cache_key
    state.cached_removable_entities = entities
    return entities
end

local function grid_distance(a_x, a_y, b_x, b_y)
    return abs((a_x or 0) - (b_x or 0)) + abs((a_y or 0) - (b_y or 0))
end

local function find_stationary_npc_near_table(table_entity, npcs)
    for _, npc in ipairs(npcs or {}) do
        if npc.motion == "stationary" and grid_distance(table_entity.x, table_entity.y, npc.x, npc.y) == 1 then
            return npc
        end
    end
    return nil
end

local function delayed_interaction_order(entity)
    if not entity then
        return math.huge
    end
    local position_key = string.format("%d,%d", entity.x or -1, entity.y or -1)
    local order = ACTIVE_DELAYED_INTERACTION_ORDER_BY_POSITION[position_key]
    if type(order) == "number" then
        return order
    end
    return math.huge
end

local function build_delayed_interactions(static_entities, npcs)
    local delayed = {}
    local matched_table_keys = {}
    local allow_npc_counter_services = map_allows_npc_counter_services(state.active_supported_map_id)
    local emitted_positions = {}

    if state.active_supported_map_id == WORLD_MAP_ID then
        return delayed, matched_table_keys
    end

    for _, entity in ipairs(static_entities or {}) do
        if entity.type_name == "table" then
            local npc = allow_npc_counter_services and find_stationary_npc_near_table(entity, npcs) or nil
            local hardcoded_label = DELAYED_INTERACTION_LABELS_BY_POSITION[string.format("%d,%d", entity.x, entity.y)]
            local hardcoded_approach = ACTIVE_DELAYED_INTERACTION_APPROACHES_BY_POSITION[string.format("%d,%d", entity.x, entity.y)]
            if hardcoded_label or npc then
                local counter_label = hardcoded_label or (npc_label(npc) .. " counter")
                delayed[#delayed + 1] = {
                    key = hardcoded_label and string.format("delayed:%s:service", entity.key)
                        or string.format("delayed:%s:%s", entity.key, npc.key),
                    kind = "delayed_interaction",
                    type_name = "delayed_interaction",
                    label = counter_label,
                    x = entity.x,
                    y = entity.y,
                    approach_x = hardcoded_approach and hardcoded_approach.x or nil,
                    approach_y = hardcoded_approach and hardcoded_approach.y or nil,
                    passable = false,
                    table_x = entity.x,
                    table_y = entity.y,
                    npc_x = npc and npc.x or nil,
                    npc_y = npc and npc.y or nil,
                    npc_key = npc and npc.key or nil,
                    npc_type_name = npc and npc.type_name or nil,
                }
                emitted_positions[string.format("%d,%d", entity.x, entity.y)] = true
                matched_table_keys[entity.key] = true
            end
        end
    end

    for position_key, hardcoded_label in pairs(DELAYED_INTERACTION_LABELS_BY_POSITION or {}) do
        if not emitted_positions[position_key] and hardcoded_label and hardcoded_label ~= "" then
            local x_text, y_text = string.match(position_key, "^(%-?%d+),(%-?%d+)$")
            local x = tonumber(x_text)
            local y = tonumber(y_text)
            local hardcoded_approach = ACTIVE_DELAYED_INTERACTION_APPROACHES_BY_POSITION[position_key]
            if x ~= nil and y ~= nil and in_bounds(x, y) then
                delayed[#delayed + 1] = {
                    key = string.format("delayed:hardcoded:%d:%d", x, y),
                    kind = "delayed_interaction",
                    type_name = "delayed_interaction",
                    label = hardcoded_label,
                    x = x,
                    y = y,
                    approach_x = hardcoded_approach and hardcoded_approach.x or nil,
                    approach_y = hardcoded_approach and hardcoded_approach.y or nil,
                    passable = false,
                    table_x = x,
                    table_y = y,
                    npc_x = nil,
                    npc_y = nil,
                    npc_key = nil,
                    npc_type_name = nil,
                }
                emitted_positions[position_key] = true
            end
        end
    end

    table.sort(delayed, function(a, b)
        local order_a = delayed_interaction_order(a)
        local order_b = delayed_interaction_order(b)
        if order_a ~= order_b then
            return order_a < order_b
        end
        if a.y ~= b.y then
            return a.y < b.y
        end
        if a.x ~= b.x then
            return a.x < b.x
        end
        return (a.label or "") < (b.label or "")
    end)

    return delayed, matched_table_keys
end

local function current_entities(player, npcs)
    local entities = {}
    local static_entities = read_static_entities(player)
    local removable_entities = read_removable_entities(player)
    local delayed, matched_table_keys = build_delayed_interactions(static_entities, npcs)

    for _, entity in ipairs(delayed) do
        entities[#entities + 1] = entity
    end
    for _, npc in ipairs(npcs or {}) do
        entities[#entities + 1] = npc
    end
    for _, entity in ipairs(removable_entities) do
        entities[#entities + 1] = entity
    end
    for _, entity in ipairs(static_entities) do
        if entity.selectable ~= false and not matched_table_keys[entity.key] then
            entities[#entities + 1] = entity
        end
    end

    return entities
end

local function format_entity_callout(player, entity, total_entities)
    if not entity then
        return "No entities."
    end

    local target_x = entity.approach_x or entity.x
    local target_y = entity.approach_y or entity.y
    local distance = grid_distance(player.x, player.y, target_x, target_y)
    local direction = combined_direction(target_x - player.x, target_y - player.y)
    return string.format(
        "%d, %s, %d Steps %s, %d of %d.",
        state.selected_entity_index or 1,
        entity_label_for_speech(entity),
        distance,
        direction,
        state.selected_entity_index or 1,
        total_entities or 0
    )
end

local function clamp_selection(entities)
    if not entities or #entities == 0 then
        state.selected_entity_index = 1
        state.selected_entity_key = nil
        return
    end
    if state.selected_entity_index < 1 then
        state.selected_entity_index = #entities
    elseif state.selected_entity_index > #entities then
        state.selected_entity_index = 1
    end
end

local function refresh_selected_entity(entities)
    if not entities or #entities == 0 then
        state.selected_entity_index = 1
        state.selected_entity_key = nil
        return nil
    end

    if state.selected_entity_key then
        for index, entity in ipairs(entities) do
            if entity.key == state.selected_entity_key then
                state.selected_entity_index = index
                return entity
            end
        end
    end

    clamp_selection(entities)
    local entity = entities[state.selected_entity_index]
    state.selected_entity_key = entity and entity.key or nil
    return entity
end

local function selected_entity(entities)
    return refresh_selected_entity(entities)
end

local function direction_pressed(keys)
    return keyboard_pressed(keys, "Up")
        or keyboard_pressed(keys, "Down")
        or keyboard_pressed(keys, "Left")
        or keyboard_pressed(keys, "Right")
end

local function sync_selected_patrolling_entity_from_live(keys, entity)
    if not entity or entity.kind == "static" or entity.motion ~= "patrolling" then
        return entity
    end
    if not direction_pressed(keys) then
        return entity
    end

    local track = state.npc_tracks[entity.slot]
    if not track or track.live_x == nil or track.live_y == nil then
        return entity
    end

    track.cached_x = track.live_x
    track.cached_y = track.live_y
    track.cached_motion = "patrolling"
    entity.x = track.cached_x
    entity.y = track.cached_y
    return entity
end

local function move_selection(delta, entities)
    if not entities or #entities == 0 then
        state.selected_entity_index = 1
        state.selected_entity_key = nil
        return nil
    end
    refresh_selected_entity(entities)
    state.selected_entity_index = state.selected_entity_index + delta
    clamp_selection(entities)
    local entity = entities[state.selected_entity_index]
    state.selected_entity_key = entity and entity.key or nil
    return entity
end

local function inferred_transition_direction(supported_map_id, player, previous_player, target_map_state)
    local direction = border_exit_direction_for_map(supported_map_id, target_map_state.x, target_map_state.y)
    if direction then
        return direction
    end

    direction = nearest_border_direction_by_position_for_map(supported_map_id, target_map_state.x, target_map_state.y)
    if direction then
        return direction
    end

    if previous_player and previous_player.map_id == supported_map_id then
        return title_case_words(facing_name(previous_player.facing))
    end

    if player and player.map_id == supported_map_id then
        return opposite_direction(title_case_words(facing_name(player.facing)))
    end

    return nil
end

local function remember_target_map_exit(player, previous_player)
    if not CONFIG.enable_border_exit_learning then
        return
    end
    if not player or not previous_player or previous_player.map_id == player.map_id then
        return
    end

    local entering_from_world = previous_player.map_id == WORLD_MAP_ID and map_is_supported(player.map_id) and player.map_id ~= WORLD_MAP_ID
    local leaving_to_world = player.map_id == WORLD_MAP_ID and map_is_supported(previous_player.map_id) and previous_player.map_id ~= WORLD_MAP_ID
    if not entering_from_world and not leaving_to_world then
        return
    end

    local supported_map_id = entering_from_world and player.map_id or previous_player.map_id
    if not map_allows_world_map_exit_learning(supported_map_id) then
        return
    end

    local target_map_state = nil
    local destination_map_id = nil
    if entering_from_world then
        target_map_state = player
        destination_map_id = WORLD_MAP_ID
    elseif leaving_to_world then
        target_map_state = state.last_supported_tiles_by_map[supported_map_id] or previous_player
        destination_map_id = WORLD_MAP_ID
    else
        return
    end

    if target_map_state.map_id ~= supported_map_id then
        return
    end

    if not in_bounds_for_map(supported_map_id, target_map_state.x, target_map_state.y) then
        return
    end

    if not passability_at_for_map(supported_map_id, target_map_state.x, target_map_state.y) then
        return
    end

    if find_transition_at(supported_map_id, target_map_state.x, target_map_state.y) then
        return
    end

    local direction = inferred_transition_direction(supported_map_id, player, previous_player, target_map_state)
    if not direction then
        return
    end

    local exit_tile = nearest_border_exit_tile_for_map(supported_map_id, direction, target_map_state.x, target_map_state.y)
    if not exit_tile then
        return
    end

    local learned_map_id = target_map_state.map_id
    state.learned_exit_by_map[learned_map_id] = state.learned_exit_by_map[learned_map_id] or {}
    state.learned_exit_by_map[learned_map_id][direction] = {
        x = exit_tile.x,
        y = exit_tile.y,
        direction = direction,
        destination_map_id = destination_map_id,
    }
    persist_learned_exits()
    console.log(string.format(
        "[pathfinder-engine] learned %s at %d, %d to map %02X",
        border_exit_label(direction, destination_map_id),
        exit_tile.x or -1,
        exit_tile.y or -1,
        destination_map_id or 0
    ))
end

local function handle_map_transition(player)
    if player.map_id ~= state.last_announced_map_id then
        remember_target_map_exit(player, state.previous_player)
        state.last_announced_map_id = player.map_id
        if player.map_id == state.active_supported_map_id then
            speak("Entering " .. map_name_from_id(player.map_id) .. ".", true)
        end
    end
end

local function in_grid(passability, x, y)
    return passability ~= nil
        and x ~= nil
        and y ~= nil
        and x >= 0
        and y >= 0
        and x < (passability.width or 0)
        and y < (passability.height or 0)
        and passability.cells ~= nil
        and passability.cells[y] ~= nil
        and passability.cells[y][x] ~= nil
end

local function build_passability(player, selected, npcs)
    local cells = {}
    for y = 0, TARGET_MAP.height - 1 do
        cells[y] = {}
        for x = 0, TARGET_MAP.width - 1 do
            local terrain = resolved_removable_tile_name(x, y)
            local border_blocked = ACTIVE_BORDER_BLOCKING
                and border_exit_direction(x, y) ~= nil
                and not border_open_position(x, y)
                and not is_explicit_exit_approach_tile(x, y)
                and not is_learned_exit_tile(player, x, y)
            cells[y][x] = {
                x = x,
                y = y,
                terrain = terrain,
                passable = resolved_passability_at(x, y) and not border_blocked,
            }
        end
    end

    for _, npc in ipairs(npcs or {}) do
        if npc.x ~= nil and npc.y ~= nil and cells[npc.y] and cells[npc.y][npc.x] then
            cells[npc.y][npc.x].passable = false
        end
    end

    if selected and selected.kind ~= "static" and selected.x ~= nil and selected.y ~= nil and cells[selected.y] and cells[selected.y][selected.x] then
        cells[selected.y][selected.x].passable = (selected.passable == true)
    end

    if in_bounds(player.x, player.y) then
        cells[player.y][player.x].passable = true
    end

    return {
        width = TARGET_MAP.width,
        height = TARGET_MAP.height,
        cells = cells,
    }
end

local function swamp_warning_message(player)
    if state.hazard_warnings_enabled == false then
        state.last_swamp_warning_key = nil
        state.last_swamp_warning_frame = nil
        return nil
    end
    if not player or player.map_id ~= state.active_supported_map_id then
        state.last_swamp_warning_key = nil
        state.last_swamp_warning_frame = nil
        return nil
    end
    if not TARGET_MAP or player.map_id == WORLD_MAP_ID then
        state.last_swamp_warning_key = nil
        state.last_swamp_warning_frame = nil
        return nil
    end
    if not in_bounds(player.x, player.y) then
        state.last_swamp_warning_key = nil
        state.last_swamp_warning_frame = nil
        return nil
    end

    local hazard_labels = {
        swamp = "Swamp",
        force_field = "Force field",
    }

    local directions = {
        { label = "north", dx = 0, dy = -1 },
        { label = "east", dx = 1, dy = 0 },
        { label = "south", dx = 0, dy = 1 },
        { label = "west", dx = -1, dy = 0 },
    }

    for _, direction in ipairs(directions) do
        local near_x = player.x + direction.dx
        local near_y = player.y + direction.dy
        local far_x = player.x + (direction.dx * 2)
        local far_y = player.y + (direction.dy * 2)

        if in_bounds(near_x, near_y) and in_bounds(far_x, far_y) then
            local near_tile = resolved_removable_tile_name(near_x, near_y)
            local far_tile = resolved_removable_tile_name(far_x, far_y)
            local hazard_label = hazard_labels[far_tile]
            if resolved_passability_at(near_x, near_y) and hazard_label then
                local warning_key = string.format("%d,%d:%s", far_x, far_y, far_tile)
                if state.last_swamp_warning_key ~= warning_key then
                    local frame = emu.framecount()
                    if state.last_swamp_warning_frame
                        and (frame - state.last_swamp_warning_frame) < CONFIG.hazard_warning_cooldown_frames then
                        return nil
                    end
                    state.last_swamp_warning_key = warning_key
                    state.last_swamp_warning_frame = frame
                    return string.format("Alert: %s, 2 steps %s", hazard_label, direction.label)
                end
                return nil
            end
        end
    end

    state.last_swamp_warning_key = nil
    state.last_swamp_warning_frame = nil
    return nil
end

local function npc_matches_counter_presence_rule(npc, rule)
    if not npc or type(rule) ~= "table" then
        return false
    end
    if npc.x ~= rule.npc_x or npc.y ~= rule.npc_y then
        return false
    end
    if rule.npc_type_id ~= nil and npc.type_id ~= rule.npc_type_id then
        return false
    end
    return true
end

local function counter_presence_message(player, npcs)
    if not player or player.map_id ~= state.active_supported_map_id then
        state.last_counter_presence_key = nil
        return nil
    end

    local position_key = string.format("%d,%d", player.x or -1, player.y or -1)
    local rule = ACTIVE_COUNTER_PRESENCE_RULES_BY_APPROACH[position_key]
    if type(rule) ~= "table" then
        state.last_counter_presence_key = nil
        return nil
    end

    local ready = false
    for _, npc in ipairs(npcs or {}) do
        if npc_matches_counter_presence_rule(npc, rule) then
            ready = true
            break
        end
    end

    local presence_key = position_key .. ":" .. (ready and "ready" or "working")
    if state.last_counter_presence_key == presence_key then
        return nil
    end

    state.last_counter_presence_key = presence_key
    if ready then
        return rule.ready_message or "Shopkeeper is ready"
    end
    return rule.working_message or "Shopkeeper is working"
end

local function reconstruct_path(came_from, current_key)
    local path = {}
    local key = current_key
    while key do
        local x, y = string.match(key, "(-?%d+):(-?%d+)")
        path[#path + 1] = { x = tonumber(x), y = tonumber(y) }
        key = came_from[key]
    end
    local reversed = {}
    for index = #path, 1, -1 do
        reversed[#reversed + 1] = path[index]
    end
    return reversed
end

local function ordered_neighbors(current_x, current_y, goal_x, goal_y)
    local dx = goal_x - current_x
    local dy = goal_y - current_y

    local horizontal_first = abs(dx) >= abs(dy)
    local toward_horizontal = dx < 0 and { x = current_x - 1, y = current_y } or { x = current_x + 1, y = current_y }
    local away_horizontal = dx < 0 and { x = current_x + 1, y = current_y } or { x = current_x - 1, y = current_y }
    local toward_vertical = dy < 0 and { x = current_x, y = current_y - 1 } or { x = current_x, y = current_y + 1 }
    local away_vertical = dy < 0 and { x = current_x, y = current_y + 1 } or { x = current_x, y = current_y - 1 }

    if dx == 0 then
        toward_horizontal = { x = current_x + 1, y = current_y }
        away_horizontal = { x = current_x - 1, y = current_y }
    end
    if dy == 0 then
        toward_vertical = { x = current_x, y = current_y + 1 }
        away_vertical = { x = current_x, y = current_y - 1 }
    end

    if horizontal_first then
        return {
            toward_horizontal,
            toward_vertical,
            away_vertical,
            away_horizontal,
        }
    end

    return {
        toward_vertical,
        toward_horizontal,
        away_horizontal,
        away_vertical,
    }
end

local function find_bfs_path(passability, start_x, start_y, goal_x, goal_y)
    if not in_grid(passability, start_x, start_y) or not in_grid(passability, goal_x, goal_y) then
        return nil
    end
    if not passability.cells[goal_y][goal_x].passable and not (goal_x == start_x and goal_y == start_y) then
        return nil
    end

    local start_key = make_key(start_x, start_y)
    local goal_key = make_key(goal_x, goal_y)
    local came_from = {}
    local visited = { [start_key] = true }
    local queue = { { x = start_x, y = start_y, key = start_key } }
    local head = 1

    while head <= #queue do
        local current = queue[head]
        head = head + 1
        if current.key == goal_key then
            return reconstruct_path(came_from, current.key)
        end

        for _, neighbor in ipairs(ordered_neighbors(current.x, current.y, goal_x, goal_y)) do
            if in_grid(passability, neighbor.x, neighbor.y) and passability.cells[neighbor.y][neighbor.x].passable then
                local neighbor_key = make_key(neighbor.x, neighbor.y)
                if not visited[neighbor_key] then
                    visited[neighbor_key] = true
                    came_from[neighbor_key] = current.key
                    queue[#queue + 1] = { x = neighbor.x, y = neighbor.y, key = neighbor_key }
                end
            end
        end
    end

    return nil
end

local function best_adjacent_goal(passability, start_x, start_y, entity)
    local candidates = {
        { x = entity.x, y = entity.y - 1 },
        { x = entity.x + 1, y = entity.y },
        { x = entity.x, y = entity.y + 1 },
        { x = entity.x - 1, y = entity.y },
    }

    local best_goal = nil
    local best_path = nil
    local best_length = nil

    for _, candidate in ipairs(candidates) do
        if in_grid(passability, candidate.x, candidate.y) and passability.cells[candidate.y][candidate.x].passable then
            local path = find_bfs_path(passability, start_x, start_y, candidate.x, candidate.y)
            if path then
                local length = #path
                if not best_goal or length < best_length then
                    best_goal = candidate
                    best_path = path
                    best_length = length
                end
            end
        end
    end

    return best_goal, best_path
end

local function route_goal_for_entity(passability, start_x, start_y, entity)
    if entity and entity.exit_direction and entity.x == start_x and entity.y == start_y then
        return { x = start_x, y = start_y }, {
            { x = start_x, y = start_y },
        }
    end

    if entity and entity.approach_x ~= nil and entity.approach_y ~= nil then
        local path = find_bfs_path(passability, start_x, start_y, entity.approach_x, entity.approach_y)
        if path then
            return { x = entity.approach_x, y = entity.approach_y }, path
        end
        return nil, nil
    end

    if entity.kind == "static" or entity.kind == "removable" then
        if entity.passable and in_grid(passability, entity.x, entity.y) and passability.cells[entity.y][entity.x].passable then
            local path = find_bfs_path(passability, start_x, start_y, entity.x, entity.y)
            if path then
                return { x = entity.x, y = entity.y }, path
            end
        end
        return best_adjacent_goal(passability, start_x, start_y, entity)
    end

    return best_adjacent_goal(passability, start_x, start_y, entity)
end

local function path_key(node)
    return make_key(node and node.x, node and node.y)
end

local function copy_path(path, start_index)
    local copied = {}
    for index = start_index or 1, #(path or {}) do
        copied[#copied + 1] = {
            x = path[index].x,
            y = path[index].y,
        }
    end
    return copied
end

local function merge_paths(prefix, suffix)
    if not prefix or #prefix == 0 then
        return copy_path(suffix, 1)
    end
    if not suffix or #suffix == 0 then
        return copy_path(prefix, 1)
    end

    local merged = copy_path(prefix, 1)
    local start_index = 1
    if path_key(prefix[#prefix]) == path_key(suffix[1]) then
        start_index = 2
    end
    for index = start_index, #suffix do
        merged[#merged + 1] = {
            x = suffix[index].x,
            y = suffix[index].y,
        }
    end
    return merged
end

local function path_contains_position(path, x, y)
    for index, node in ipairs(path or {}) do
        if node.x == x and node.y == y then
            return index
        end
    end
    return nil
end

local function best_rejoin_path(passability, start_x, start_y, cached_path)
    local best_combined = nil
    local best_score = nil

    for index = 1, #(cached_path or {}) do
        local node = cached_path[index]
        local approach = find_bfs_path(passability, start_x, start_y, node.x, node.y)
        if approach then
            local remainder = copy_path(cached_path, index)
            local combined = merge_paths(approach, remainder)
            local score = #combined
            if not best_combined or score < best_score then
                best_combined = combined
                best_score = score
            end
        end
    end

    return best_combined
end

local function incremental_path_to_goal(passability, player, entity, goal, fresh_path)
    local cache = state.route_cache
    if not cache or cache.map_id ~= player.map_id or cache.entity_key ~= entity.key or cache.goal_key ~= make_key(goal.x, goal.y) then
        if fresh_path then
            state.route_cache = {
                map_id = player.map_id,
                entity_key = entity.key,
                goal_key = make_key(goal.x, goal.y),
                path = copy_path(fresh_path, 1),
            }
        else
            state.route_cache = nil
        end
        return fresh_path
    end

    local cached_path = cache.path or {}
    local current_index = path_contains_position(cached_path, player.x, player.y)
    if current_index then
        local suffix = copy_path(cached_path, current_index)
        state.route_cache.path = copy_path(suffix, 1)
        return suffix
    end

    local repaired = best_rejoin_path(passability, player.x, player.y, cached_path)
    if repaired then
        state.route_cache.path = copy_path(repaired, 1)
        return repaired
    end

    if fresh_path then
        state.route_cache.path = copy_path(fresh_path, 1)
        return fresh_path
    end

    state.route_cache = nil
    return nil
end

local function path_to_steps(path)
    if not path or #path <= 1 then
        return nil
    end
    local steps = {}
    local current_direction = nil
    local current_count = 0
    for index = 2, #path do
        local dx = path[index].x - path[index - 1].x
        local dy = path[index].y - path[index - 1].y
        local direction = combined_direction(dx, dy)
        if direction == current_direction then
            current_count = current_count + 1
        else
            if current_direction then
                steps[#steps + 1] = { direction = current_direction, count = current_count }
            end
            current_direction = direction
            current_count = 1
        end
    end
    if current_direction then
        steps[#steps + 1] = { direction = current_direction, count = current_count }
    end
    return steps
end

local function append_transition_step(steps, entity, goal)
    if not entity or not entity.exit_direction or not goal then
        return steps
    end

    local dx = (entity.x or 0) - goal.x
    local dy = (entity.y or 0) - goal.y
    local movement_direction = nil
    if dx == 0 and dy == -1 then
        movement_direction = "North"
    elseif dx == 1 and dy == 0 then
        movement_direction = "East"
    elseif dx == 0 and dy == 1 then
        movement_direction = "South"
    elseif dx == -1 and dy == 0 then
        movement_direction = "West"
    end

    if movement_direction ~= entity.exit_direction then
        return steps
    end

    local copied = {}
    for _, step in ipairs(steps or {}) do
        copied[#copied + 1] = {
            direction = step.direction,
            count = step.count,
        }
    end

    if #copied > 0 and copied[#copied].direction == entity.exit_direction then
        copied[#copied].count = copied[#copied].count + 1
    else
        copied[#copied + 1] = {
            direction = entity.exit_direction,
            count = 1,
        }
    end

    return copied
end

local function format_route_steps(steps)
    if not steps or #steps == 0 then
        return nil
    end
    local parts = {}
    for _, step in ipairs(steps) do
        parts[#parts + 1] = string.format("%s %d", step.direction, step.count)
    end
    return table.concat(parts, ", ") .. "."
end

local function format_path_nodes(path)
    if not path or #path == 0 then
        return "none"
    end

    local parts = {}
    for _, node in ipairs(path) do
        parts[#parts + 1] = string.format("(%d,%d)", node.x or 0, node.y or 0)
    end
    return table.concat(parts, " -> ")
end

local function route_steps_to_target(player, entity)
    if not entity then
        return "No entity selected."
    end
    if player.map_id ~= state.active_supported_map_id then
        return "Enter " .. map_name_from_id(state.active_supported_map_id) .. " first."
    end

    local passability = build_passability(player, entity, read_npcs(player))
    local goal, path = route_goal_for_entity(passability, player.x, player.y, entity)

    if not goal or not path then
        return "No path."
    end

    path = incremental_path_to_goal(passability, player, entity, goal, path)

    local steps = append_transition_step(path_to_steps(path), entity, goal)
    return format_route_steps(steps) or "Already there."
end

local function entity_has_path(player, entity)
    if not entity or player.map_id ~= state.active_supported_map_id then
        return false
    end

    local passability = build_passability(player, entity, read_npcs(player))
    local goal, path = route_goal_for_entity(passability, player.x, player.y, entity)
    return goal ~= nil and path ~= nil
end

local function teleport_target_for_direction(entity, direction)
    if not entity or not direction then
        return nil
    end

    return {
        x = entity.x + direction.dx,
        y = entity.y + direction.dy,
        direction_name = direction.name,
    }
end

local function teleport_in_bounds(player, x, y)
    if not player or player.map_id ~= state.active_supported_map_id then
        return false
    end
    return in_bounds(x, y)
end

local function teleport_player_to(player, entity, direction)
    if not entity then
        return "No entity selected."
    end
    if player.map_id ~= state.active_supported_map_id then
        return "Not in " .. map_name_from_id(state.active_supported_map_id) .. "."
    end

    local target = teleport_target_for_direction(entity, direction)
    if not target or not teleport_in_bounds(player, target.x, target.y) then
        return "Teleport target out of bounds."
    end

    mainmemory.write_u8(0x003A, target.x)
    mainmemory.write_u8(0x003B, target.y)
    return string.format(
        "Teleporting %s of %s.",
        title_case_words(target.direction_name),
        entity_label_for_speech(entity)
    )
end

local function run_frame()
    local keys = _G.DWA_PATHFINDER_KEYS or input.get()
    local assist_settings = load_assist_settings()
    if type(assist_settings.pathfinding_filter_enabled) == "boolean" then
        state.pathfinding_filter_enabled = assist_settings.pathfinding_filter_enabled
    end
    if type(assist_settings.hazard_warnings_enabled) == "boolean" then
        state.hazard_warnings_enabled = assist_settings.hazard_warnings_enabled
    end
    local player = read_player_state()

    if _G.DWA_NON_GAMEPLAY_ACTIVE then
        state.previous_player = player
        return
    end

    if map_is_supported(player.map_id) then
        local supported_map_changed = player.map_id ~= state.active_supported_map_id
        set_active_supported_map(player.map_id)
        local runtime_key = world_runtime_state_key()
        if supported_map_changed or runtime_key ~= state.last_world_runtime_state_key then
            apply_world_runtime_overrides()
            refresh_active_map_candidates()
            state.last_world_runtime_state_key = runtime_key
        end
    end

    if player.map_id == state.active_supported_map_id and in_bounds(player.x, player.y) and resolved_passability_at(player.x, player.y) then
        state.last_supported_tiles_by_map[player.map_id] = {
            map_id = player.map_id,
            x = player.x,
            y = player.y,
            facing = player.facing,
        }
    end

    if player.map_id ~= state.current_map_id then
        state.current_map_id = player.map_id
        state.selected_entity_index = 1
        state.selected_entity_key = nil
        reset_npc_tracks()
        reset_route_cache()
        state.removed_removable_keys = {}
        state.last_take_trigger_active = false
        state.last_counter_presence_key = nil
        state.last_world_runtime_state_key = nil
        if player.map_id == state.active_supported_map_id then
            sync_removed_chests_from_treasure_log(player)
            sync_removed_doors_from_door_log(player)
            apply_special_map_overrides(player)
        end
    elseif state.previous_player and state.previous_player.map_id == state.active_supported_map_id then
        confirm_opened_door_in_front(player, state.previous_player)
    end

    confirm_taken_chest(player)
    if player.map_id == state.active_supported_map_id then
        apply_special_map_overrides(player)
    end

    handle_map_transition(player)
    local npcs = read_npcs(player)
    local all_entities = current_entities(player, npcs)
    local entities = filtered_entities(all_entities)
    local current_entity = refresh_selected_entity(entities)
    sync_selected_patrolling_entity_from_live(keys, current_entity)
    local swamp_warning = swamp_warning_message(player)
    if swamp_warning then
        speak(swamp_warning, true)
    end
    local counter_presence = counter_presence_message(player, npcs)
    if counter_presence then
        speak(counter_presence, true)
    end

    if shift_modified_pressed(keys, CONFIG.navigate_entity_hotkey) then
        state.pathfinding_filter_enabled = not state.pathfinding_filter_enabled
        persist_pathfinding_filter_setting()
        reset_route_cache()
        if state.pathfinding_filter_enabled then
            speak("Pathfinding filter on.", true)
        else
            speak("Pathfinding filter off.", true)
        end
    elseif shift_modified_pressed(keys, CONFIG.previous_entity_hotkey) then
        cycle_category(-1)
        speak(category_change_message(all_entities), true)
    elseif shift_modified_pressed(keys, CONFIG.next_entity_hotkey) then
        cycle_category(1)
        speak(category_change_message(all_entities), true)
    elseif shift_modified_pressed(keys, CONFIG.repeat_entity_hotkey) then
        state.category_index = 1
        state.selected_entity_index = 1
        state.selected_entity_key = nil
        reset_route_cache()
        speak(category_change_message(all_entities), true)
    elseif keyboard_pressed(keys, CONFIG.previous_entity_hotkey) then
        local entity = nil
        if state.pathfinding_filter_enabled and entities and #entities > 0 then
            for _ = 1, #entities do
                entity = move_selection(-1, entities)
                if entity_has_path(player, entity) then
                    break
                end
            end
            if not entity or not entity_has_path(player, entity) then
                speak("No pathable entities.", true)
                entity = nil
            end
        else
            entity = move_selection(-1, entities)
        end
        reset_route_cache()
        if entity then
            speak(format_entity_callout(player, entity, #entities), true)
        end
    elseif keyboard_pressed(keys, CONFIG.next_entity_hotkey) then
        local entity = nil
        if state.pathfinding_filter_enabled and entities and #entities > 0 then
            for _ = 1, #entities do
                entity = move_selection(1, entities)
                if entity_has_path(player, entity) then
                    break
                end
            end
            if not entity or not entity_has_path(player, entity) then
                speak("No pathable entities.", true)
                entity = nil
            end
        else
            entity = move_selection(1, entities)
        end
        reset_route_cache()
        if entity then
            speak(format_entity_callout(player, entity, #entities), true)
        end
    elseif keyboard_pressed(keys, CONFIG.repeat_entity_hotkey) then
        speak(format_entity_callout(player, selected_entity(entities), #entities), true)
    elseif keyboard_pressed(keys, CONFIG.map_name_hotkey) then
        if player.map_id == state.active_supported_map_id then
            speak(map_name_from_id(player.map_id), true)
        else
            speak("Not in a learned map.", true)
        end
    elseif keyboard_pressed(keys, CONFIG.navigate_entity_hotkey) then
        speak(route_steps_to_target(player, selected_entity(entities)), true)
    end

    if shift_held(keys) then
        for key_name, direction in pairs(TELEPORT_DIRECTIONS) do
            if keyboard_pressed(keys, key_name) then
                speak(teleport_player_to(player, selected_entity(entities), direction), true)
                break
            end
        end
    end

    state.last_keyboard = keys
    state.previous_player = {
        map_id = player.map_id,
        map_type = player.map_type,
        x = player.x,
        y = player.y,
        facing = player.facing,
        magic_keys = player.magic_keys,
    }
end

local function speak_current_map_name()
    local player = read_player_state()
    if player.map_id == state.active_supported_map_id then
        speak(map_name_from_id(player.map_id), true)
    else
        speak("Not in a learned map.", true)
    end
end

return {
    run_frame = run_frame,
    speak_current_map_name = speak_current_map_name,
}
