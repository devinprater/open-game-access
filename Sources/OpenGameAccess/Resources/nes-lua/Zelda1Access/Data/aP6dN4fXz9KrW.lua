-- Dungeons.lua
-- Pure data module for Zelda 1 dungeons.
-- Owns: dungeon names and on-demand room/location label formatting.
-- Navigation.lua owns all announcements, hotkeys, and timing.
-- This module never calls ctx.write_speech directly.
--
-- Enemy names are owned by GameData.lua (the unified ENEMY_TYPE_NAMES table).
-- This module no longer carries its own copy.

local DUNGEON_NAMES = {
    [1]="Eagle",   [2]="Moon",   [3]="Manji",
    [4]="Snake",   [5]="Lizard", [6]="Dragon",
    [7]="Demon",   [8]="Lion",   [9]="Death Mountain",
}

local M = {}

-- Returns the name of the dungeon (e.g. "Eagle") or a fallback string.
function M.get_dungeon_name(level)
    return DUNGEON_NAMES[level] or string.format("Dungeon %d", level)
end

-- Decompose a dungeon room_id byte into (col, row), both 0-indexed.
-- The byte's low nibble is the column and the high nibble is the row,
-- so the dungeon grid is at most 16 cols x 16 rows (the game uses 8x8
-- of that). Pure integer math, no allocations.
local function row_col(room_id)
    return room_id % 16, math.floor(room_id / 16)
end

-- Format a dungeon room coord according to the player's preferred
-- format. Keeps dungeon and overworld coordinate styles in sync at
-- the same time as letting the player split them into independent
-- settings (see accessibility_settings.dungeon_coordinate_format).
--
-- Both "numeric" and "spreadsheet" are 1-indexed for human-friendly
-- output even though the game stores everything 0-indexed internally.
--   numeric room 0x35    -> "6, 4"
--   spreadsheet room 0x35 -> "F, 4"
function M.format_room_coord(room_id, format)
    local c, r = row_col(room_id)
    if format == "spreadsheet" then
        return string.char(string.byte("A") + c) .. ", " .. tostring(r + 1)
    else
        return tostring(c + 1) .. ", " .. tostring(r + 1)
    end
end

-- Build a "Room X, Y." label for a dungeon room.
function M.room_label(room_id, format)
    return "Room " .. M.format_room_coord(room_id, format) .. "."
end

-- Build a full location label like "Dungeon 1, Eagle, Room F, 4."
-- Used by Insert in dungeons.
function M.location_label(level, room_id, format)
    return "Dungeon " .. tostring(level) .. ", " ..
           M.get_dungeon_name(level) .. ", " ..
           M.room_label(room_id, format)
end

-- ============================================================
-- PERSON_TEXTS removed -- runtime decoded from PRG-ROM by GameData
-- ============================================================
--
-- All NPC dialog text is decoded at runtime by
-- GameData.decode_person_text(selector). Zero hardcoding -- every
-- dialog in the game auto-resolves from the live $0415 selector.
--
-- The thin wrapper below is for callers (Navigation.lua) that work
-- through the Dungeons module API. They get the decoded dialog
-- without having to know about GameData's internals.

local game_data = nil  -- lazy-loaded on first call

local function ensure_game_data()
    if game_data then return game_data end
    local chunk = loadfile(
        "C:/Users/gadeu/OneDrive/Documents/Playground/Zelda/Build/Research/Data/bU2sQ7tE5gJhV.lua")
    if chunk then
        local ok, mod = pcall(chunk)
        if ok and type(mod) == "table" then game_data = mod end
    end
    return game_data
end

-- Lookup the dialog string for a live $0415 selector value.
-- Returns the decoded message or nil if the selector is 0 (no
-- dialog active) or unresolvable.
function M.lookup_dialog_by_selector(selector)
    if not selector or selector == 0 then return nil end
    local gd = ensure_game_data()
    if not gd or not gd.decode_person_text then return nil end
    return gd.decode_person_text(selector)
end

-- ============================================================
-- PERSON_TRANSACTIONS (selector-based purchases)
-- ============================================================
--
-- Some dungeon NPCs aren't just hint-givers -- they sell upgrades
-- or charge for services. The L5 bomb-upgrade Moblin is the
-- canonical example: dialog says "I bet you'd like to have more
-- bombs" and walking into him deducts 100 rupees and bumps
-- MaxBombs.
--
-- These NPCs are functionally shops, but they live inside
-- dungeons (no overworld loc_idx) so the cave-shop mechanism
-- doesn't reach them. We use the same PersonTextSelector that
-- identifies the dialog as the canonical NPC-kind ID, and a
-- transactions table maps that ID to a {label, cost, completion}
-- triple.
--
-- Navigation.lua reads this at entity-build time when in a
-- dungeon room with a Person NPC active. If a transaction is
-- defined for the live $0415 AND the completion check returns
-- false (i.e. not yet purchased), it emits a navigable
-- shop-style entity ("Bomb Upgrade, 100 Rupees"). Once
-- completion is true the entity disappears on rebuild.
--
-- completion_check is a function returning true if the
-- transaction has already been fulfilled for the current save.
-- For the bomb upgrade, that's MaxBombs > the starting value
-- of 8 (every paid upgrade adds 4, so the threshold is
-- inclusive: > 8 means at least one upgrade has been bought).

local PERSON_TRANSACTIONS = {
    [0x32] = {
        label = "Bomb Upgrade",
        cost  = 100,
        completion_check = function()
            local level   = mainmemory.read_u8(0x0010)
            local room_id = mainmemory.read_u8(0x00EB)
            local base    = (level <= 6) and 0x06FF or 0x077F
            local flag    = mainmemory.read_u8(base + room_id)
            return bit.band(flag, 0x10) ~= 0
        end,
    },
}

-- Look up the transaction for a live PersonTextSelector value.
-- Returns the transaction table or nil. The caller should also
-- invoke entry.completion_check() to decide whether to emit the
-- entity (suppressed once the transaction has been fulfilled).
function M.lookup_transaction_by_selector(selector)
    if not selector or selector == 0 then return nil end
    return PERSON_TRANSACTIONS[selector]
end

-- ============================================================
-- BOSS WATCHERS
-- ============================================================
--
-- Per-boss state edge detectors. Bosses have unique vulnerability
-- mechanics that need real-time announcement so a blind player can
-- tell when to attack:
--   Gohma: eye open/closed -- only vulnerable to arrows when
--          $046B+slot == 3 (per Z_04.asm Gohma_HandleWeaponCollision
--          line 8485-8487, confirmed eye state 3 = fully open;
--          0/1 = closed, 2 = half-open).
--   Digdogger: big/child -- only vulnerable when child (handled in
--              Navigation.lua currently; will move here in the
--              entity-module refactor).
--
-- Each watcher is keyed by enemy obj_type (0x33 Blue Gohma, 0x34
-- Red Gohma). The watcher's update(state, ctx) is called once per
-- frame and is responsible for:
--   - scanning enemy slots for its boss type
--   - reading the relevant per-slot state byte
--   - edge-detecting transitions
--   - calling ctx.write_speech for accessibility callouts
--
-- State storage: each watcher gets its own field on the shared
-- state table (state.gohma_eye_state etc.) and is responsible for
-- resetting it on room transition. Navigation.lua's room-entry
-- handler calls dungeons.reset_boss_watchers(state).

local ENEMY_SLOT_FIRST = 1
local ENEMY_SLOT_LAST  = 0x0B
local ADDR_OBJ_TYPE_BASE = 0x034F
local GOHMA_EYE_STATE_BASE = 0x046B

-- Returns true if the eye state value means "fully open and
-- vulnerable to arrows". Per the disassembly, only state 3
-- triggers the damage path; states 0, 1, 2 all parry the arrow.
local function gohma_eye_is_open(s)
    return s == 3
end

local BOSS_WATCHERS = {
    -- Each watcher declares the obj_type IDs it cares about. The
    -- dispatcher does ONE slot scan up front and only invokes a
    -- watcher when its boss type is actually present. Zero per-frame
    -- work in non-boss rooms.
    --
    -- Gleeok was implemented and removed -- multi-part boss state
    -- couldn't be tracked without enough overhead to lag the
    -- emulator during the fight, which defeats the purpose. If we
    -- revisit Gleeok later it'll need a fundamentally different
    -- approach (event-driven only, no per-frame scan) since key
    -- elements like Gohma's eye state are higher value and the
    -- minute byte cost of multi-part-boss tracking has bigger
    -- consequences in tight combat.
    gohma = {
        types = { [0x33] = true, [0x34] = true },
        update = function(state, ctx, slot)
            local cur = mainmemory.read_u8(GOHMA_EYE_STATE_BASE + slot)
            local prev = state.gohma_eye_state
            if prev == nil then
                state.gohma_eye_state = cur
                return
            end
            local was_open = gohma_eye_is_open(prev)
            local is_open  = gohma_eye_is_open(cur)
            if is_open and not was_open then
                ctx.write_speech("Eye open.")
            elseif was_open and not is_open then
                ctx.write_speech("Eye closed.")
            end
            state.gohma_eye_state = cur
        end,
        reset = function(state)
            state.gohma_eye_state = nil
        end,
    },
}

-- Build a flat lookup: obj_type byte -> {watcher_key, watcher}.
-- Computed once at module load so the per-frame dispatch is a
-- single table lookup per slot, not nested pairs().
local BOSS_TYPE_LOOKUP = {}
for key, w in pairs(BOSS_WATCHERS) do
    if w.types then
        for type_id, _ in pairs(w.types) do
            BOSS_TYPE_LOOKUP[type_id] = { key = key, watcher = w }
        end
    end
end

-- Run boss watchers only when their boss is actually present.
--
-- One linear scan of the 11 enemy slots per frame. As soon as we hit
-- a slot whose obj_type is in BOSS_TYPE_LOOKUP, we invoke that watcher
-- and stop. No boss in any slot means we do 11 byte reads and bail --
-- cheaper than the previous design that ran every watcher's own slot
-- scan even when no boss was present.
--
-- If a watcher's boss vanishes (e.g. between rebuilds, or boss died),
-- its prev state stays set until the next reset_boss_watchers call
-- on room transition. Each watcher's reset() handles that cleanup.
function M.update_boss_watchers(state, ctx)
    for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
        local t = mainmemory.read_u8(ADDR_OBJ_TYPE_BASE + slot)
        local hit = BOSS_TYPE_LOOKUP[t]
        if hit then
            hit.watcher.update(state, ctx, slot)
            return  -- only one boss per room; bail after first match
        end
    end
end

-- Reset all boss watcher state. Called on room transition so
-- previous-room state can't leak into a new room.
function M.reset_boss_watchers(state)
    for _, w in pairs(BOSS_WATCHERS) do
        if w.reset then w.reset(state) end
    end
end

-- ============================================================
-- BASEMENTS (CELLARS)
-- ============================================================
--
-- Two basement layouts exist in Z1, both running in GameMode 0x09
-- (side-scroll cellar):
--
--   TREASURE BASEMENT: one stair on the upper-left, one item on
--     the floor (Bow, Red Ring, Raft, etc.). Walking up the stair
--     returns Link to the dungeon room he came from.
--
--   PASSAGE BASEMENT: two stairs (upper-left and upper-right).
--     Walking up either takes Link to a different dungeon room
--     above. No floor item -- pure transit room.
--
-- Discriminator: live tile probe at (24, 2). Passage basements
-- render the right staircase as 0x6F at columns 24-25, rows 2-17
-- (mirror of the left staircase at columns 6-7). Treasure
-- basements have plain wall/floor bytes there. The discriminator
-- is the live rendered geometry, not ROM metadata -- if the stair
-- is drawn, the player can use it.
--
-- All four basement-aware code paths (entry announcement, Insert
-- hotkey, entity emit, routing) call into this module so they
-- stay in sync by construction.

local RAM_PLAY_AREA_TILES = 0x6530
local GRID_HEIGHT          = 22
local STAIR_COL_TILE       = 0x6F
local RIGHT_PROBE_COL      = 24
local RIGHT_PROBE_ROW      = 2

-- True if the current basement is a passage (two stairs), false
-- if it's a treasure basement (one stair). Reads the live tile
-- at (24, 2) on the System Bus.
function M.is_passage_basement()
    local offset = RIGHT_PROBE_COL * GRID_HEIGHT + RIGHT_PROBE_ROW
    local ok, tile = pcall(function()
        return memory.read_u8(RAM_PLAY_AREA_TILES + offset, "System Bus")
    end)
    return ok and tile == STAIR_COL_TILE
end

-- Speech for the room-entry announcement. Caller (Navigation.lua's
-- on_room_entry) feeds this through ctx.write_speech directly.
function M.basement_announcement()
    if M.is_passage_basement() then
        return "Passage."
    end
    return "Basement."
end

-- Speech for the Insert hotkey announcement. Slightly more verbose
-- than the entry announcement so the player gets a full phrase
-- even when re-querying the location mid-room.
function M.basement_location_label()
    if M.is_passage_basement() then
        return "Dungeon passage."
    end
    return "Dungeon basement."
end

-- Build the basement-specific entity list. Returns an array of
-- entity tables that the caller appends to its own entity list.
-- Treasure basements get one stair entity; passage basements get
-- two with disambiguating labels. The room-item slot scan in
-- Navigation.lua handles the floor item for treasure basements.
function M.basement_entities()
    local entities = {}
    if M.is_passage_basement() then
        entities[#entities+1] = {
            type_name      = "exit",
            label          = "Right Passage Up",
            x              = 24,
            y              = 1,
            approach_x     = 24,
            approach_y     = 1,
            exit_direction = "North",
        }
        entities[#entities+1] = {
            type_name      = "exit",
            label          = "Left Passage Up",
            x              = 6,
            y              = 1,
            approach_x     = 6,
            approach_y     = 1,
            exit_direction = "North",
        }
    else
        entities[#entities+1] = {
            type_name      = "exit",
            label          = "Stairs up",
            x              = 6,
            y              = 1,
            approach_x     = 6,
            approach_y     = 1,
            exit_direction = "North",
        }
    end
    return entities
end

-- Routing override for basement entities. Returns a route string
-- for passage stair entities (BFS gets the side-scroll geometry
-- wrong because stair columns block horizontal travel through
-- rows 2-17 but the live grid reports those tiles as walkable).
-- Returns nil for non-passage-stair entities so the caller falls
-- through to standard BFS routing -- treasure basements behave
-- like overworld and BFS handles them correctly.
function M.basement_route(link_x, link_y, entity)
    if not M.is_passage_basement() then return nil end
    local label = entity and entity.label or ""
    if label ~= "Left Passage Up" and label ~= "Right Passage Up" then
        return nil
    end
    local FLOOR_Y = 17
    local tx = entity.approach_x or entity.x
    if link_x == tx then
        if link_y <= 1 then return "Already there." end
        return string.format("North %d.", link_y - 1)
    end
    local parts = {}
    if link_y < FLOOR_Y then
        parts[#parts+1] = string.format("South %d", FLOOR_Y - link_y)
    end
    local dx = tx - link_x
    if dx < 0 then
        parts[#parts+1] = string.format("West %d", -dx)
    elseif dx > 0 then
        parts[#parts+1] = string.format("East %d", dx)
    end
    parts[#parts+1] = string.format("North %d", FLOOR_Y - 1)
    return table.concat(parts, ", ") .. "."
end

return M
