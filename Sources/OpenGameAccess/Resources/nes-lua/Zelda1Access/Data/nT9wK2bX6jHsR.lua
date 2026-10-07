local M = {}

-- ============================================================
-- NAVIGATION.LUA
-- Navigation engine for Zelda 1 accessibility mod.
-- Handles: BFS pathfinding, entity management, room transition
-- detection, radar, footsteps, bumps, hotkeys.
-- All map data lives in WorldMap.lua (overworld) or dungeon modules.
-- ============================================================

local SCRIPT_PATH = debug.getinfo(1, "S").source:sub(2):gsub("\\", "/")
local DATA_DIR    = SCRIPT_PATH:match("^(.*)/[^/]+$") or "."

local LOG_FILE       = DATA_DIR .. "/navigation_startup.log"
local WORLD_MAP_FILE = DATA_DIR .. "/oR2tJ9xN5qLcE.lua"
local CAVE_DATA_FILE = DATA_DIR .. "/vH3jL8wT5cYqM.lua"
local DUNGEONS_FILE  = DATA_DIR .. "/aP6dN4fXz9KrW.lua"
local GAME_DATA_FILE = DATA_DIR .. "/bU2sQ7tE5gJhV.lua"
local WAYPOINTS_FILE = DATA_DIR .. "/iY8nP3kS6dWfB.lua"
local VISITED_ROOMS_FILE = DATA_DIR .. "/fG5hL7qC4vMzA.lua"

-- WorldMap loaded at startup -- owns all overworld data
local world_map  = nil
local cave_data  = {}
local dungeons = nil  -- owns dungeon names, room labels, AND old-man dialogs (lookup_dialog)
local game_data = nil
local waypoints = nil  -- loaded at startup; player-placed path markers
local visited_rooms = nil  -- persistent dungeon room tracking

-- Local references to the unified name tables in GameData.
-- Captured at startup so entity labeling does direct local table lookups
-- with zero module-indirection cost on the hot path.
local ENEMY_NAMES     = {}
local ITEM_NAMES      = {}
local DOOR_TILE_TYPES = {}

local CONFIG = {
    map_name_hotkey              = "Insert",
    coordinate_probe_hotkeys     = { "M" },
    repeat_entity_hotkey         = "Home",
    previous_entity_hotkey       = "PageUp",
    next_entity_hotkey           = "PageDown",
    navigate_entity_hotkey       = "End",
    waypoint_navigate_hotkeys    = { "OemPeriod", "Period", "." },
    waypoint_prev_hotkeys        = { "Oemcomma", "Comma", "," },
    waypoint_next_hotkeys        = { "OemQuestion", "Slash", "/" },
    waypoint_confirm_window_frames = 300,  -- 5 seconds at 60fps
    show_debug_overlay           = false,
    room_transition_grace_frames = 20,
    bump_confirm_frames          = 6,
    bump_cooldown_frames         = 12,
    bump_repeat_frames           = 24,
    synth_step_interval_frames   = 8,
    enemy_radar_interval_frames  = 30,
    item_radar_interval_frames   = 30,
    slot_check_interval_frames   = 6,
}

local ADDR = {
    cur_level         = 0x0010,
    game_mode         = 0x0012,
    room_id           = 0x00EB,
    room_item_id      = 0x00AB,
    obj_x_base        = 0x0070,
    obj_y_base        = 0x0084,
    obj_state_base    = 0x00AC,
    obj_type_base     = 0x034F,
    obj_pos_frac_base = 0x03A8,
    cur_save_slot     = 0x0016,
    quest_numbers     = 0x062D,
    sword_level       = 0x0657,
    keys_count        = 0x066E,
    rupees_to_add     = 0x067D,
    rupees_to_sub     = 0x067E,
    block_push_complete = 0x04CF,  -- 0=unpushed, 1=pushed, 2=triggered
    -- Compass / Map ownership flags. These two bytes are bitmasks
    -- indexed by dungeon level: bit (1 << (level - 1)) for L1-L8.
    -- Level 9 has its own dedicated bytes that store 1 (owned) or 0.
    inv_compass       = 0x0667,
    inv_map           = 0x0668,
    inv_compass9      = 0x0669,
    inv_map9          = 0x066A,
    -- Per-room dungeon screen state byte. The game stores one byte
    -- per room indexed by room_id, in two ranges:
    --   $06FF + room_id  for L1-L6 (verified empirically on this ROM)
    --   $077F + room_id  for L7-L9
    -- Byte format from disassembly inspection (Z_07.asm):
    --   bit 0x80 = enemies dealt with / room cleared
    --   bit 0x40 = (semantics unclear -- wiki labels this "mapped")
    --   bit 0x20 = visit state -- set by MarkRoomVisited
    --   bits 0-3 = locks/walls opened (W/E/S/N)
    -- We use bit 0x20 for visited because that's what the actual
    -- game code sets.
    dungeon_screen_state_lo = 0x06FF,  -- base for L1-L6
    dungeon_screen_state_hi = 0x077F,  -- base for L7-L9
    -- Boss room ID for the current dungeon. Stored as part of the
    -- per-level info block at $6BBC (LevelInfo_BossRoomId in the
    -- disassembly's Variables.inc). Single byte; the room_id of
    -- the boss room.
    --
    -- VERIFIED EMPIRICALLY: in Dungeon 1, $6BBC = 0x35 which is
    -- where the user found Aquamentus.
    --
    -- Note: there's also LevelInfo_TriforceRoomId at $6BAE which
    -- in dungeon 1 is 0x36 -- adjacent room. The compass announce-
    -- ment uses $6BBC (boss) because that's the more useful
    -- target; the triforce drops in the boss room as part of the
    -- defeat sequence anyway.
    level_boss_room   = 0x6BBC,
}

local GRID = {
    width               = 32,
    height              = 22,
    tile_size           = 8,
    status_bar_height   = 0x40,
    collision_hotspot_y = 0x0B,
    nav_row_min         = 0,
    nav_row_max         = 21,
}

local RAM = {
    play_area_tiles     = 0x6530,
    collision_threshold = 0x034A,
    domain              = "System Bus",
}

local ROM = {
    domain = "PRG ROM",  -- kept for rom_read used by shop inventory load
}

local ENEMY_SLOT_FIRST       = 1
local ENEMY_SLOT_LAST        = 0x0B
local ITEM_SLOT_FIRST        = 1
local ITEM_SLOT_LAST         = 0x13
local ROOM_ITEM_SLOT         = 0x13
local MONSTER_PROJECTILE_MIN = 0x53
local FIRE_TYPE_MIN          = 0x3F
local FIRE_TYPE_MAX          = 0x40
local CAVE_NPC_MIN           = 0x6A
local CAVE_NPC_MAX           = 0x7F
local DUNGEON_NPC_MIN        = 0x4B
local DUNGEON_NPC_MAX        = 0x52
local DROPPED_ITEM_TYPE      = 0x60
local ROOM_ITEM_NONE         = 0x3F
local PUSH_BLOCK_OBJ_TYPE    = 0x68  -- tile object that lives in slot 0x0B
local PUSH_BLOCK_TILE        = 0xB0  -- tile byte at the block's grid position
local STAIR_TILE             = 0x70  -- top-left of the 2x2 stair cluster.
                                     -- The full cluster is 0x70 / 0x72 (top row)
                                     -- and 0x71 / 0x73 (bottom row). We scan for
                                     -- the top-left byte and use it as the
                                     -- stair entity position. All four bytes
                                     -- are walkable (< collision threshold) so
                                     -- Link walks onto them to trigger the
                                     -- side-scroll basement transition.

-- ROM offsets for the per-room LevelBlockAttrs table. Six bytes per room
-- indexed by raw room_id, each byte encoding several flags. We only need
-- one of them currently: LevelBlockAttrsD bit 0x40 = room contains a push
-- block (currently unused but kept for future).
local ROM_LEVEL_BLOCK_ATTRS_D = 0x69FE

-- Cave-specific constants
local SWORD_CAVE_LOCS       = {[16]=true, [18]=true, [19]=true}
local SHOP_LOCS             = {[26]=true,[29]=true,[30]=true,[31]=true,[32]=true}
-- Secret Rupee Cave variants on this ROM, per cave_data.lua and
-- in-game verification: loc 33, 34, and 35 are all Secret Rupee
-- Caves (5/30/100 rupees). cave_data.lua is the single source of
-- truth for cave types -- WorldMap.lua used to have a generic Z1
-- defaults table here that mislabeled these locs ("cave_white_sword"
-- / "cave_magical_sword") but it didn't match this ROM and has been
-- removed.
--
-- The gating gates entity emission on NPC presence -- the moblin
-- sits in the room, walks into him triggers payment, and the rupee
-- on the floor disappears. Combined with cave_reward_collected()
-- (the persistent $067F bit 0x10 flag), this correctly suppresses
-- the rupee on cleared caves regardless of which loc index the
-- specific Secret Rupee Cave uses.
local SECRET_RUPEE_CAVE_LOCS = {[33]=true, [34]=true, [35]=true}  -- Moblin + Rupee on floor

-- Overworld screens where a hidden cave stair can be revealed by some
-- mechanic (bomb wall, burn tree, armos touch, gravestone push,
-- recorder). When the stair tile (0x70) appears in the live tile grid
-- on one of these screens, it means the player has already triggered
-- the reveal and the baked "Event" marker should be replaced by a real
-- "Stairs" entity at the actual stair position. Self-consistent:
--   stair tile present  -> stair entity, no event entity
--   stair tile absent   -> event entity, no stair entity
-- Keyed by raw room_id. Add screens here as they're verified.
local OVERWORLD_REVEAL_ROOMS = {
    [0x01] = true,  -- B1 bomb (Door Repair Cave)
    [0x03] = true,  -- D1 bomb (Door Repair Cave)
    [0x05] = true,  -- F1 bomb (Dungeon 9)
    [0x07] = true,  -- H1 bomb (Door Repair Cave)
    [0x0B] = true,  -- L1 armos (Dungeon 5 alt entrance)
    [0x0D] = true,  -- N1 bomb (Potion Shop)
    [0x10] = true,  -- A2 bomb (Gambling Cave)
    [0x12] = true,  -- C2 bomb (Bait Shop)
    [0x13] = true,  -- D2 bomb (Secret Rupee Cave)
    [0x14] = true,  -- E2 bomb (Door Repair Cave)
    [0x16] = true,  -- G2 bomb (Gambling Cave)
    [0x1C] = true,  -- M2 armos (Hint Cave)
    [0x1D] = true,  -- N2 Any Road Cave (triggered)
    [0x21] = true,  -- B3 gravestone push (Magical Sword Cave)
    [0x22] = true,  -- C3 armos alt (Dungeon 6)
    [0x23] = true,  -- D3 push rock (Take Any Road cave)
    [0x26] = true,  -- G3 bomb wall (Bait Shop)
    [0x27] = true,  -- H3 bomb wall (Potion Shop)
    [0x28] = true,  -- I3 burn tree (Secret Rupee Cave)
    [0x2C] = true,  -- M3 bomb wall (Heart or Potion Cave)
    [0x2D] = true,  -- N3 bomb wall (Secret Rupee Cave)
    [0x33] = true,  -- D4 bomb wall (Potion Shop)
    [0x34] = true,  -- E4 armos touch (Blue Ring Shop)
    [0x3D] = true,  -- N4 armos touch (Secret Rupee Cave)
    [0x42] = true,  -- C5 lake drain (recorder reveals stair to south path)
    [0x46] = true,  -- G5 burn (Cheap Shield Shop)
    [0x47] = true,  -- H5 burn (Heart or Potion Cave)
    [0x48] = true,  -- I5 burn (30 Secret Rupee Cave)
    [0x49] = true,  -- J5 push rock (Take Any Road cave)
    [0x4B] = true,  -- L5 burn (Potion Shop)
    [0x4D] = true,  -- N5 burn (Cheap Shield Shop)
    [0x4E] = true,  -- O5 armos push (10 Secret Rupee Cave)
    [0x51] = true,  -- B6 burn (10 Secret Rupee Cave)
    [0x56] = true,  -- G6 burn (10 Secret Rupee Cave)
    [0x5B] = true,  -- L6 burn (10 Secret Rupee Cave)
    [0x62] = true,  -- C7 burn (100 Secret Rupee Cave)
    [0x63] = true,  -- D7 burn (Door Repair Cave)
    [0x67] = true,  -- H7 bomb (30 Secret Rupee Cave)
    [0x68] = true,  -- I7 burn (Door Repair Cave)
    [0x6A] = true,  -- K7 burn (Door Repair Cave)
    [0x6B] = true,  -- L7 burn (100 Secret Rupee Cave)
    [0x6D] = true,  -- N7 burn (Dungeon 8)
    [0x71] = true,  -- B8 bomb (Secret Rupee Cave)
    [0x76] = true,  -- G8 bomb (Money Making Cave)
    [0x78] = true,  -- I8 burn tree
    [0x79] = true,  -- J8 push rock (warp save / Power Bracelet)
    [0x7B] = true,  -- L8 bomb (Heart or Potion Cave)
    [0x7C] = true,  -- M8 bomb (Gambling Cave)
    [0x7D] = true,  -- N8 bomb (Door Repair Cave)
}

-- Tile bytes that mark cave entrances in the overworld tile grid.
-- Two distinct mechanisms render different tiles:
--
-- REVEALED STAIR (0x70): Bare staircase exposed by a reveal mechanic
-- like the lake drain on E3. Same cluster as a dungeon stair:
-- 0x70/0x72 top, 0x71/0x73 bottom. Confirmed via tile_diff_probe
-- on E3 lake drain. The 0x70 tile itself is where Link stands to
-- descend.
--
-- CAVE ENTRANCE (0x24): The walkable bottom row of a cave-mouth
-- cluster, used for both visible cave entrances and revealed cave
-- entrances (bomb walls, burn trees, armos touches that produce a
-- cave mouth instead of bare stairs). Two confirmed forms:
--   * Visible mouth: 0xF3/0xF3 top row, 0x24/0x24 bottom row.
--     Confirmed on G7 (Blue Candle Shop) at (14, 2-3).
--   * Revealed mouth: 0x24 across all four tiles. Confirmed on
--     G8 (30 Secret Rupee Cave) bomb-wall reveal at (14-15, 2-3).
-- Either way, the 0x24 cluster is consistently present and
-- consistently walkable, so we scan for 0x24 as the canonical
-- cave-mouth marker. Link enters by walking onto a 0x24 tile.
local OVERWORLD_STAIR_TILE  = 0x70  -- revealed stair (top-left)
local OVERWORLD_CAVE_MOUTH  = 0x24  -- cave entrance (any tile in cluster)

-- Room-specific loc_idx overrides (rooms that share a loc_idx with a different cave type)
-- These only override label/dialog -- entity building uses SECRET_RUPEE_CAVE_LOCS for the formula
local CAVE_ROOM_OVERRIDES = {
    [0x4E] = { label="Secret Rupee Cave", dialog="It's a secret to everybody.",                   cave_type="rupee" },
    [0x0D] = { label="Potion Shop",        dialog="Buy medicine before you go.",                  cave_type="potion_shop", shop_loc=26, requires_letter=true },
}

-- ============================================================
-- CAVE ENTITY BUILDING RULES (READ THIS BEFORE TOUCHING)
-- ============================================================
-- Items are ALWAYS on the floor in every cave type.
-- The NPC/Moblin does NOT give items directly - the item spawns
-- on the floor and Link walks over it to collect it.
--
-- Cave types and their entity behavior:
--   loc 16: Wood Sword Cave   - NPC present + sword on floor (if not yet obtained)
--   loc 17: Heart/Potion Cave - NPC present + heart container + blue potion on floor
--   loc 31: Secret Cave       - Moblin present + Rupee on floor
--   loc 33: Secret Rupee Cave - Moblin present + Rupee on floor
--   loc 34: White Sword Cave  - NPC present + sword on floor (if not yet obtained)
--   loc 35: Secret Rupee Cave - Moblin present + Rupee on floor
--   loc 22: Gambling Cave     - NPC present, outcome varies
--   loc 23-30: Shops          - Items on floor with prices
--   CAVE_ROOM_OVERRIDES: room-specific exceptions (e.g. 0x4E shares loc_idx with wrong type)
-- ============================================================

-- CAVE_INFO removed: cave_data.lua is the single source of truth for all cave labels/dialogs.

local SHOP_ROM = {
    [26]={items_addr=0x1861E, prices_addr=0x1865A},
    [29]={items_addr=0x18627, prices_addr=0x18663},
    [30]={items_addr=0x1862A, prices_addr=0x18666},
    [31]={items_addr=0x1862D, prices_addr=0x18669},
    [32]={items_addr=0x18630, prices_addr=0x1866C},
}

-- Item names live in GameData.lua (single ITEM_NAMES table, captured above
-- as a local reference). The three call sites that used to have their own
-- copies (shop loader, dropped-item scan, room item) all share that table.

-- Per-call dungeon coordinate helpers.
-- Dungeons.lua exposes format_room_coord / room_label / location_label
-- which take the player's coordinate-format preference and return a
-- string formatted that way. We call these on demand at room transition
-- and hotkey-press time -- they're cheap (one string.format call) so
-- we're no longer baking 2,560 strings at startup.

-- Returns the active dungeon coordinate format string from settings.
-- Defaults to "numeric" if settings haven't been wired up yet.
local function dungeon_fmt(ctx)
    return (ctx and ctx.settings and ctx.settings.dungeon_coordinate_format)
        or "numeric"
end

-- Fixed door positions inside dungeon rooms.
-- Each dungeon room has up to four doors at the centers of the cardinal walls.
--
-- The door FACE (the visible graphic -- locked keyhole, hole from a bombed
-- wall, plain wall, etc.) sits at the wall-edge tile (y=2 for north, y=19
-- for south, x=2 for west, x=28 for east). The first WALKABLE tile inside
-- the doorway -- where Link actually stands when he's "in the door" -- is
-- one tile inward from the face: y=3 for north, y=18 for south, etc.
--
-- BFS routes to approach_x/approach_y, so we point that at the walkable
-- interior tile. From there, Link's own forward motion through the door
-- triggers the room scroll, just like overworld border exits work.
--
-- The pass-one walkable scan checks the approach tile (the inside-the-door
-- tile). If the live tile there is walkable, the door is open. This
-- correctly handles bombed-open holes too: the hole graphic at the face
-- tile is solid, but the tile inside the doorway becomes walkable once
-- the wall is breached. Closed/locked/cracked/shutter doors leave the
-- approach tile solid and don't get an entity in pass one. Per-door-state
-- labeling (locked vs shutter vs cracked) is a separate scan that reads
-- the door FACE tile to recognize unique signatures (e.g. the keyhole
-- 0x98 for north locked doors).
local DUNGEON_DOORS = {
    { x = 15, y =  3, label = "North door", direction = "North", face_x = 15, face_y =  2 },
    { x = 15, y = 18, label = "South door", direction = "South", face_x = 15, face_y = 19 },
    { x =  3, y = 11, label = "West door",  direction = "West",  face_x =  2, face_y = 11 },
    { x = 28, y = 11, label = "East door",  direction = "East",  face_x = 29, face_y = 11 },
}

-- ============================================================
-- WORLD MAP WRAPPERS
-- ============================================================

-- ============================================================
-- ENGINE STATE
-- ============================================================

local SHOP_INVENTORY    = {}

local state = {
    started         = false,
    frame_counter   = 0,
    room_data       = { rooms={} },

    cur_level       = -1,
    cur_room_id     = -1,
    cur_in_cave     = false,
    cur_quest       = -1,
    cur_game_mode   = -1,
    cur_room_key    = nil,
    cur_room_entry  = nil,

    live_grid_cache       = nil,
    live_grid_room_key    = nil,
    live_grid_build_frame = -1,

    entities     = {},
    entity_count = 0,

    enemy_slots              = {},
    enemy_type_baseline      = {},
    item_slots               = {},
    item_lifetime_baseline   = {},
    room_item_state_baseline = -1,
    cave_npc_slot_baseline    = -1,  -- slot number where NPC was found, for disappearance poll

    -- Door face tile baseline. Captured at end of build_entity_list when in
    -- a dungeon. Indexed by direction; value is the live tile byte at the
    -- door's face position. The polling loop compares current face tiles
    -- against this baseline -- any change (key opens a locked door, bomb
    -- opens a cracked wall, shutter opens after enemies cleared) triggers
    -- a live-grid invalidation and entity rebuild.
    door_tile_baseline = nil,

    -- Keys-count edge detection. The game decrements 0x066E when Link
    -- uses a key on a locked door. Watching for that drop is a clean,
    -- discrete trigger that we can use to force a door rebuild without
    -- relying on tile-comparison polling. Initialised lazily on first
    -- dungeon poll so we don't false-trigger on entry.
    prev_keys_count = -1,

    dungeon_rupee_announced  = false,
    dungeon_last_rupees_to_add = 0,
    dungeon_last_rupees_to_sub = 0,

    last_check_frame      = -9999,
    check_interval        = 6,
    deferred_rebuild_frame= -1,

    selected_entity_index = 1,
    selected_room_key     = nil,
    locked_enemy_slot     = nil,

    link_x       = 0,
    link_y       = 0,
    link_tile_x  = 0,
    link_tile_y  = 0,
    prev_link_x  = 0,
    prev_link_y  = 0,

    last_step_frame                   = -9999,
    step_variant                      = 0,
    last_bump_frame                   = -9999,
    -- Stuck-frame counter for bump detection. Increments each frame
    -- Link is stationary while a direction is held; resets on movement
    -- or direction release. Crossing CONFIG.bump_confirm_frames fires
    -- the initial bump. See update_bumps() for the full state machine.
    stuck_frames                      = 0,
    -- Direction Link last bumped against. Used by the repeat-pulse
    -- path so a continuously-held direction against a wall pulses
    -- every CONFIG.bump_repeat_frames instead of going silent after
    -- the first bump. Cleared when Link actually moves; deliberately
    -- preserved through brief input-register flickers so re-presses
    -- against the same wall keep pulsing.
    bump_lock_direction               = nil,
    footstep_resume_block_until_frame = -1,

    last_enemy_radar_frame = -9999,
    last_item_radar_frame  = -9999,

    room_transition_until_frame = -1,
    last_room_id_check_frame = -999,
    transition_announced_room = nil,

    committed_in_cave       = false,
    in_cave_candidate       = false,
    in_cave_candidate_since = -999,
    in_cave_debounce_frames = 15,

    speech_queue          = {},
    speech_queue_next_frame = -1,

    -- Last dialog spoken in the current location. Set by the cave and
    -- dungeon dialog systems when they fire an Old Man / NPC line.
    -- The N hotkey reads this back so the player can re-hear the
    -- dialog without leaving the room. Cleared on every room
    -- transition so leaving and returning doesn't replay a stale
    -- line; refreshed naturally if the location speaks a new dialog.
    last_dialog_spoken           = nil,

    -- Cave state
    cave_last_in_cave        = false,
    cave_cached_label        = nil,
    cave_cached_dialog       = nil,
    cave_cached_loc_idx      = 0,
    cave_cached_quest        = 0,
    cave_dialog_frame        = -1,
    cave_sword_baseline      = -1,
    cave_shop_check_frame    = 0,   -- 0 so first shop check waits for frame_counter >= 0
    cave_rupee_announced     = false,
    cave_last_rupees_to_add  = 0,   -- previous frame value for edge detection
    cave_last_rupees_to_sub  = 0,
    cave_last_letter_state   = 0,   -- previous frame value of 0x0666 for edge detection
    cave_info_paid            = false,
    cave_rupee_collected       = false,
    cave_letter_collected      = false,

    cave_last_screen_flag_bit  = -1,
    cave_reward_pickup_handled = false,

    -- Dungeon state
    dungeon_entry_announced      = false,
    overworld_return_announced   = false,
    prev_cur_level               = nil,
    -- (cur_dungeon_room_label / cur_dungeon_location_label removed --
    -- room labels are now built on demand from dungeons.room_label() /
    -- dungeons.location_label() so the format setting is honored at
    -- speech time, not baked in at startup.)

    -- Dungeon dialog state. When entering a dungeon room with an Old Man /
    -- hint NPC present, we schedule a deferred speech of the room's hint
    -- (looked up in dungeon_dialogs.lua). Same 60-frame settle pattern as
    -- the cave dialog system. dungeon_dialog_pending holds the text to
    -- speak, dungeon_dialog_frame is the frame at which to fire it.
    dungeon_dialog_pending       = nil,
    dungeon_dialog_frame         = -1,

    -- PersonTextSelector ($0415) edge detection. The selector is 0
    -- when no dialog is active and becomes non-zero the moment a
    -- dungeon NPC's dialog triggers (e.g. when Link walks close to
    -- the bomb-upgrade Moblin). We watch every frame for the 0 ->
    -- non-zero transition; when it fires, we look up the matching
    -- string in PERSON_TEXTS and speak it.
    --
    -- This is the canonical signal for "NPC is now talking" --
    -- doesn't depend on entry timing or NPC slot scans, just the
    -- single byte the game itself uses to track which dialog is
    -- on screen. Reset on every room transition so re-entering a
    -- room and re-triggering the same dialog speaks again.
    prev_person_selector         = 0,

    -- PersonTextIndex ($0416) + PersonTextPtr ($045F/$0460) edge
    -- detector. The text index alone is unreliable -- it gets reused
    -- by other render tasks (sprite counts, scrolling) and was seen
    -- holding values like 0x80 / 0xA0 in non-dialog rooms. The pointer
    -- $045F is much more discriminating: it's the live read pointer
    -- into PRG-ROM that climbs as each character is rendered, and is
    -- 0 except when the dialog text reader is actively running. We AND
    -- both signals together as box_active = (idx > 0) AND (ptr > 0)
    -- and watch its 0 -> 1 edge to fire dialog speech. Reset on every
    -- room transition.
    prev_box_active              = false,

    -- Per-selector cooldown to prevent doubled announcements.
    -- The selector can briefly drop to 0 and back to the same
    -- non-zero value when Link bumps the NPC, walks past the
    -- trigger box, or wiggles in a corner -- each cycle would
    -- otherwise fire a fresh 0 -> non-zero edge and re-speak the
    -- same line.
    --
    -- Mechanism: when we speak a dialog, record the selector
    -- value AND the frame. Subsequent edges for the SAME
    -- selector value within 180 frames (~3 seconds) get
    -- silently dropped. After the cooldown elapses, the same
    -- selector can speak again -- so genuine re-triggering by
    -- walking away and back still works. 180 chosen so the room
    -- entry announcement ("New room. Room 5, 2.") has time to
    -- finish before a re-fire would overlap it.
    --
    -- Reset on room entry alongside prev_person_selector so the
    -- cooldown never carries between rooms.
    last_spoken_selector         = 0,
    last_spoken_selector_frame   = -9999,

    -- Digdogger boss state edge detection. Verified empirically
    -- (see Tools/dumps/digdogger_probe_*.log): in the L5 boss
    -- room, type 0x38 in any enemy slot with $046B+slot==0 is the
    -- BIG (invulnerable) form. After the recorder fires, the slot
    -- changes to type 0x18 with $046B+slot==1 -- the SMALL
    -- (vulnerable, attackable) form. If the player doesn't kill
    -- the child fast enough, the boss reforms back to type 0x38.
    --
    -- Watching the type+IsChild combination across frames lets us
    -- announce the transformation in real time: "Digdogger
    -- shrinks." on big->child, "Digdogger reforms." on child->big.
    -- Crucial accessibility info -- without it, a blind player
    -- can't tell whether the recorder worked or whether they need
    -- to play it again after a reform.
    --
    -- Values: nil (no Digdogger present), "big", or "child".
    -- Reset on every room transition so the announcement fires
    -- fresh on re-entry to a boss room.
    prev_digdogger_state         = nil,

    -- Push-block completion edge detection. BlockPushComplete at RAM 0x04CF
    -- starts at 0, advances to 1 the moment Link finishes pushing the block,
    -- and to 2 once the secret has triggered. We watch for the 0->1 edge
    -- per dungeon room and announce "Block pushed." plus the secret effect.
    -- Reset to -1 on every room entry so the edge detector doesn't fire
    -- spuriously when entering a room where the block was already pushed.
    prev_block_push_complete     = -1,
    -- Push-block initial position cache. Captured the first time we see
    -- the block in slot 0x0B after entering a room. Once the block's live
    -- position diverges from this baseline, we know it has been pushed
    -- and we suppress the entity so the player isn't told to walk to a
    -- thing they can no longer interact with. Reset on every room entry.
    push_block_initial_x         = nil,
    push_block_initial_y         = nil,

    -- Overworld reveal-handled flag. Set by build_entity_list whenever
    -- it emits a live cave/stairs entity from the 0x70/0x24 tile scan.
    -- The polling helper uses this to short-circuit instead of
    -- comparing positions, because some screens have baked Event
    -- entries at the same coordinates as the live tile (e.g. G8 baked
    -- at (14, 3) and live 0x24 cluster at (14, 2-3) -- position match
    -- can't distinguish stale baked from fresh live). Reset on every
    -- room entry.
    overworld_reveal_handled     = false,

    -- Basement (cellar) state. The dungeon basement is a side-scroll
    -- room reached by walking onto stairs in a normal dungeon room. It
    -- runs in GameMode 0x09 instead of 0x05, has its own RoomId range
    -- (typically 0x7F or similar), and contains the dungeon item (e.g.
    -- the Bow in L1). The CellarSourceRoomId at RAM 0x0527 stores the
    -- room we descended from so the game knows where to return us.
    cur_in_basement              = false,
    basement_announced           = false,

    -- Mod-side tracking of dungeon rooms moved to VisitedRooms.lua.
    -- That module persists to dungeon_visited.json keyed by
    -- save_slot + quest + level + room_id, so progress survives
    -- BizHawk restarts and different saves don't pollute each other.
    -- The transition speech also cross-checks the game's bit 0x20 so
    -- a save-state load that reverts the game's state correctly
    -- announces previously-visited rooms as new.
}

-- ============================================================
-- UTILITIES
-- ============================================================

local function write_log(msg)
    local f = io.open(LOG_FILE, "a")
    if f then f:write(tostring(msg or "") .. "\n"); f:close() end
end

local function reset_log()
    local f = io.open(LOG_FILE, "w"); if f then f:close() end
end

local function clamp(v, lo, hi)
    return v < lo and lo or v > hi and hi or v
end

local function abs(v) return v < 0 and -v or v end

local function pixel_to_tile(x, y)
    local tx = clamp(math.floor(x / GRID.tile_size), 0, GRID.width-1)
    local ty = clamp(
        math.floor(((y + GRID.collision_hotspot_y) - GRID.status_bar_height) / GRID.tile_size),
        GRID.nav_row_min, GRID.nav_row_max)
    return tx, ty
end

local function room_row_col(room_id)
    return math.floor(room_id / 16), room_id % 16
end

local function room_key_str(level, room_id)
    return string.format("%02X:%02X", level, room_id)
end

-- Format a screen coordinate using either numeric ("3, 8") or
-- spreadsheet ("H, 8") style. The numeric form lists column then row,
-- both 1-indexed. The spreadsheet form uses the column letter (A-P)
-- and row number (1-8). Both use a comma+space separator so screen
-- readers parse them consistently.
local function screen_coord_str(room_id, format)
    local r, c = room_row_col(room_id)
    if format == "spreadsheet" then
        return string.char(string.byte("A") + c) .. ", " .. tostring(r + 1)
    end
    return string.format("%d, %d", c + 1, r + 1)
end

-- Format a tile coordinate inside the current screen using the same
-- numeric/spreadsheet rule. Tile X is 0-31, tile Y is 0-21. For
-- spreadsheet mode we use a longer letter range -- A-Z for columns
-- 0-25, then AA-AF for 26-31 -- so every tile maps to a unique
-- letter prefix without colliding.
local function tile_coord_str(x, y, format)
    if format == "spreadsheet" then
        local letter
        if x < 26 then
            letter = string.char(string.byte("A") + x)
        else
            letter = "A" .. string.char(string.byte("A") + (x - 26))
        end
        return letter .. ", " .. tostring(y)
    end
    return string.format("%d, %d", x, y)
end

local function is_walkable(grid, x, y)
    local row = grid and grid[y+1]
    local tile = row and row[x+1]
    return tile and tile.passable == true
end

local function nearest_walkable(grid, x, y)
    local cands = {{x+1,y},{x-1,y},{x,y+1},{x,y-1}}
    for _, c in ipairs(cands) do
        if is_walkable(grid, c[1], c[2]) then return c[1], c[2] end
    end
    return nil, nil
end

local function rough_dir(fx, fy, tx, ty)
    local parts = {}
    if ty < fy then parts[#parts+1]="North" elseif ty > fy then parts[#parts+1]="South" end
    if tx < fx then parts[#parts+1]="West"  elseif tx > fx then parts[#parts+1]="East"  end
    return #parts == 0 and "Here" or table.concat(parts, " ")
end

-- Detect Shift + Period as a rising-edge press. The shared just_pressed_any
-- helpers in ctx all filter shift-held presses out of consideration so the
-- bare Period hotkey isn't double-handled. For Shift+. / Shift+, / Shift+/
-- / Shift+M (waypoint mutations) we need to detect the press while shift
-- is held, so we track our own per-frame state for these combos.
local nav_prev_period = false
local nav_prev_comma  = false
local nav_prev_slash  = false
local nav_prev_m      = false
local function shift_held(keys)
    return keys.LeftShift or keys.RightShift or keys.Shift
        or keys.ShiftKey or keys.LShiftKey or keys.RShiftKey or false
end
local function is_shift_period_pressed(ctx)
    local keys = ctx.keys or {}
    local now = keys.OemPeriod or keys.Period or keys["."] or false
    local pressed = now and not nav_prev_period and shift_held(keys)
    nav_prev_period = now
    return pressed
end
local function is_shift_comma_pressed(ctx)
    local keys = ctx.keys or {}
    local now = keys.Oemcomma or keys.Comma or keys[","] or false
    local pressed = now and not nav_prev_comma and shift_held(keys)
    nav_prev_comma = now
    return pressed
end
local function is_shift_slash_pressed(ctx)
    local keys = ctx.keys or {}
    local now = keys.OemQuestion or keys.Slash or keys["/"] or false
    local pressed = now and not nav_prev_slash and shift_held(keys)
    nav_prev_slash = now
    return pressed
end
local function is_shift_m_pressed(ctx)
    local keys = ctx.keys or {}
    local now = keys.M == true or keys.m == true
    local pressed = now and not nav_prev_m and shift_held(keys)
    nav_prev_m = now
    return pressed
end

local function same_tile(ax, ay, bx, by) return ax==bx and ay==by end

local function pan_bucket(pixel_x)
    return math.floor((clamp((pixel_x-120)/120,-1,1)*5)+0.5)*20
end

local function step_pitch(tile_y)
    return 3.0^(-((clamp(tile_y,0,GRID.height-1)-11)/11))
end

local function enemy_pitch(tile_y)
    return 2.0^(-((clamp(tile_y,0,GRID.height-1)-11)/18))
end

local function drop_item_name(id)
    return ITEM_NAMES[id] or string.format("Item %02X", id)
end

local function room_item_name(id)
    return ITEM_NAMES[id] or string.format("Item %02X", id)
end

-- ============================================================
-- ROM READ
-- ============================================================

local function rom_read(addr)
    local ok, v = pcall(function() return memory.read_u8(addr, ROM.domain) end)
    return ok and v or 0
end

-- ============================================================
-- CAVE HELPERS
-- ============================================================

local function lookup_cave(loc_idx, quest)
    local entry = cave_data[loc_idx]
    if not entry then return nil end
    return entry[quest] or entry[0] or nil
end

local function cave_npc_present()
    for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
        local t = mainmemory.read_u8(ADDR.obj_type_base + slot)
        if t >= CAVE_NPC_MIN and t <= CAVE_NPC_MAX then return true end
    end
    return false
end

-- Z1's persistent "this cave's reward has been collected" flag.
-- Per Data Crystal's canonical RAM map, the overworld screen state
-- byte at $067F + room_id has these bits:
--   bit 0x80 = S (secret discovered) -- fires on first reveal,
--              BEFORE the reward is collected. Too aggressive to
--              gate on; would hide rewards on first visit.
--   bit 0x40 = s (pushable rock stairs found)
--   bit 0x10 = I (item obtained) -- THIS IS THE ONE WE WANT.
--              Set when the player walks over the room item
--              (heart container, potion, rupee, letter, etc).
--              Persists across save state and game resets.
--   bits 0x07 = enemies killed counter
--
-- Cave room_id IS the overworld room_id we came from (Z1 doesn't
-- reassign room_id when entering a cave), so $067F + room_id reads
-- the right byte directly while in-cave.
--
-- Returns true if bit 0x10 of $067F + room_id is set, meaning the
-- cave's reward has already been collected.
local function cave_reward_collected(room_id)
    local flag = mainmemory.read_u8(0x067F + room_id)
    return bit.band(flag, 0x10) ~= 0
end

-- Returns true if any enemy slot currently holds a dungeon NPC type.
-- The base range 0x4B-0x52 covers Old Man / Old Woman / Friendly Moblin
-- (the NPC-class enemy types reserved for hint-givers). 0x36 is the
-- Friendly Goriya ("Grumble Grumble") which lives in the regular
-- enemy range but is also a dialog NPC -- gates the room until you
-- feed it bait. Add other talking-enemy types here as we encounter
-- them.
local DUNGEON_NPC_EXTRAS = {
    [0x36] = true,  -- Friendly Goriya (Grumble Grumble)
}

local function dungeon_npc_present()
    for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
        local t = mainmemory.read_u8(ADDR.obj_type_base + slot)
        if t >= DUNGEON_NPC_MIN and t <= DUNGEON_NPC_MAX then return true end
        if DUNGEON_NPC_EXTRAS[t] then return true end
    end
    return false
end

-- Look up the dungeon dialog for the current room. Reads live
-- $0415 (PersonTextSelector) and looks up the matching string in
-- the selector table. Quest-independent, dungeon-independent --
-- the same selector value always means the same line, anywhere
-- in the game.
--
-- Returns the dialog string or nil. nil means no entry mapped
-- yet for the active selector; Navigation.lua silently drops the
-- dialog in that case (the NPC presence is still announced via
-- the entity label). Unknown selectors get logged so we can
-- map them as the player encounters new NPCs.
local function lookup_dungeon_dialog(level, room_id)
    if not dungeons or not dungeons.lookup_dialog_by_selector then
        return nil
    end
    local selector = mainmemory.read_u8(0x0415)
    if selector == 0 then return nil end
    local hit = dungeons.lookup_dialog_by_selector(selector)
    if hit then return hit end
    -- Selector is set but not in our table -- log so we can map it.
    -- Single log line per (level, room_id, selector) combination so
    -- we don't spam.
    local key = string.format("%d:%02X:%02X", level, room_id, selector)
    state.unknown_selectors = state.unknown_selectors or {}
    if not state.unknown_selectors[key] then
        state.unknown_selectors[key] = true
        write_log(string.format(
            "UNKNOWN PERSON SELECTOR 0x%02X in level=%d room=0x%02X -- please check Data/PersonTexts.txt and report",
            selector, level, room_id))
    end
    return nil
end

-- ============================================================
-- DUNGEON INVENTORY / VISITED HELPERS
-- ============================================================
-- These read live RAM each call -- cheap (single byte read) and
-- ensures we always reflect the current state without having to
-- watch for pickup events. Map and compass can be acquired mid-
-- room via dropped items, and the next N or B press will pick up
-- the change automatically.

-- Returns true if the player owns the map for the given dungeon.
-- Levels 1-8 share a bitmask byte; level 9 has its own byte.
local function dungeon_has_map(level)
    if level <= 0 then return false end
    if level == 9 then
        return mainmemory.read_u8(ADDR.inv_map9) ~= 0
    end
    local mask = mainmemory.read_u8(ADDR.inv_map)
    return bit.band(mask, bit.lshift(1, level - 1)) ~= 0
end

-- Returns true if the player owns the compass for the given dungeon.
local function dungeon_has_compass(level)
    if level <= 0 then return false end
    if level == 9 then
        return mainmemory.read_u8(ADDR.inv_compass9) ~= 0
    end
    local mask = mainmemory.read_u8(ADDR.inv_compass)
    return bit.band(mask, bit.lshift(1, level - 1)) ~= 0
end

-- Returns true if the room has been "visited" -- bit 0x20 of
-- the per-room screen-state byte. The game's MarkRoomVisited
-- routine sets this bit at the start of the room-scroll
-- transition (mode 4). The inventory map's drawing code reads
-- the same bit -- if set, the room appears filled-in on the
-- map. So checking this bit is equivalent to asking "would
-- the inventory map show this room as visited?"
local function dungeon_room_visited(level, room_id)
    if level <= 0 then return false end
    local base = (level <= 6) and ADDR.dungeon_screen_state_lo
                              or  ADDR.dungeon_screen_state_hi
    local b = mainmemory.read_u8(base + room_id)
    return bit.band(b, 0x20) ~= 0
end

-- Read the current dungeon's boss room ID from the level-info work
-- RAM. This byte is rewritten by InitMode2 when a dungeon is
-- entered.
--
-- IMPORTANT: $6BBC is in cartridge WRAM ($6000-$7FFF), NOT the
-- 2KB CPU RAM. So we must use memory.read_u8(addr, "System Bus")
-- here -- mainmemory.read_u8 only reaches $0000-$07FF and
-- returns garbage for higher addresses (this was a real bug; we
-- spent time chasing wrong outputs because of it).
local function dungeon_boss_room()
    local ok, v = pcall(function()
        return memory.read_u8(ADDR.level_boss_room, "System Bus")
    end)
    return (ok and v) or 0
end

-- Format the cardinal direction from one room to another in the
-- 8x8 dungeon grid. Used for the "compass without map" case.
-- Returns "North", "South", "East", "West", or a compound like
-- "Northwest". Returns "Here" if same room.
local function dungeon_room_direction(from_room, to_room)
    local fr_row, fr_col = room_row_col(from_room)
    local to_row, to_col = room_row_col(to_room)
    local parts = {}
    if to_row < fr_row then parts[#parts+1] = "North"
    elseif to_row > fr_row then parts[#parts+1] = "South" end
    if to_col < fr_col then parts[#parts+1] = "West"
    elseif to_col > fr_col then parts[#parts+1] = "East" end
    return #parts == 0 and "Here" or table.concat(parts)
end

-- ============================================================
-- PASSABILITY GRID
-- ============================================================

local function build_live_passability_grid()
    local ok, threshold = pcall(function() return mainmemory.read_u8(RAM.collision_threshold) end)
    if not ok or not threshold or threshold == 0 then threshold = 0x89 end
    local grid = {}
    for y = 1, GRID.height do grid[y] = {} end
    local has_data = false
    for col = 0, GRID.width-1 do
        for row = 0, GRID.height-1 do
            local offset = col*GRID.height + row
            local ok2, v = pcall(function() return memory.read_u8(RAM.play_area_tiles+offset, RAM.domain) end)
            local tile = (ok2 and v) or 0
            if tile ~= 0 then has_data = true end
            local pass = tile < threshold
            grid[row+1][col+1] = { passable=pass, category=pass and "walkable" or "solid", symbol=pass and "." or "#" }
        end
    end
    return has_data and grid or nil
end

local function seal_border_exits(grid, room_entry)
    local exit_tiles = {}
    if room_entry and type(room_entry.entities) == "table" then
        for _, e in ipairs(room_entry.entities) do
            if e.type_name == "exit" and e.approach_x then
                exit_tiles[e.approach_x..","..e.approach_y] = true
                if e.exit_direction == "East" then
                    exit_tiles["30,"..e.approach_y]=true; exit_tiles["31,"..e.approach_y]=true
                elseif e.exit_direction == "West" then
                    exit_tiles["0,"..e.approach_y]=true;  exit_tiles["1,"..e.approach_y]=true
                elseif e.exit_direction == "North" then
                    exit_tiles[e.approach_x..",0"]=true;  exit_tiles[e.approach_x..",1"]=true
                elseif e.exit_direction == "South" then
                    exit_tiles[e.approach_x..",20"]=true; exit_tiles[e.approach_x..",21"]=true
                end
            end
        end
    end
    local function seal_row(ry)
        local row = grid[ry+1]; if not row then return end
        for x = 0, GRID.width-1 do
            if not exit_tiles[x..","..ry] then
                local t = row[x+1]
                if t then t.passable=false; t.category="solid"; t.symbol="#" end
            end
        end
    end
    local function seal_col(cx)
        for y = 0, GRID.height-1 do
            if not exit_tiles[cx..","..y] then
                local row = grid[y+1]; local t = row and row[cx+1]
                if t then t.passable=false; t.category="solid"; t.symbol="#" end
            end
        end
    end
    -- Seal North, South, East by default. West left open for live scanner.
    -- Static exits punch holes through sealed borders.
    seal_row(0); seal_row(GRID.height-1); seal_col(GRID.width-1)
end

local function get_tile_grid()
    local key = state.cur_room_key
    if key ~= state.live_grid_room_key then
        state.live_grid_cache = nil
        state.live_grid_room_key = key
        state.live_grid_build_frame = state.frame_counter + 90
    end
    if state.live_grid_cache == nil and state.frame_counter >= state.live_grid_build_frame then
        local built = build_live_passability_grid()
        if built then
            seal_border_exits(built, state.cur_room_entry)
            local tx, ty = state.link_tile_x, state.link_tile_y
            if built[ty+1] and built[ty+1][tx+1] then built[ty+1][tx+1].passable = true end
        end
        state.live_grid_cache = built
    end
    return state.live_grid_cache
end

-- ============================================================
-- BFS PATHFINDER
-- ============================================================

local function resolve_route_target_tile(grid, entity)
    local tx = entity and (entity.approach_x or entity.x)
    local ty = entity and (entity.approach_y or entity.y)
    if type(grid)=="table" and not is_walkable(grid,tx,ty) then
        local nx,ny = nearest_walkable(grid,tx,ty)
        if nx==nil then return nil,nil end
        tx,ty = nx,ny
    end
    return tx,ty
end

local function route_steps_to_entity(entity)
    if not entity then return "No entity selected." end
    local grid = get_tile_grid()
    local sx = state.link_tile_x
    local sy = state.link_tile_y
    local in_cave = state.cur_in_cave
    local in_basement = state.cur_in_basement

    -- Caves: single linear room with no maze geometry. BFS adds
    -- nothing here. Cardinal directions to the entity's exact tile
    -- is the correct UX.
    if in_cave then
        local tx = entity.approach_x or entity.x
        local ty = entity.approach_y or entity.y
        if same_tile(sx,sy,tx,ty) then return "Already there." end
        local parts={}
        local dy=ty-sy; local dx=tx-sx
        if dy<0 then parts[#parts+1]=string.format("North %d",abs(dy))
        elseif dy>0 then parts[#parts+1]=string.format("South %d",dy) end
        if dx<0 then parts[#parts+1]=string.format("West %d",abs(dx))
        elseif dx>0 then parts[#parts+1]=string.format("East %d",dx) end
        return table.concat(parts,", ").."."
    end

    -- Basement (cellar) routing. Passage stairs need a three-leg
    -- override because BFS misreads the side-scroll geometry --
    -- logic lives in Dungeons.lua (see BASEMENTS section). Returns
    -- nil for treasure basements and non-stair entities, falling
    -- through to standard BFS.
    if in_basement and dungeons and dungeons.basement_route then
        local override = dungeons.basement_route(sx, sy, entity)
        if override then return override end
    end

    local target_x, target_y
    if entity.exit_direction=="East" or entity.exit_direction=="West" then
        target_x=entity.approach_x
        if not entity.fixed_approach and grid and is_walkable(grid,entity.approach_x,sy) then
            target_y=sy
        else target_y=entity.approach_y end
    elseif entity.exit_direction=="North" then
        target_y=entity.approach_y
        if not entity.fixed_approach and grid and is_walkable(grid,sx,entity.approach_y) then
            target_x=sx
        else target_x=entity.approach_x end
    elseif entity.exit_direction=="South" then
        target_y=entity.approach_y
        if not entity.fixed_approach and grid and is_walkable(grid,sx,entity.approach_y) then
            target_x=sx
        else target_x=entity.approach_x end
    else
        target_x,target_y = resolve_route_target_tile(grid, entity)
        if target_x==nil then return "Target is blocked." end
    end

    if same_tile(sx,sy,target_x,target_y) then return "Already there." end
    if entity.exit_direction=="East" and sx>=target_x then return "Already there." end

    if type(grid)~="table" then
        local parts={}; local dy=target_y-sy; local dx=target_x-sx
        if dy<0 then parts[#parts+1]=string.format("North %d",abs(dy))
        elseif dy>0 then parts[#parts+1]=string.format("South %d",dy) end
        if dx<0 then parts[#parts+1]=string.format("West %d",abs(dx))
        elseif dx>0 then parts[#parts+1]=string.format("East %d",dx) end
        return table.concat(parts,", ").."."
    end

    local sk=sx..","..sy
    local tk=target_x..","..target_y
    local queue={{x=sx,y=sy}}; local head=1
    local visited={[sk]=true}; local prev={}
    local dirs={{dx=0,dy=1,name="South"},{dx=1,dy=0,name="East"},{dx=0,dy=-1,name="North"},{dx=-1,dy=0,name="West"}}

    while head<=#queue do
        local node=queue[head]; head=head+1
        local nk=node.x..","..node.y
        if nk==tk then break end
        for _,d in ipairs(dirs) do
            local nx,ny=node.x+d.dx,node.y+d.dy
            local nkey=nx..","..ny
            if not visited[nkey] and is_walkable(grid,nx,ny) then
                visited[nkey]=true
                prev[nkey]={key=nk,dir=d.name}
                queue[#queue+1]={x=nx,y=ny}
            end
        end
    end

    if not visited[tk] then
        local fallback_dir = entity.exit_direction
        local function run_fallback(axis_check, axis_val)
            local fq={{x=sx,y=sy}}; local fh=1
            local fv={[sk]=true}; local fp={}; local found=nil
            while fh<=#fq do
                local fn=fq[fh]; fh=fh+1
                if axis_check(fn)==axis_val then found=fn; break end
                for _,d in ipairs(dirs) do
                    local nx,ny=fn.x+d.dx,fn.y+d.dy
                    local nk=nx..","..ny
                    if not fv[nk] and is_walkable(grid,nx,ny) then
                        fv[nk]=true
                        fp[nk]={key=fn.x..","..fn.y,dir=d.name}
                        fq[#fq+1]={x=nx,y=ny}
                    end
                end
            end
            if not found then return nil end
            local path={}; local cur=found.x..","..found.y
            while cur~=sk do
                local lnk=fp[cur]; if not lnk then break end
                table.insert(path,1,lnk.dir); cur=lnk.key
            end
            local parts,rd,rc={},nil,0
            for _,d in ipairs(path) do
                if d==rd then rc=rc+1
                else if rd then parts[#parts+1]=rd.." "..rc end; rd,rc=d,1 end
            end
            if rd then parts[#parts+1]=rd.." "..rc end
            return table.concat(parts,", ").."."
        end
        if fallback_dir=="East" or fallback_dir=="West" then
            local r=run_fallback(function(n)return n.x end, entity.approach_x)
            if r then return r end
        elseif fallback_dir=="North" or fallback_dir=="South" then
            local r=run_fallback(function(n)return n.y end, entity.approach_y)
            if r then return r end
        end
        return "No path."
    end

    local path_dirs={}; local cur=tk
    while cur~=sk do
        local lnk=prev[cur]; if not lnk then return "No path." end
        table.insert(path_dirs,1,lnk.dir); cur=lnk.key
    end
    local parts,rd,rc={},nil,0
    for _,d in ipairs(path_dirs) do
        if d==rd then rc=rc+1
        else if rd then parts[#parts+1]=rd.." "..rc end; rd,rc=d,1 end
    end
    if rd then parts[#parts+1]=rd.." "..rc end
    return table.concat(parts,", ").."."
end

-- ============================================================
-- ENTITY BUILDING
-- ============================================================

local function add_live_ram_exits(entities, grid, room_id, room_entry)
    if type(grid)~="table" then return end
    if room_entry and room_entry.suppress_auto_exits then return end

    local has_exit={North=false,South=false,West=false,East=false}
    for _,e in ipairs(entities) do
        if e.type_name=="exit" and e.exit_direction then has_exit[e.exit_direction]=true end
    end
    local is_h_row = room_id>=0x70 and room_id<=0x7F

    -- Find the single best gap (widest walkable run) along a border row
    -- or column. Returns the center index of the widest gap, or nil if
    -- nothing is wide enough. If multiple gaps tie for widest, the first
    -- one wins (deterministic). Replaces the prior all_gaps() approach
    -- which emitted one exit per gap -- in practice screens should only
    -- have a single exit per side, and BFS gets confused by duplicates.
    -- Baked screens with intentional dual exits per side (e.g. G6's
    -- two-bridge crossings) bypass this scan entirely via the
    -- has_exit[direction] gate set by the baked room_entry.
    local function best_gap(s, min_len)
        min_len = min_len or 1
        local best_center, best_len = nil, 0
        local in_gap, gs = false, nil
        for i = 1, #s do
            local ch = s:sub(i,i)
            if ch=="." and not in_gap then in_gap=true; gs=i
            elseif ch~="." and in_gap then
                local len=i-gs
                if len>=min_len and len>best_len then
                    best_len=len; best_center=gs-1+math.floor(len/2)
                end
                in_gap=false
            end
        end
        if in_gap then
            local len=#s-gs+1
            if len>=min_len and len>best_len then
                best_len=len; best_center=gs-1+math.floor(len/2)
            end
        end
        return best_center
    end

    local function row_str(y)
        local s=""
        for x=0,GRID.width-1 do
            local row=grid[y+1]; local tile=row and row[x+1]
            s=s..(tile and tile.passable and "." or "#")
        end
        return s
    end

    local function col_str(x)
        local s=""
        for y=0,GRID.height-1 do
            local row=grid[y+1]; local tile=row and row[x+1]
            s=s..(tile and tile.passable and "." or "#")
        end
        return s
    end

    if not has_exit.North then
        local cx = best_gap(row_str(1), 1)
        if cx then
            entities[#entities+1]={type_name="exit",label="North exit",x=cx,y=0,approach_x=cx,approach_y=1,exit_direction="North"}
        end
    end
    if not has_exit.South and not is_h_row then
        local cx = best_gap(row_str(20), 1)
        if cx then
            entities[#entities+1]={type_name="exit",label="South exit",x=cx,y=22,approach_x=cx,approach_y=20,exit_direction="South"}
        end
    end
    if not has_exit.West then
        local cy = best_gap(col_str(0), 1)
        if cy then
            entities[#entities+1]={type_name="exit",label="West exit",x=-1,y=cy,approach_x=0,approach_y=cy,exit_direction="West"}
        end
    end
    if not has_exit.East then
        local cy = best_gap(col_str(30), 1)
        if cy then
            entities[#entities+1]={type_name="exit",label="East exit",x=31,y=cy,approach_x=30,approach_y=cy,exit_direction="East"}
        end
    end
end

local function build_entity_list()
    local entities = {}
    local level   = state.cur_level
    local room_id = state.cur_room_id
    local in_cave = state.cur_in_cave
    local quest   = state.cur_quest
    local m = world_map and world_map.get_screen_manifest(room_id)

    if in_cave then
        local loc_idx = m and m.location_idx or 0
        local room_override = CAVE_ROOM_OVERRIDES[room_id]
        local cave_info = lookup_cave(loc_idx, state.cur_quest)
        local letter_shown = mainmemory.read_u8(0x0666) >= 2
        -- CAVE_ROOM_OVERRIDES takes priority over shop detection
        -- potion_shop overrides redirect to a different shop_loc's inventory
        local shop
        if room_override and room_override.cave_type == "potion_shop" then
            if letter_shown then
                shop = SHOP_INVENTORY[room_override.shop_loc]
            end
        elseif not room_override then
            -- Loc 26 (medicine-woman potion shops) gate as a class on the
            -- letter. Without 0x0666 >= 2 the shopkeeper won't sell, so we
            -- don't populate the shop inventory in the entity list either.
            -- All other shop locs (29, 30, 31, 32) populate normally.
            if loc_idx == 26 and not letter_shown then
                shop = nil
            else
                shop = SHOP_INVENTORY[loc_idx]
            end
        end
        if shop then
            local shop_pos={{11,13},{15,13},{19,13}}
            local purchased = (mainmemory.read_u8(ADDR.obj_type_base+1)==0)
            if not purchased then
                for i,item in ipairs(shop.items) do
                    -- Skip None items (price 0 or label "Item 3F" = empty slot)
                    if item.price > 0 and item.label ~= "Item 3F" then
                        local sx=shop_pos[i] and shop_pos[i][1] or (8+(i-1)*8)
                        local sy=shop_pos[i] and shop_pos[i][2] or 13
                        entities[#entities+1]={type_name="item",label=string.format("%s, %d Rupees",item.label,item.price),x=sx,y=sy,approach_x=sx,approach_y=sy}
                    end
                end
            end
            entities[#entities+1]={type_name="exit",label="Exit",x=15,y=20,approach_x=15,approach_y=20}
            state.entities=entities; state.entity_count=#entities; return
        end

        -- Door Repair Cave (loc 23): the old man auto-charges 20 rupees on
        -- entry with no purchase decision. No shop inventory, no NPC to
        -- interact with, nothing to navigate to. Just the exit. Dialog and
        -- rupee-loss announcement are handled by the cave update loop
        -- elsewhere.
        if loc_idx == 23 then
            entities[#entities+1]={type_name="exit",label="Exit",x=15,y=20,approach_x=15,approach_y=20}
            state.entities=entities; state.entity_count=#entities; return
        end

        -- Hint Caves (loc 21, 25): the old man speaks a single hint and
        -- nothing else. No item to pick up, no rupees to pay, no shop.
        -- Only the exit is needed. The dialog itself is handled by the
        -- cave update loop's deferred-dialog mechanism, same as any
        -- other cave with a one-line NPC line.
        if loc_idx == 21 or loc_idx == 25 then
            entities[#entities+1]={type_name="exit",label="Exit",x=15,y=20,approach_x=15,approach_y=20}
            state.entities=entities; state.entity_count=#entities; return
        end

        -- Loc 26 Potion Shop (medicine woman): until the player has shown
        -- the letter to the old woman (InvLetter at 0x0666 >= 2), the cave
        -- behaves as empty. No shop inventory, no NPC interaction, no
        -- dropped items. The cave is technically populated in RAM but the
        -- shopkeeper won't sell, so we hide everything from the entity list
        -- to match. Once the letter is shown, the letter-state edge
        -- detector fires a rebuild and the shop appears normally.
        -- Bypassed for rooms with explicit overrides (e.g. A14 / 0x0D).
        if loc_idx == 26 and not room_override and not letter_shown then
            entities[#entities+1]={type_name="exit",label="Exit",x=15,y=20,approach_x=15,approach_y=20}
            state.entities=entities; state.entity_count=#entities; return
        end

        -- NPC slot scan (internal use only -- not added to entity list)
        local npc_slot
        for slot=ENEMY_SLOT_FIRST,ENEMY_SLOT_LAST do
            local t=mainmemory.read_u8(ADDR.obj_type_base+slot)
            if t>=CAVE_NPC_MIN and t<=CAVE_NPC_MAX then
                npc_slot=slot; break
            end
        end

        -- Skip dropped item scan for gambling/information caves
        if loc_idx ~= 22 and loc_idx ~= 27 and loc_idx ~= 28 then
        for slot=ITEM_SLOT_FIRST,ITEM_SLOT_LAST do
            local ot=mainmemory.read_u8(ADDR.obj_type_base+slot)
            local os=mainmemory.read_u8(ADDR.obj_state_base+slot)
            if ot==DROPPED_ITEM_TYPE and os~=0xFF then
                local itx,ity=pixel_to_tile(mainmemory.read_u8(ADDR.obj_x_base+slot),mainmemory.read_u8(ADDR.obj_y_base+slot))
                entities[#entities+1]={type_name="item",label=ITEM_NAMES[os] or string.format("Item %02X",os),x=itx,y=ity,approach_x=itx,approach_y=ity}
            end
        end
        end

        -- NPC-gated cave types: items only shown when Moblin/NPC is present
        if loc_idx==17 or SECRET_RUPEE_CAVE_LOCS[loc_idx] or CAVE_ROOM_OVERRIDES[room_id] then
            local npc_present=false
            local npc_gate_slot=-1
            for slot=ENEMY_SLOT_FIRST,ENEMY_SLOT_LAST do
                local t=mainmemory.read_u8(ADDR.obj_type_base+slot)
                if t>=CAVE_NPC_MIN and t<=CAVE_NPC_MAX then npc_present=true; npc_gate_slot=slot; break end
            end
            state.cave_npc_slot_baseline=npc_gate_slot
            if loc_idx==17 then
                -- Heart or Potion Cave: items only show if the NPC is
                -- present AND the cave's reward hasn't been collected
                -- yet (per the persistent $067F flag). The flag check
                -- handles the cave-load timing window where the NPC
                -- slot is briefly populated even for cleared caves.
                if npc_present and not cave_reward_collected(room_id) then
                    entities[#entities+1]={type_name="item",label="Heart Container",x=19,y=13,approach_x=19,approach_y=13}
                    entities[#entities+1]={type_name="item",label="Blue Potion",x=11,y=13,approach_x=11,approach_y=13}
                end
            else
                local is_rupee_override = room_override and room_override.cave_type == "rupee"
                if SECRET_RUPEE_CAVE_LOCS[loc_idx] or is_rupee_override then
                    -- Secret Rupee Cave: same persistent-flag gate as
                    -- Heart or Potion. cave_rupee_collected is the
                    -- session-side flag for mid-session collection;
                    -- cave_reward_collected reads Z1's persistent
                    -- save-state bit. Either flag set hides the rupee.
                    if npc_present
                            and not state.cave_rupee_collected
                            and not cave_reward_collected(room_id) then
                        entities[#entities+1]={type_name="item",label="Rupee",x=15,y=13,approach_x=15,approach_y=13}
                    end
                end
                local is_letter_override = room_override and room_override.cave_type == "letter"
                if is_letter_override then
                    -- Letter override: same persistent-flag gate.
                    if npc_present and not cave_reward_collected(room_id) then
                        entities[#entities+1]={type_name="item",label="Letter",x=15,y=13,approach_x=15,approach_y=13}
                    end
                end
            end
        elseif loc_idx == 20 then
            -- Any Road Cave: three staircases that warp Link to one of
            -- the four Take Any Road overworld screens. The destinations
            -- aren't fixed -- they depend on which of the four caves
            -- you're currently in.
            --
            -- Mechanism (verified against Z_05.asm CheckSubroom):
            --   * The game keeps a 4-element cycle table in cartridge
            --     PRG-RAM at $6BB2 (System Bus domain). Each byte is
            --     the room ID of one of the four Take Any Road caves.
            --   * Stair detection: Link's X position when on a stair is
            --     $50 (left), $80 (middle), $B0 (right) -- which map to
            --     tile columns 10, 16, 20 on row 13.
            --   * Destination index = (current_idx + stair) AND $03,
            --     where stair is 1/2/3 for left/middle/right.
            --
            -- We read the live cycle every time we build the entity
            -- list, so this works in both quests and adapts to whichever
            -- four caves the current ROM defines. The labels are
            -- computed entity labels ("Left stair to G6" etc.) so
            -- existing PageUp/PageDown/End hotkeys cycle through them
            -- like any other entity.
            local stairs = {
                { stair = 1, x = 10, side = "Left" },
                { stair = 2, x = 16, side = "Middle" },
                { stair = 3, x = 20, side = "Right" },
            }
            local cycle = {}
            for i = 0, 3 do
                local ok, v = pcall(function()
                    return memory.read_u8(0x6BB2 + i, RAM.domain)
                end)
                cycle[i] = (ok and v) or 0
            end
            -- Find current cave's slot in the cycle.
            local cur_idx = -1
            for i = 0, 3 do
                if cycle[i] == room_id then cur_idx = i; break end
            end
            for _, s in ipairs(stairs) do
                local label
                if cur_idx >= 0 then
                    local dest_idx = bit.band(cur_idx + s.stair, 0x03)
                    local dest_room = cycle[dest_idx]
                    -- Read coordinate format from settings; fall back
                    -- to numeric if Take Any Road fires before settings
                    -- are wired up (shouldn't happen but be safe).
                    local fmt = (state.settings and state.settings.coordinate_format) or "numeric"
                    label = string.format("%s stair to %s",
                        s.side, screen_coord_str(dest_room, fmt))
                else
                    -- Cycle table not populated or current room not in
                    -- it -- fall back to a generic label so the entity
                    -- still appears.
                    label = string.format("%s stair", s.side)
                end
                entities[#entities+1] = {
                    type_name  = "exit",
                    label      = label,
                    x          = s.x,
                    y          = 13,
                    approach_x = s.x,
                    approach_y = 13,
                }
            end
            state.entities=entities; state.entity_count=#entities; return
        elseif loc_idx == 22 then
            -- Gambling Cave: show three bet options
            entities[#entities+1]={type_name="item",label="Bet 10 Rupees",x=11,y=13,approach_x=11,approach_y=13}
            entities[#entities+1]={type_name="item",label="Bet 10 Rupees",x=15,y=13,approach_x=15,approach_y=13}
            entities[#entities+1]={type_name="item",label="Bet 10 Rupees",x=19,y=13,approach_x=19,approach_y=13}
        elseif loc_idx == 27 or loc_idx == 28 then
            -- Information Cave: show payment options unless already paid
            if not state.cave_info_paid then
                if loc_idx == 27 then
                    entities[#entities+1]={type_name="item",label="Pay 5 Rupees",x=11,y=13,approach_x=11,approach_y=13}
                    entities[#entities+1]={type_name="item",label="Pay 10 Rupees",x=15,y=13,approach_x=15,approach_y=13}
                    entities[#entities+1]={type_name="item",label="Pay 20 Rupees",x=19,y=13,approach_x=19,approach_y=13}
                else
                    entities[#entities+1]={type_name="item",label="Pay 10 Rupees",x=11,y=13,approach_x=11,approach_y=13}
                    entities[#entities+1]={type_name="item",label="Pay 30 Rupees",x=15,y=13,approach_x=15,approach_y=13}
                    entities[#entities+1]={type_name="item",label="Pay 50 Rupees",x=19,y=13,approach_x=19,approach_y=13}
                end
            end
        else
            local rs=mainmemory.read_u8(ADDR.obj_state_base+ROOM_ITEM_SLOT)
            local ri=mainmemory.read_u8(ADDR.room_item_id)
            local already_have_letter = (loc_idx == 24) and (state.cave_letter_collected or mainmemory.read_u8(0x0666) >= 1)
            if rs==0xFF and ri~=ROOM_ITEM_NONE and not already_have_letter then
                local itx,ity
                if loc_idx==16 or loc_idx==18 or loc_idx==19
                        or (room_override and room_override.cave_type == "sword") then
                    local sword_loc = (room_override and room_override.sword_loc) or loc_idx
                    if not CAVE_ROOM_OVERRIDES[room_id] or room_override.cave_type == "sword" then
                        local sword_level = mainmemory.read_u8(0x0657)
                        -- Sword level required to consider the sword "already taken":
                        --   loc 16 (Wood Sword)    -> level 1
                        --   loc 18 (White Sword)   -> level 2
                        --   loc 19 (Magical Sword) -> level 3
                        local required = (sword_loc == 19 and 3)
                                      or (sword_loc == 18 and 2)
                                      or 1
                        if sword_level < required then itx,ity=15,13 end
                    else
                        itx,ity=pixel_to_tile(mainmemory.read_u8(ADDR.obj_x_base+ROOM_ITEM_SLOT),mainmemory.read_u8(ADDR.obj_y_base+ROOM_ITEM_SLOT))
                    end
                else
                    itx,ity=pixel_to_tile(mainmemory.read_u8(ADDR.obj_x_base+ROOM_ITEM_SLOT),mainmemory.read_u8(ADDR.obj_y_base+ROOM_ITEM_SLOT))
                end
                if itx then
                    -- Item label: sword caves use known names, others fall back to ROM item ID
                    local item_label
                    if loc_idx == 16 then item_label = "Wood Sword"
                    elseif loc_idx == 18 then item_label = "White Sword"
                    elseif loc_idx == 19 then item_label = "Magical Sword"
                    elseif loc_idx == 24 then item_label = "Letter"
                    else item_label = ITEM_NAMES[ri] or string.format("Item %02X", ri) end
                    entities[#entities+1]={type_name="item",label=item_label,x=itx,y=ity,approach_x=itx,approach_y=ity}
                end
            end
        end
        entities[#entities+1]={type_name="exit",label="Exit",x=15,y=20,approach_x=15,approach_y=20}
        state.entities=entities; state.entity_count=#entities; return
    end

    -- OVERWORLD AND DUNGEON
    -- Both paths share the live grid, the slot scans for enemies and items,
    -- and the entity output. They differ in what static entities seed the
    -- list. Overworld pulls baked room entries, runs the border-gap scan
    -- for exits, and adds cave entrances. Dungeon skips all three of those
    -- (none make sense underground) and instead runs the dungeon door scan.
    local grid = get_tile_grid()
    local room_entry = state.cur_room_entry

    if level == 0 then
        -- Live stair-tile scan. Both visible cave entrances and revealed
        -- secrets (bomb walls, burned trees, armos touches, gravestone
        -- pushes, recorder reveals) render the cave staircase as the
        -- standard 2x2 cluster topped by tile 0x70 in the play-area
        -- grid. So a single scan handles both cases -- if 0x70 is
        -- present, that's the actual cave/stair position regardless
        -- of mechanic.
        --
        -- Self-consistent rule: stair tile present = cave is reachable;
        -- stair tile absent = either no cave on screen, or the reveal
        -- hasn't been triggered yet.
        --
        -- Label depends on context: OVERWORLD_REVEAL_ROOMS get "Stairs"
        -- (because the player triggered something to expose them);
        -- everything else gets "Entrance" (because the cave was
        -- visible from the start). Both behave the same way -- walk
        -- onto the tile to descend.
        --
        -- Only scans on screens with has_underground to avoid the
        -- 704-tile cost on plain overworld screens. Bails on first
        -- match.
        -- Two-slot scan: find the first 0x24 (cave mouth) AND the
        -- first 0x70 (revealed stair). Z1 only ever puts one of each
        -- per screen, so two slots is enough. Most screens fill at
        -- most one slot; rare dual-entry screens (L1 Dungeon 5: visible
        -- entrance + armos-revealed stair) fill both. Each filled
        -- slot becomes a separate live entity.
        --
        -- Quest gating: skip the scan entirely when the screen's cave
        -- is disabled in the current quest (ignore_q1/q2 flag set) or
        -- marked as unfindable. Otherwise the scan can pick up
        -- decorative tile bytes that happen to match 0x24/0x70 on
        -- screens like L2 (Dungeon 5 Q2 entrance) and emit a
        -- phantom entrance in Q1.
        --
        -- 0x70: top-left of the bare staircase cluster.
        -- 0x24: walkable cave-entrance tile. If the tile below is also
        --       0x24 (top row of an all-0x24 revealed cluster), step
        --       down to the bottom row -- that's where Link enters.
        local mouth_x, mouth_y = nil, nil
        local stair_x, stair_y = nil, nil
        local cave_active = m and m.has_underground
                and not (m.ignore_q1 and quest==1)
                and not (m.ignore_q2 and quest==2)
                and not (world_map and world_map.is_unfindable
                        and world_map.is_unfindable(state.cur_room_key))
        if cave_active then
            for sy = 0, GRID.height - 1 do
                for sx = 0, GRID.width - 1 do
                    local offset = sx * GRID.height + sy
                    local ok, tile = pcall(function()
                        return memory.read_u8(RAM.play_area_tiles + offset, RAM.domain)
                    end)
                    if ok then
                        if tile == OVERWORLD_STAIR_TILE and not stair_x then
                            stair_x, stair_y = sx, sy
                        elseif tile == OVERWORLD_CAVE_MOUTH and not mouth_x then
                            local below_ok, below = pcall(function()
                                return memory.read_u8(
                                    RAM.play_area_tiles + sx * GRID.height + (sy + 1),
                                    RAM.domain)
                            end)
                            if below_ok and below == OVERWORLD_CAVE_MOUTH then
                                mouth_x, mouth_y = sx, sy + 1
                            else
                                mouth_x, mouth_y = sx, sy
                            end
                        end
                    end
                    if mouth_x and stair_x then break end
                end
                if mouth_x and stair_x then break end
            end
        end

        if room_entry and type(room_entry.entities)=="table" then
            -- Power Bracelet special case: E3 (room 0x24) has a baked
            -- Event marking the armos-touch item pickup. The Event is
            -- a static placeholder for "something to find here." The
            -- actual bracelet entity is emitted automatically by the
            -- live slot 0x13 scan further down (when room_item_id is
            -- 0x14 and obj_state is active). To avoid showing both
            -- Event AND Bracelet at the same time, suppress the Event
            -- once the bracelet is either on the floor (slot active)
            -- OR already collected (InvBracelet != 0).
            local bracelet_visible = false
            if room_id == 0x24 then
                local bracelet_collected = mainmemory.read_u8(0x0665) ~= 0
                local slot_state = mainmemory.read_u8(
                    ADDR.obj_state_base + ROOM_ITEM_SLOT)
                local slot_item  = mainmemory.read_u8(ADDR.room_item_id)
                local bracelet_on_floor = (slot_item ~= ROOM_ITEM_NONE)
                                      and (slot_state < 0x80)
                bracelet_visible = bracelet_collected or bracelet_on_floor
            end
            -- Heart Container F16 (room 0x5F) special case. Z1
            -- respawns the heart's slot on every screen entry, so
            -- slot state is useless on revisit. The actual collected
            -- bit lives in the per-screen overworld flags table at
            -- $067F + room_id, bit 0x10. Verified empirically by
            -- diffing CPU RAM before vs after picking up the F16
            -- heart -- only that single byte/bit changed in the
            -- region that survives a screen exit/entry. Same
            -- mechanism applies to the E3 Power Bracelet ($06A3).
            local item_screen_collected = false
            if room_id == 0x5F then
                local flag = mainmemory.read_u8(0x067F + room_id)
                item_screen_collected = bit.band(flag, 0x10) ~= 0
            end
            for _,e in ipairs(room_entry.entities) do
                if type(e)=="table" and type(e.label)=="string" and type(e.x)=="number" then
                    -- Three-category suppression rule:
                    --   * type_name == "cave" suppressed when EITHER
                    --     mouth_x or stair_x is set. The baked "cave"
                    --     Event is a placeholder for any hidden
                    --     entrance regardless of whether the reveal
                    --     produces a 0x24 cave-mouth (bomb wall, burn
                    --     tree, most armos secrets) or a 0x70 stair
                    --     (lake drain, some armos secrets). Once the
                    --     player triggers the reveal, the live entity
                    --     replaces the baked Event.
                    --   * type_name == "stairs" suppressed when stair_x
                    --     set (the live Stairs from the 0x70 scan
                    --     replaces it). Used by dual-entry screens
                    --     like L1 where a stair-typed baked Event
                    --     marks the alt route's reveal location.
                    --   * type_name == "landmark" never suppressed --
                    --     non-cave markers (false walls, lake drains,
                    --     docks) coexist with anything.
                    --   * one-time item screens (bracelet_visible,
                    --     item_screen_collected) suppress the baked
                    --     Event once the player has the item, since
                    --     it can't be picked up again.
                    local suppress = ((mouth_x or stair_x) and e.type_name == "cave")
                                  or (stair_x and e.type_name == "stairs")
                                  or (bracelet_visible and e.type_name == "cave")
                                  or (item_screen_collected and e.type_name == "cave")
                    if not suppress then
                        entities[#entities+1]=e
                    end
                end
            end
        end

        -- Emit one live entity per filled slot. A screen with both
        -- 0x24 and 0x70 (e.g. L1 after armos push) emits both.
        if mouth_x then
            entities[#entities+1] = {
                type_name  = "cave",
                label      = "Cave",
                x          = mouth_x,
                y          = mouth_y,
                approach_x = mouth_x,
                approach_y = mouth_y,
            }
        end
        if stair_x then
            entities[#entities+1] = {
                type_name  = "stairs",
                label      = "Stairs",
                x          = stair_x,
                y          = stair_y,
                approach_x = stair_x,
                approach_y = stair_y,
            }
        end
        if mouth_x or stair_x then
            -- Mark the reveal handled so the polling helper stops
            -- looking for a tile-to-emit. Reset on room entry.
            state.overworld_reveal_handled = true
        end

        add_live_ram_exits(entities, grid, room_id, room_entry)
    end

    -- Dungeon door scan: inspect the FACE tile (the wall-edge tile) for
    -- each of the four cardinal door positions. The face tile tells us
    -- whether there's a door there at all -- a plain wall has solid wall
    -- bytes (e.g. 0xDC for north), a door has either walkable floor bytes
    -- (open door) or a known door-face graphic (closed locked door, etc).
    --
    -- We can't use the inside-doorway tile (door.x, door.y) as the open
    -- detector by itself, because most dungeon rooms have walkable floor
    -- at those interior coordinates regardless of whether a door exists.
    -- A south-only room still has walkable floor at row 3 below the north
    -- wall -- that doesn't mean there's a north door.
    --
    -- Logic:
    --   face tile is walkable (< collision threshold)
    --     -> open door, add exit entity routing through doorway
    --   face tile is in DOOR_TILE_TYPES[direction] (e.g. 0x98 for north)
    --     -> closed door of known type, add informational entity
    --   face tile is anything else (plain wall, closed bombable, closed
    --     shutter)
    --     -> no entity. Bombables and shutters reveal themselves through
    --        play, the same way a sighted player discovers them.
    --
    -- Skipped entirely in basement mode: side-scroll cellars don't have
    -- the standard 4-cardinal door layout. The exit is the stairs-up
    -- which the basement-specific scan handles separately.
    if level > 0 and not state.cur_in_basement and type(grid) == "table" then
        local ok_thr, threshold = pcall(function()
            return mainmemory.read_u8(RAM.collision_threshold)
        end)
        if not ok_thr or not threshold or threshold == 0 then threshold = 0x89 end

        for _, door in ipairs(DUNGEON_DOORS) do
            local offset = door.face_x * GRID.height + door.face_y
            local ok, face_tile = pcall(function()
                return memory.read_u8(RAM.play_area_tiles + offset, RAM.domain)
            end)
            if ok and face_tile then
                if face_tile < threshold then
                    -- Open door: face tile is walkable. Route through it.
                    entities[#entities+1] = {
                        type_name      = "exit",
                        label          = door.label,
                        x              = door.x,
                        y              = door.y,
                        approach_x     = door.x,
                        approach_y     = door.y,
                        exit_direction = door.direction,
                    }
                else
                    local face_tiles = DOOR_TILE_TYPES[door.direction]
                    local entry = face_tiles and face_tiles[face_tile]
                    if type(entry) == "table" and entry.passable then
                        -- Bombed-open hole or other passable special face
                        -- (face tile is technically solid but the inner
                        -- doorway is walkable and the player can move
                        -- through). Route through it as an exit.
                        entities[#entities+1] = {
                            type_name      = "exit",
                            label          = entry.label .. ", " .. door.direction,
                            x              = door.x,
                            y              = door.y,
                            approach_x     = door.x,
                            approach_y     = door.y,
                            exit_direction = door.direction,
                        }
                    elseif entry then
                        -- Closed door of known type (locked, shutter, etc).
                        -- Either a string label (legacy form) or a table.
                        local label = type(entry) == "table" and entry.label or entry
                        entities[#entities+1] = {
                            type_name  = "door",
                            label      = label .. ", " .. door.direction,
                            x          = door.face_x,
                            y          = door.face_y,
                            approach_x = door.x,
                            approach_y = door.y,
                        }
                    end
                end
            end
        end

        -- Push-block detection. The game places the push block as object
        -- type 0x68 in slot 0x0B (the highest enemy slot, used for tile
        -- objects that aren't actually creatures). Its live position is
        -- at obj_x_base+0x0B and obj_y_base+0x0B. Push-block rooms are
        -- gated by LevelBlockAttrsD bit 0x40 in ROM, but the live slot
        -- check is sufficient -- if type 0x68 is in the slot, the room
        -- has a push block.
        --
        -- We can't rely on a fixed tile-grid scan position because push
        -- blocks aren't always on row 10. Reading slot 0x0B directly
        -- gives us the actual position regardless of layout.
        --
        -- The first time we see the block in this room, we cache its X/Y
        -- as the initial position. On every later rebuild, we suppress
        -- the entity if the block has moved -- it can be pushed in any
        -- of N/S/E/W and only once, so once it's off its starting tile
        -- it's no longer interactable. The baseline resets on room entry,
        -- so re-entering a room where the block has reset to its initial
        -- position will re-show the entity, matching the game's reset.
        local push_slot_type = mainmemory.read_u8(ADDR.obj_type_base + ENEMY_SLOT_LAST)
        if push_slot_type == PUSH_BLOCK_OBJ_TYPE then
            local px = mainmemory.read_u8(ADDR.obj_x_base + ENEMY_SLOT_LAST)
            local py = mainmemory.read_u8(ADDR.obj_y_base + ENEMY_SLOT_LAST)
            -- Capture initial position on first sight
            if state.push_block_initial_x == nil then
                state.push_block_initial_x = px
                state.push_block_initial_y = py
            end
            -- Only emit the entity if the block is still at its initial
            -- position. Once moved, drop it from the list.
            if px == state.push_block_initial_x
                    and py == state.push_block_initial_y then
                local block_tx, block_ty = pixel_to_tile(px, py)
                entities[#entities+1] = {
                    type_name  = "push_block",
                    label      = "Push block",
                    x          = block_tx,
                    y          = block_ty,
                    approach_x = block_tx,
                    approach_y = block_ty + 1,  -- approach from south
                }
            end
        end

        -- Stair detection. Stairs in dungeon rooms are rendered as a 2x2
        -- tile cluster: 0x70 0x72 on the top row, 0x71 0x73 on the bottom.
        -- All four bytes are walkable (< collision threshold), so Link
        -- walks onto them to trigger the side-scroll basement transition.
        --
        -- We scan the entire 32x22 tile grid for 0x70 (the top-left of
        -- the cluster) and emit one "Stairs" entity per occurrence. The
        -- entity targets the 0x70 tile itself as both position and
        -- approach, since walking onto it is the trigger.
        --
        -- Both static stairs (always present) and revealed stairs (after
        -- pushing a block) get picked up the same way -- the live tile
        -- grid reflects whatever's currently in the room. Cost is one
        -- linear scan over 704 tiles per rebuild, no per-frame work.
        for sy = 0, GRID.height - 1 do
            for sx = 0, GRID.width - 1 do
                local offset = sx * GRID.height + sy
                local ok, tile = pcall(function()
                    return memory.read_u8(RAM.play_area_tiles + offset, RAM.domain)
                end)
                if ok and tile == STAIR_TILE then
                    entities[#entities+1] = {
                        type_name  = "stairs",
                        label      = "Stairs",
                        x          = sx,
                        y          = sy,
                        approach_x = sx,
                        approach_y = sy,
                    }
                end
            end
        end

        -- Person transaction (shop-style dungeon NPC). When a dungeon
        -- NPC is present AND the live $0415 PersonTextSelector matches
        -- a known transaction in Dungeons.PERSON_TRANSACTIONS, emit a
        -- navigable entity at the NPC's tile position with cost in the
        -- label ("Bomb Upgrade, 100 Rupees"). Same UX pattern as cave
        -- shop items so the player has a clear target with the price
        -- known up front.
        --
        -- Suppression: completion_check() returns true once the
        -- transaction has been fulfilled (e.g. MaxBombs > 8 means the
        -- bomb upgrade was bought). At that point the entity stops
        -- being emitted on rebuild, so once the player walks into the
        -- Moblin and pays, the offer disappears.
        --
        -- The Moblin/NPC ALSO appears in the regular enemy slot scan
        -- below as "Friendly Moblin" (or whatever the type name is).
        -- Both entities coexist: the enemy entry is the creature, the
        -- transaction entry is the offer. Player navigates to the
        -- transaction entry to see the price, walks onto it to pay.
        if dungeons and dungeons.lookup_transaction_by_selector then
            local selector = mainmemory.read_u8(0x0415)
            local txn = dungeons.lookup_transaction_by_selector(selector)
            if txn and not (txn.completion_check and txn.completion_check()) then
                local npc_present = false
                for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
                    local t = mainmemory.read_u8(ADDR.obj_type_base + slot)
                    if t >= DUNGEON_NPC_MIN and t <= DUNGEON_NPC_MAX then
                        npc_present = true
                        break
                    end
                end
                if npc_present then
                    entities[#entities+1] = {
                        type_name  = "item",
                        label      = string.format("%s, %d Rupees",
                                                   txn.label, txn.cost),
                        x          = 15,
                        y          = 13,
                        approach_x = 15,
                        approach_y = 13,
                    }
                end
            end
        end
    end

    -- Basement (cellar) entities. Logic lives in Dungeons.lua
    -- (see BASEMENTS section). The treasure basement's floor item
    -- is emitted by the room-item slot scan further down.
    if state.cur_in_basement and dungeons and dungeons.basement_entities then
        for _, e in ipairs(dungeons.basement_entities()) do
            entities[#entities+1] = e
        end
    end

    if false then  -- old basement scan parked here for reference
        local STAIR_COL_TILE = 0x6F
        local stair_cols = {}
        for sx = 0, GRID.width - 1 do
            -- Scan row 2 -- the topmost stair tile, just below the
            -- archway. One read per column rather than scanning the
            -- whole grid.
            local offset = sx * GRID.height + 2
            local ok, tile = pcall(function()
                return memory.read_u8(RAM.play_area_tiles + offset, RAM.domain)
            end)
            if ok and tile == STAIR_COL_TILE then
                -- Skip the right column of an adjacent pair: if the
                -- previous column was already a stair, this one is
                -- the same staircase.
                if not stair_cols[#stair_cols] or
                        stair_cols[#stair_cols] ~= sx - 1 then
                    stair_cols[#stair_cols+1] = sx
                end
            end
        end
        -- Treasure-vs-passage discriminator (verified empirically by
        -- basement_probe 20260504):
        --   L9 R0x00 treasure: attr_a=0x07, attr_b=0x07 -> A == B
        --   L8 R0x2F passage:  attr_a=0x4C, attr_b=0x3F -> A != B
        -- Treasure basements have only one source room, so A and B
        -- both point to the same room. Passage basements connect two
        -- distinct dungeon rooms, so A and B differ.
        --
        -- Treasure basements occasionally render decorative 0x6F tiles
        -- on the side wall (not actually stairs). Without this gate the
        -- live scan emits a phantom right-side stair entity. So in a
        -- treasure basement we only emit the FIRST 0x6F column found
        -- (the real stair back to the source room).
        local ok_a, attr_a = pcall(function()
            return memory.read_u8(0x687E + state.cur_room_id, RAM.domain)
        end)
        local ok_b, attr_b = pcall(function()
            return memory.read_u8(0x68FE + state.cur_room_id, RAM.domain)
        end)
        local is_passage = ok_a and ok_b and attr_a ~= attr_b
        if not is_passage and #stair_cols > 1 then
            stair_cols = { stair_cols[1] }
        end
        local function stair_label(idx, total, col)
            if total == 1 then return "Stairs up" end
            if total == 2 then
                return idx == 1 and "Left stairs up" or "Right stairs up"
            end
            -- More than 2 (shouldn't happen but be defensive): label
            -- by column number.
            return string.format("Stairs up at column %d", col)
        end
        -- Approach the stair at its BASE (row 17, the floor row Link
        -- can walk on between stairs), not its top (row 0). The stair
        -- column tiles are walkable but the stair's TOP is sealed by
        -- a wall above it -- you can't cross horizontally there. So
        -- routing to the top would give directions like "North 15,
        -- West 18" when on the wrong stair, and the player would
        -- bonk against the top wall trying to cross. Routing to the
        -- floor (row 17) lets cardinal directions produce a sensible
        -- "South N, West M" path: walk down off the current stair,
        -- across the floor to the target column, and walking north
        -- from there triggers the ascent.
        --
        -- Empirically verified stair-base coords: left (6, 17),
        -- right (24, 17), middle floor (15, 17).
        for i, col in ipairs(stair_cols) do
            entities[#entities+1] = {
                type_name      = "exit",
                label          = stair_label(i, #stair_cols, col),
                x              = col,
                y              = 0,
                approach_x     = col,
                approach_y     = 17,
                exit_direction = "North",
            }
        end
        -- No fallback emit. Previous version emitted a hardcoded
        -- "Stairs up" at (6, 0) when the 0x6F scan found nothing,
        -- but that produced phantom stairs in basement layouts where
        -- the stair geometry doesn't render with the 0x6F tile (e.g.
        -- treasure basements with item-room layouts). If the scan
        -- finds no stair columns, the player can still navigate to
        -- the basement's item via the room-item / dropped-item slot
        -- scans below.
    end

    -- Per-enemy labels come from GameData's ENEMY_TYPE_NAMES, keyed by the
    -- live RAM obj_type at 0x034F+slot. The ROM placement byte at 0x18500
    -- indexes a monster-group table, not a direct type ID, so using it for
    -- labels mislabels every overworld enemy. Reading the RAM type directly
    -- gives the actual creature in each slot (e.g. mixed Octoroks + Zora).
    local new_enemy_slots = {}
    local new_enemy_type_baseline = {}
    for slot=ENEMY_SLOT_FIRST,ENEMY_SLOT_LAST do
        local t=mainmemory.read_u8(ADDR.obj_type_base+slot)
        if t~=0 and t<MONSTER_PROJECTILE_MIN then
            local is_fire=t>=FIRE_TYPE_MIN and t<=FIRE_TYPE_MAX
            local is_npc=(t>=DUNGEON_NPC_MIN and t<=DUNGEON_NPC_MAX) or (t>=CAVE_NPC_MIN and t<=CAVE_NPC_MAX)
            local is_push_block = (t == PUSH_BLOCK_OBJ_TYPE)
            if not is_fire and not is_push_block and not is_npc then
                local ex,ey=pixel_to_tile(mainmemory.read_u8(ADDR.obj_x_base+slot),mainmemory.read_u8(ADDR.obj_y_base+slot))
                local label=ENEMY_NAMES[t] or string.format("Enemy %02X", t)
                entities[#entities+1]={type_name="enemy",label=label,x=ex,y=ey,approach_x=ex,approach_y=ey,slot=slot,obj_type=t}
                new_enemy_slots[#new_enemy_slots+1]={slot=slot,obj_type=t}
                new_enemy_type_baseline[slot]=t
            end
        end
    end
    state.enemy_slots=new_enemy_slots
    state.enemy_type_baseline=new_enemy_type_baseline

    local new_item_slots={}; local new_item_lifetime_baseline={}; local new_room_item_state_baseline=-1
    -- Always capture room-item slot state baseline, even when the slot
    -- isn't currently active. Some screens (e.g. E3 Power Bracelet) keep
    -- room_item_id assigned but flip obj_state from 0xFF to 0x00 when
    -- the item becomes visible (after touching armos). Without this
    -- baseline capture, check_room_item_state_changed never fires when
    -- the item is revealed because the baseline stayed at -1 ("never
    -- captured"). Captured here unconditionally so the rebuild loop
    -- detects ALL obj_state[0x13] changes, not just collection events.
    new_room_item_state_baseline = mainmemory.read_u8(ADDR.obj_state_base + ROOM_ITEM_SLOT)
    for slot=ITEM_SLOT_FIRST,ITEM_SLOT_LAST do
        local ot=mainmemory.read_u8(ADDR.obj_type_base+slot)
        local os=mainmemory.read_u8(ADDR.obj_state_base+slot)
        local item_id=slot==ROOM_ITEM_SLOT and mainmemory.read_u8(ADDR.room_item_id) or os
        local lifetime=mainmemory.read_u8(ADDR.obj_pos_frac_base+slot)
        local is_room=slot==ROOM_ITEM_SLOT and item_id~=ROOM_ITEM_NONE and os<0x80
        local is_drop=ot==DROPPED_ITEM_TYPE and item_id~=0xFF and lifetime>0
        -- Already-collected suppression for unique inventory items.
        -- Two cases:
        --   1. Heart Container on F16 (room 0x5F): Z1 respawns the
        --      slot on every entry. We read the per-screen overworld
        --      flag at $067F + room_id bit 0x10 to know if it's
        --      already collected. If so, suppress.
        --   2. Power Bracelet on E3 (room 0x24): same respawn quirk.
        --      Either the per-screen flag (bit 0x10 at $06A3) or
        --      InvBracelet at $0665 confirms collection. Either is
        --      sufficient.
        if level == 0 and slot == ROOM_ITEM_SLOT and is_room then
            local screen_flag = mainmemory.read_u8(0x067F + room_id)
            if room_id == 0x5F and bit.band(screen_flag, 0x10) ~= 0 then
                is_room = false
            elseif room_id == 0x24 then
                if bit.band(screen_flag, 0x10) ~= 0
                        or mainmemory.read_u8(0x0665) ~= 0 then
                    is_room = false
                end
            elseif item_id == 0x14
                    and mainmemory.read_u8(0x0665) ~= 0 then
                -- Defensive: any other room reporting bracelet ID
                -- when the player owns it is a respawn artifact.
                is_room = false
            end
        end
        if is_room or is_drop then
            local ix,iy=pixel_to_tile(mainmemory.read_u8(ADDR.obj_x_base+slot),mainmemory.read_u8(ADDR.obj_y_base+slot))
            local label=is_drop and drop_item_name(item_id) or room_item_name(item_id)
            entities[#entities+1]={type_name="item",label=label,x=ix,y=iy,approach_x=ix,approach_y=iy,slot=slot,item_id=item_id}
            new_item_slots[#new_item_slots+1]={slot=slot}
            if slot~=ROOM_ITEM_SLOT then new_item_lifetime_baseline[slot]=lifetime end
        end
    end
    state.item_slots=new_item_slots
    state.item_lifetime_baseline=new_item_lifetime_baseline
    state.room_item_state_baseline=new_room_item_state_baseline

    if level == 0 and m and m.has_underground
            and not (m.ignore_q1 and quest==1)
            and not (m.ignore_q2 and quest==2)
            and not (world_map and world_map.is_unfindable
                    and world_map.is_unfindable(state.cur_room_key)) then
        -- Fallback block REMOVED.
        --
        -- Previously: if has_underground was true and no cave/stairs
        -- entity had been added by either the live tile scan or a
        -- baked entry, this block emitted a default Entrance from
        -- CAVE_ENTRANCE_TILES or hardcoded coords.
        --
        -- Why it was removed: the user's verified Q1 walkthrough
        -- chart is now the single source of truth for hidden
        -- entrances. The live two-slot scan handles all visible
        -- entrances and revealed (post-trigger) caves. There is no
        -- screen left where a fallback emit is correct -- if the
        -- ROM says has_underground=true but neither the chart nor
        -- the live scan produces an entity, the cave is either
        -- Q2-only, unfindable in normal play, or otherwise not
        -- meant to be navigable in the current state. Emitting a
        -- phantom Entrance at a default coordinate just confused
        -- the player.
        --
        -- The outer `if` is kept as a no-op for clarity in case
        -- we want to add per-quest logic here later, but the body
        -- is intentionally empty.
    end

    -- Capture the door face tile baseline so the poll loop can detect
    -- when a door changes state without us having to listen for game
    -- events. Only relevant in dungeons (overhead view), not basements.
    if level > 0 and not state.cur_in_basement then
        local baseline = {}
        for _, door in ipairs(DUNGEON_DOORS) do
            local offset = door.face_x * GRID.height + door.face_y
            local ok, v = pcall(function()
                return memory.read_u8(RAM.play_area_tiles + offset, RAM.domain)
            end)
            baseline[door.direction] = (ok and v) or 0
        end
        state.door_tile_baseline = baseline
    else
        state.door_tile_baseline = nil
    end

    state.entities=entities
    state.entity_count=#entities
end

-- ============================================================
-- STATE CHANGE DETECTORS
-- ============================================================

local function check_enemy_state_changed()
    for _, es in ipairs(state.enemy_slots) do
        local cur=mainmemory.read_u8(ADDR.obj_type_base+es.slot)
        if cur~=es.obj_type then return true end
    end
    for slot=ENEMY_SLOT_FIRST,ENEMY_SLOT_LAST do
        local t=mainmemory.read_u8(ADDR.obj_type_base+slot)
        if t~=0 and t<MONSTER_PROJECTILE_MIN then
            local is_fire=t>=FIRE_TYPE_MIN and t<=FIRE_TYPE_MAX
            local is_npc=(t>=DUNGEON_NPC_MIN and t<=DUNGEON_NPC_MAX) or (t>=CAVE_NPC_MIN and t<=CAVE_NPC_MAX)
            local is_push_block = (t == PUSH_BLOCK_OBJ_TYPE)
            if not is_fire and not is_npc and not is_push_block then
                local found=false
                for _, es in ipairs(state.enemy_slots) do
                    if es.slot==slot then found=true; break end
                end
                if not found then return true end
            end
        end
    end
    return false
end

local function update_enemy_positions()
    for _, e in ipairs(state.entities) do
        if e.type_name=="enemy" and e.slot then
            local ex=mainmemory.read_u8(ADDR.obj_x_base+e.slot)
            local ey=mainmemory.read_u8(ADDR.obj_y_base+e.slot)
            local etx,ety=pixel_to_tile(ex,ey)
            e.x=etx; e.y=ety; e.approach_x=etx; e.approach_y=ety
        end
    end
end

local function check_item_state_changed()
    for _, is in ipairs(state.item_slots) do
        if is.slot~=ROOM_ITEM_SLOT then
            local cur_type=mainmemory.read_u8(ADDR.obj_type_base+is.slot)
            if cur_type~=DROPPED_ITEM_TYPE then return true end
        end
    end
    return false
end

local function check_room_item_state_changed()
    if state.room_item_state_baseline<0 then return false end
    local cur=mainmemory.read_u8(ADDR.obj_state_base+ROOM_ITEM_SLOT)
    return cur~=state.room_item_state_baseline
end

-- Returns true if the current overworld screen is on a reveal screen
-- (in OVERWORLD_REVEAL_ROOMS) and a live entrance/stair tile exists
-- in the play-area grid that the entity list doesn't reflect yet.
-- This catches the moment a reveal mechanic fires (recorder, bomb,
-- burn, armos) without any enemy/item state change.
--
-- Two reveal kinds, both watched:
--   0x70 (stair cluster) -- bare staircase, e.g. lake drain on E3
--   0xF3 (cave mouth)    -- archway, looks like a normal entrance,
--                            e.g. most bomb walls and burn trees
--
-- Logic: scan the grid. If we find a reveal tile, check whether the
-- current entity list already has a cave/stairs entity at that exact
-- position. If yes, the rebuild already happened -- short-circuit.
-- If no, the entity list is stale (still has the baked Event at a
-- different position) -- return true so the poll loop schedules a
-- rebuild.
local function check_overworld_stair_revealed()
    if state.cur_level ~= 0 then return false end
    if not OVERWORLD_REVEAL_ROOMS[state.cur_room_id] then return false end
    -- Already handled this reveal? short-circuit. Set by build_entity_list
    -- when it emits a live cave/stairs entity from the tile scan, cleared
    -- on room entry.
    if state.overworld_reveal_handled then return false end
    -- Scan for a reveal tile. Bail on first hit.
    for sx = 0, GRID.width - 1 do
        for sy = 0, GRID.height - 1 do
            local offset = sx * GRID.height + sy
            local ok, tile = pcall(function()
                return memory.read_u8(RAM.play_area_tiles + offset, RAM.domain)
            end)
            if ok and (tile == OVERWORLD_STAIR_TILE
                    or tile == OVERWORLD_CAVE_MOUTH) then
                return true
            end
        end
    end
    return false
end

-- Compare the four door face tiles against the baseline captured when the
-- entity list was last built. Returns true if any door changed -- meaning
-- a key was used, a bomb went off, a shutter opened, etc. Four cheap byte
-- reads on the System Bus, no allocations.
local function check_door_state_changed()
    local baseline = state.door_tile_baseline
    if not baseline then return false end
    for _, door in ipairs(DUNGEON_DOORS) do
        local offset = door.face_x * GRID.height + door.face_y
        local ok, cur = pcall(function()
            return memory.read_u8(RAM.play_area_tiles + offset, RAM.domain)
        end)
        if ok and cur ~= baseline[door.direction] then
            return true
        end
    end
    return false
end

-- ============================================================
-- ROOM ENTRY
-- ============================================================

local FILE_SCREEN_MODES = {
    [0x00]=true, [0x01]=true, [0x0E]=true, [0x0F]=true,
}

local function on_room_entry(ctx)
    local level    = mainmemory.read_u8(ADDR.cur_level)
    local game_mode= mainmemory.read_u8(ADDR.game_mode)
    local room_id  = mainmemory.read_u8(ADDR.room_id)
    local in_cave  = (game_mode==0x0B or game_mode==0x0C)
    local in_basement = game_mode==0x09

    if FILE_SCREEN_MODES[game_mode] then
        state.cur_room_id=room_id; state.cur_game_mode=game_mode; return
    end

    -- Dungeon basement (side-scroll cellar): GameMode 0x09. Like an
    -- overhead dungeon room it uses the live tile grid at 0x6530, the
    -- standard slot scans for the dungeon item and enemies, and the
    -- border-gap scan to find the stairs-back exit. Skips the door
    -- detection entirely (DUNGEON_DOORS positions don't apply -- the
    -- basement is a different room shape).
    if in_basement then
        state.cur_level=level; state.cur_room_id=room_id; state.cur_game_mode=game_mode
        state.cur_in_cave=false
        state.cur_in_basement=true
        state.cur_room_key=string.format("%02X:B%02X", level, room_id)
        state.cur_room_entry=nil
        state.selected_entity_index=1; state.enemy_slots={}; state.item_slots={}
        state.enemy_type_baseline={}; state.item_lifetime_baseline={}
        state.room_item_state_baseline=-1
        state.last_check_frame=-9999; state.deferred_rebuild_frame=-1
        state.speech_queue={}; state.speech_queue_next_frame=-1
        state.last_enemy_radar_frame=-9999; state.last_item_radar_frame=-9999
        state.entities={}; state.entity_count=0
        state.deferred_rebuild_frame = state.frame_counter + 60
        if state.cur_room_key ~= state.live_grid_room_key then
            state.live_grid_cache=nil; state.live_grid_room_key=state.cur_room_key
            state.live_grid_build_frame=state.frame_counter+90
        end
        if not state.basement_announced then
            state.basement_announced = true
            -- Logic lives in Dungeons.lua (see BASEMENTS section).
            ctx.write_speech(dungeons.basement_announcement())
        end
        -- Reset push-block / dialog state (basement has none of these)
        state.prev_block_push_complete = -1
        state.push_block_initial_x = nil
        state.push_block_initial_y = nil
        state.dungeon_dialog_pending = nil
        state.dungeon_dialog_frame = -1
        return
    end

    -- Returning from basement: clear the basement_announced flag so the
    -- next basement entry announces fresh.
    if not in_basement and state.cur_in_basement then
        state.cur_in_basement = false
        state.basement_announced = false
    end

    -- Dungeon rooms: minimal state update + cache labels + announce room coords.
    -- The transition frame stays light: one direct-RAM read, two table indexes,
    -- write_speech, return. Entity building is deferred to a later frame so it
    -- never lands on the transition path.
    if level > 0 and not in_cave then
        state.cur_level=level; state.cur_room_id=room_id; state.cur_game_mode=game_mode
        state.cur_in_cave=false
        state.cur_room_key=room_key_str(level, room_id)
        state.cur_room_entry=nil
        state.selected_entity_index=1; state.enemy_slots={}; state.item_slots={}
        state.enemy_type_baseline={}; state.item_lifetime_baseline={}
        state.room_item_state_baseline=-1
        state.last_check_frame=-9999; state.deferred_rebuild_frame=-1
        state.speech_queue={}; state.speech_queue_next_frame=-1
        state.last_enemy_radar_frame=-9999; state.last_item_radar_frame=-9999
        state.entities={}; state.entity_count=0
        -- Defer entity build by 60 frames so slots have time to populate after
        -- the transition. Same settle pattern the cave system uses.
        state.deferred_rebuild_frame = state.frame_counter + 60
        -- Invalidate the live tile grid so dungeon walls/floors get read fresh.
        if state.cur_room_key ~= state.live_grid_room_key then
            state.live_grid_cache=nil; state.live_grid_room_key=state.cur_room_key
            state.live_grid_build_frame=state.frame_counter+90
        end
        -- Build the speech on demand using the player's chosen
        -- dungeon coordinate format. Replaces the old prebaked-table
        -- lookup (DUNGEON_ROOM_LABELS / DUNGEON_LOCATION_LABELS) so
        -- the dungeon_coordinate_format setting in accessibility
        -- settings is actually respected.
        local raw_room_id = memory.read_u8(0x00EB, "RAM")
        local fmt = dungeon_fmt(ctx)
        -- Speech on transition depends on map ownership.
        -- With map: speak the full "Room col, row." label.
        -- Without map: speak only the visited state ("New room" /
        -- "Visited room") -- gives the player feedback that they
        -- moved without revealing positional info they shouldn't
        -- have without the map.
        --
        -- Visited check is just our cache. The cache is kept in sync
        -- with the game's bit 0x20 by visited_rooms.reconcile(),
        -- which fires on save-state load via event.onloadstate. So
        -- by the time we read the cache here, it reflects the
        -- player's actual exploration history for this save.
        --
        -- We can't AND with the live bit 0x20 because mode 4 (the
        -- room-scroll) sets the bit BEFORE our handler runs. The
        -- bit is therefore always 1 when we look at it, even on a
        -- fresh first entry. Reconciliation handles the disagreement
        -- case at save-state-load time instead.
        local cur_save_slot = mainmemory.read_u8(ADDR.cur_save_slot)
        local cur_quest     = mainmemory.read_u8(ADDR.quest_numbers + cur_save_slot)
        local seen_key = visited_rooms.key(
            cur_save_slot, cur_quest, level, raw_room_id)
        local was_seen = visited_rooms.has(seen_key)
        -- Add to cache so the next visit reads as visited. add() is
        -- a no-op when the entry already exists.
        visited_rooms.add(seen_key)
        -- Always lead with the visited-state announcement so the
        -- player gets immediate feedback about whether this is
        -- somewhere they've been before. With the map, append the
        -- room coords so they also know WHERE on the dungeon grid
        -- they are. Without the map, the visited-state alone is
        -- the announcement -- coords would be revealing info the
        -- player shouldn't have without owning the map.
        local visited_phrase = was_seen and "Visited room." or "New room."
        if dungeon_has_map(level) then
            ctx.write_speech(string.format("%s %s",
                visited_phrase,
                dungeons.room_label(raw_room_id, fmt)))
        else
            ctx.write_speech(visited_phrase)
        end

        -- Reset the BlockPushComplete edge detector for the new room.
        -- -1 means "no baseline yet" -- the next poll will read the
        -- current value as the baseline so we don't false-trigger on
        -- entering a room where the block was already pushed last time.
        state.prev_block_push_complete = -1
        -- Reset the push-block position baseline. Will be captured the
        -- first time the block is seen in slot 0x0B in this room.
        state.push_block_initial_x = nil
        state.push_block_initial_y = nil

        -- Schedule a dungeon-dialog check 60 frames out. At that point the
        -- NPC slots have settled, so we can detect whether an Old Man is
        -- in the room and speak the room's hint if so. Mirrors the cave
        -- dialog pattern. Lookup happens at fire time so the room_id and
        -- level are guaranteed current.
        state.dungeon_dialog_pending = true
        state.dungeon_dialog_frame   = state.frame_counter + 60

        -- Reset the PersonTextSelector edge watcher. New room means
        -- the previous selector value is irrelevant -- we want the
        -- next 0 -> non-zero transition in this room to fire fresh.
        state.prev_person_selector = mainmemory.read_u8(0x0415)

        -- Reset the dialog-box-active edge watcher. Capture the current
        -- combined state (idx>0 AND ptr>0) so the next 0 -> 1 transition
        -- in this room fires fresh. If the box is somehow up on entry
        -- (e.g. dialog already rendering during the transition), this
        -- captures it and the watcher correctly waits for it to drop
        -- to 0 first before re-firing.
        local entry_idx = mainmemory.read_u8(0x0416)
        local entry_ptr_lo = mainmemory.read_u8(0x045F)
        local entry_ptr_hi = mainmemory.read_u8(0x0460)
        state.prev_box_active = entry_idx ~= 0
            and (entry_ptr_lo ~= 0 or entry_ptr_hi ~= 0)

        -- Reset the cooldown so the first dialog in this room
        -- speaks even if a different room recently spoke the same
        -- selector value. "-9999" is the engine's standard
        -- never-fired sentinel.
        state.last_spoken_selector = 0
        state.last_spoken_selector_frame = -9999

        -- Reset the Digdogger boss state watcher. New room means
        -- whatever boss state we tracked in the previous room is
        -- irrelevant. nil means "haven't seen a Digdogger yet" --
        -- the watcher's first non-nil read in this room will set
        -- the baseline silently (no false-fire on entering a boss
        -- room with the boss already big).
        state.prev_digdogger_state = nil

        -- Reset all boss watchers in Dungeons.lua. Same baseline-
        -- on-first-read pattern: each watcher's update() seeds its
        -- prev field on its first scan in the new room so we don't
        -- false-fire when entering a boss room with the boss
        -- already in some non-zero state.
        if dungeons and dungeons.reset_boss_watchers then
            dungeons.reset_boss_watchers(state)
        end

        -- Reset the N-hotkey replay cache. Will be repopulated by the
        -- deferred dialog fire if this room turns out to have an Old
        -- Man with a hint.
        state.last_dialog_spoken = nil

        state.dungeon_rupee_announced = false
        state.dungeon_last_rupees_to_add = 0
        state.dungeon_last_rupees_to_sub = 0
        return
    end

    -- Overworld: lazy-load enemy placements + screen manifest on first entry
    if level == 0 and world_map and not world_map.is_overworld_data_loaded() then
        world_map.load_overworld_data(write_log, ENEMY_NAMES)
        -- Also load shop inventory now
        for loc_idx, addrs in pairs(SHOP_ROM) do
            local items={}
            for i=0,2 do
                local code=bit.band(rom_read(addrs.items_addr+i),0x3F)
                local price=rom_read(addrs.prices_addr+i)
                local label = ITEM_NAMES[code] or string.format("Item %02X",code)
                if code == 0x03 then label = "Arrows" end
                items[#items+1]={label=label,price=price}
            end
            SHOP_INVENTORY[loc_idx]={items=items}
        end
    end

    -- Returning to overworld -- clear cached prev_keys_count.
    -- (cur_dungeon_room_label / cur_dungeon_location_label no longer
    -- exist; the dungeon-room speech is built fresh from dungeons.
    -- room_label() each transition.)
    if level == 0 and not in_cave then
        state.prev_keys_count            = -1
    end

    local quest      = mainmemory.read_u8(0x0011)
    local key        = room_key_str(level, room_id)
    local room_entry = (world_map and world_map.get_room_entry(key))
                    or (state.room_data.rooms and state.room_data.rooms[key])
                    or nil

    state.cur_level=level; state.cur_game_mode=game_mode; state.cur_room_id=room_id
    state.cur_in_cave=in_cave; state.cur_quest=quest; state.cur_room_key=key
    state.cur_room_entry=room_entry

    state.selected_entity_index=1; state.selected_room_key=nil; state.locked_enemy_slot=nil
    state.enemy_slots={}; state.item_slots={}; state.enemy_type_baseline={}
    state.item_lifetime_baseline={}; state.room_item_state_baseline=-1
    state.cave_npc_slot_baseline=-1; state.last_check_frame=-9999
    state.deferred_rebuild_frame=-1; state.speech_queue={}; state.speech_queue_next_frame=-1

    -- Reset cave state on every room transition so entry announcement fires correctly
    state.cave_last_in_cave   = false
    state.cave_cached_label   = nil
    state.cave_cached_dialog  = nil
    state.cave_cached_loc_idx = 0
    state.cave_cached_quest   = 0
    state.cave_dialog_frame   = -1
    state.cave_sword_baseline = -1
    state.cave_shop_check_frame = 0
    state.cave_rupee_announced = false
    state.cave_last_rupees_to_add = 0
    state.cave_last_rupees_to_sub = 0
    state.cave_last_letter_state = mainmemory.read_u8(0x0666)
    state.cave_info_paid = false
    state.cave_rupee_collected = false
    state.cave_letter_collected = false
    state.cave_last_screen_flag_bit = -1
    state.cave_reward_pickup_handled = false
    state.cave_entry_frame = nil

    -- Reset the N-hotkey replay cache. Will be repopulated by the
    -- cave deferred dialog fire if this cave has an NPC line, or by
    -- the overworld code (which doesn't have dialogs at the moment
    -- but the field is still cleared for consistency).
    state.last_dialog_spoken = nil

    if key~=state.live_grid_room_key then
        state.live_grid_cache=nil; state.live_grid_room_key=key
        state.live_grid_build_frame=state.frame_counter+90
    end

    -- Reset the reveal-handled flag for the new screen. The polling
    -- helper relies on this flag to know when to fire a rebuild after
    -- a reveal mechanic exposes a new entrance tile. build_entity_list
    -- below will set it back to true if the screen already has a live
    -- cave/stairs tile (e.g. revisiting a previously-bombed screen).
    state.overworld_reveal_handled = false

    state.entities={}; state.entity_count=0
    if in_cave then
        build_entity_list()
    elseif level == 0 then
        build_entity_list()  -- always build immediately on overworld entry
    end

    if not in_cave then
        local fmt = ctx.settings and ctx.settings.coordinate_format or "numeric"
        ctx.write_speech(string.format("Screen %s.", screen_coord_str(room_id, fmt)))
    end
end

-- ============================================================
-- AUDIO HELPERS
-- ============================================================

local function drain_speech_queue(ctx)
    if #state.speech_queue==0 then return end
    local item=state.speech_queue[1]
    if state.frame_counter>=item.frame then
        ctx.write_speech(item.text)
        table.remove(state.speech_queue,1)
        state.speech_queue_next_frame=state.frame_counter+90
    end
end

local function emit_enemy_ping(ctx, entity)
    if not entity or entity.type_name~="enemy" then return end
    if not ctx.settings.enemy_radar_enabled then return end
    ctx.emit_sound_command("synth_enemy",pan_bucket(entity.x*GRID.tile_size),string.format("%.3f",enemy_pitch(entity.y)),ctx.settings.enemy_radar_volume)
end

local function emit_item_ping(ctx, entity)
    if not entity or entity.type_name~="item" then return end
    if not ctx.settings.item_beacon_enabled then return end
    ctx.emit_sound_command("synth_item",pan_bucket(entity.x*GRID.tile_size),entity.item_id or 0,ctx.settings.item_beacon_volume)
end

local function emit_entity_ping(ctx, entity)
    if state.cur_in_cave or not entity then return end
    if entity.type_name=="enemy" then emit_enemy_ping(ctx,entity)
    elseif entity.type_name=="item" then emit_item_ping(ctx,entity) end
end

local function nearest_enemy_entity()
    local best,best_d=nil,math.huge
    for _,e in ipairs(state.entities) do
        if e.type_name=="enemy" then
            local d=abs(e.x-state.link_tile_x)+abs(e.y-state.link_tile_y)
            if d<best_d then best_d=d; best=e end
        end
    end
    return best
end

local function nearest_item_entity()
    local best,best_d=nil,math.huge
    for _,e in ipairs(state.entities) do
        if e.type_name=="item" then
            local d=abs(e.x-state.link_tile_x)+abs(e.y-state.link_tile_y)
            if d<best_d then best_d=d; best=e end
        end
    end
    return best
end

local function update_enemy_radar(ctx)
    if state.cur_in_cave then return end
    -- E4 (Great Fairy screen): suppress radar. The fairy is a permanent
    -- fixture and pinging her every few seconds is just noise.
    if state.cur_room_id == 0x43 then return end
    if not ctx.settings.enemy_radar_enabled then return end
    if (state.frame_counter-state.last_enemy_radar_frame)<CONFIG.enemy_radar_interval_frames then return end
    local entity=nil
    if state.locked_enemy_slot then
        for _,e in ipairs(state.entities) do
            if e.type_name=="enemy" and e.slot==state.locked_enemy_slot then entity=e; break end
        end
    end
    if not entity then entity=nearest_enemy_entity() end
    if not entity then return end
    emit_enemy_ping(ctx,entity)
    state.last_enemy_radar_frame=state.frame_counter
end

local function update_item_radar(ctx)
    if state.cur_in_cave then return end
    -- E4 (Great Fairy screen): suppress item beacon for the same reason
    -- as the enemy radar above.
    if state.cur_room_id == 0x43 then return end
    if not ctx.settings.item_beacon_enabled then return end
    if (state.frame_counter-state.last_item_radar_frame)<CONFIG.item_radar_interval_frames then return end
    local sel=state.entities[state.selected_entity_index]
    local entity=(sel and sel.type_name=="item") and sel or nearest_item_entity()
    if not entity then return end
    emit_item_ping(ctx,entity)
    state.last_item_radar_frame=state.frame_counter
end

-- ============================================================
-- FOOTSTEPS AND BUMPS
-- ============================================================

local function update_footsteps(ctx)
    if not ctx.settings.footsteps_enabled then return end
    local moved=state.link_x~=state.prev_link_x or state.link_y~=state.prev_link_y
    if not moved then return end
    if state.frame_counter<=state.footstep_resume_block_until_frame then return end
    if (state.frame_counter-state.last_step_frame)>=CONFIG.synth_step_interval_frames then
        ctx.emit_sound_command("synth_step",pan_bucket(state.link_x),state.step_variant,
            string.format("%.3f",step_pitch(state.link_tile_y)),ctx.settings.footstep_volume)
        state.last_step_frame=state.frame_counter
        state.step_variant=(state.step_variant+1)%2
    end
end

local prev_input_dir = nil

local function get_link_input_dir()
    local d=mainmemory.read_u8(0x03F8)
    if d==1 then return "north" end
    if d==2 then return "south" end
    if d==4 then return "west"  end
    if d==8 then return "east"  end
    return nil
end

local function direction_just_pressed(ctx)
    local cur=get_link_input_dir()
    local just=(cur~=nil and cur~=prev_input_dir) and cur or nil
    prev_input_dir=cur
    return just
end

local function first_held_dir(keys) return get_link_input_dir() end
local function dir_still_held(keys, dir) return dir and get_link_input_dir()==dir end

-- Wall-bump detection. Watches a single counter -- stuck_frames --
-- that increments while Link is stationary AND pressing a direction.
-- When the counter crosses CONFIG.bump_confirm_frames the player has
-- been pushing into something solid for long enough to count as a
-- wall bump, and we fire the synth_bump sound.
--
-- After the initial fire, bump_lock_direction is set so the repeat
-- path can pulse every CONFIG.bump_repeat_frames as long as the same
-- direction stays held -- gives the player a steady wall-pulse
-- instead of going silent after one bump.
--
-- Counter resets when Link moves (no longer stuck) or releases the
-- direction (no longer pressing). The lock survives input-register
-- flickers but clears on actual movement, so walking away from a
-- wall and back into another one fires fresh instead of starting
-- mid-pulse-cycle.
local function update_bumps(ctx)
    if not ctx.settings.wall_bumps_enabled then
        state.stuck_frames = 0
        state.bump_lock_direction = nil
        return
    end
    if state.frame_counter <= state.room_transition_until_frame then
        state.stuck_frames = 0
        state.bump_lock_direction = nil
        return
    end

    local held = first_held_dir(ctx.keys)
    local moved = state.link_x ~= state.prev_link_x or state.link_y ~= state.prev_link_y

    -- Movement resets everything. Link is moving freely; whatever
    -- bump might have been brewing is no longer relevant.
    if moved then
        state.stuck_frames = 0
        state.bump_lock_direction = nil
        return
    end

    -- No direction held. Reset the counter but KEEP the lock --
    -- $03F8 can flicker to 0 between input polls when the player is
    -- holding steadily, and we don't want a brief gap to lose the
    -- repeat-pulse on continued pressing. Lock only clears on actual
    -- movement (handled above).
    if not held then
        state.stuck_frames = 0
        return
    end

    -- Stationary with direction held. Tick the counter.
    state.stuck_frames = (state.stuck_frames or 0) + 1

    -- Repeat path: already locked into this direction, pulse every
    -- bump_repeat_frames so continuous pressing pulses instead of
    -- going silent after one bump. Cooldown gating ensures we don't
    -- double-fire when the initial-bump path and repeat path overlap.
    if state.bump_lock_direction == held
            and (state.frame_counter - state.last_bump_frame) >= CONFIG.bump_repeat_frames then
        state.footstep_resume_block_until_frame = state.frame_counter
        ctx.emit_sound_command("synth_bump", pan_bucket(state.link_x), ctx.settings.wall_bump_volume)
        state.last_bump_frame = state.frame_counter
        return
    end

    -- Initial-bump path: counter has reached the confirm threshold,
    -- we're past the cooldown, and we haven't already locked on this
    -- direction. Fire the first bump and lock.
    if state.stuck_frames >= CONFIG.bump_confirm_frames
            and (state.frame_counter - state.last_bump_frame) >= CONFIG.bump_cooldown_frames
            and state.bump_lock_direction ~= held then
        state.footstep_resume_block_until_frame = state.frame_counter
        ctx.emit_sound_command("synth_bump", pan_bucket(state.link_x), ctx.settings.wall_bump_volume)
        state.last_bump_frame = state.frame_counter
        state.bump_lock_direction = held
    end
end

-- ============================================================
-- ENTITY CALLOUT
-- ============================================================

local function format_callout(entity, index, total)
    if not entity then return "No entities on this screen." end
    local tx=entity.approach_x or entity.x
    local ty=entity.approach_y or entity.y
    local steps=abs(tx-state.link_tile_x)+abs(ty-state.link_tile_y)
    local dir=rough_dir(state.link_tile_x,state.link_tile_y,tx,ty)
    if dir=="Here" then
        return string.format("%d, %s, here, %d of %d.",index,entity.label,index,total)
    end
    return string.format("%d, %s, %d steps %s, %d of %d.",index,entity.label,steps,dir,index,total)
end

-- ============================================================
-- STARTUP
-- ============================================================

local function startup(ctx)
    reset_log()
    write_log("Navigation engine loaded")

    local wm_chunk, wm_err = loadfile(WORLD_MAP_FILE)
    if wm_chunk then
        local ok, wm = pcall(wm_chunk)
        if ok and type(wm)=="table" then
            world_map = wm
            world_map.load(write_log)
            write_log("WorldMap loaded")
        else
            write_log("WorldMap execute failed: " .. tostring(wm))
        end
    else
        write_log("WorldMap load failed: " .. tostring(wm_err))
    end

    local cd_chunk, cd_err = loadfile(CAVE_DATA_FILE)
    if cd_chunk then
        local ok, cd = pcall(cd_chunk)
        if ok and type(cd)=="table" then
            cave_data = cd
            write_log(string.format("cave_data loaded, %d entries", (function() local n=0; for _ in pairs(cd) do n=n+1 end; return n end)()))
        else
            write_log("cave_data execute failed: " .. tostring(cd))
        end
    else
        write_log("cave_data load failed: " .. tostring(cd_err))
    end

    local dg_chunk, dg_err = loadfile(DUNGEONS_FILE)
    if dg_chunk then
        local ok, dg = pcall(dg_chunk)
        if ok and type(dg)=="table" then
            dungeons = dg
            write_log("Dungeons loaded")
            -- No prebaked label tables anymore -- room labels are built
            -- on demand from dungeons.room_label() / dungeons.
            -- location_label() so the dungeon_coordinate_format setting
            -- is honored at speech time.
        else
            write_log("Dungeons execute failed: " .. tostring(dg))
        end
    else
        write_log("Dungeons load failed: " .. tostring(dg_err))
    end

    local gd_chunk, gd_err = loadfile(GAME_DATA_FILE)
    if gd_chunk then
        local ok, gd = pcall(gd_chunk)
        if ok and type(gd)=="table" then
            game_data = gd
            ENEMY_NAMES     = gd.ENEMY_TYPE_NAMES or {}
            ITEM_NAMES      = gd.ITEM_NAMES or {}
            DOOR_TILE_TYPES = gd.DOOR_TILE_TYPES or {}
            write_log(string.format("GameData loaded, %d enemy names, %d item names",
                (function() local n=0; for _ in pairs(ENEMY_NAMES) do n=n+1 end; return n end)(),
                (function() local n=0; for _ in pairs(ITEM_NAMES)  do n=n+1 end; return n end)()))
        else
            write_log("GameData execute failed: " .. tostring(gd))
        end
    else
        write_log("GameData load failed: " .. tostring(gd_err))
    end

    local wp_chunk, wp_err = loadfile(WAYPOINTS_FILE)
    if wp_chunk then
        local ok, wp = pcall(wp_chunk)
        if ok and type(wp)=="table" then
            waypoints = wp
            waypoints.load(DATA_DIR)
            write_log(string.format("Waypoints loaded, %d existing markers",
                waypoints.count()))
        else
            write_log("Waypoints execute failed: " .. tostring(wp))
        end
    else
        write_log("Waypoints load failed: " .. tostring(wp_err))
    end

    -- VisitedRooms loads after waypoints, same pattern. Maps the
    -- player's exploration history to a JSON file so a Lua-console
    -- reset doesn't wipe "have I been here?" knowledge. Combined
    -- with the game's bit 0x20 at speech time so save-state loads
    -- correctly revert visited status.
    local vr_chunk, vr_err = loadfile(VISITED_ROOMS_FILE)
    if vr_chunk then
        local ok, vr = pcall(vr_chunk)
        if ok and type(vr) == "table" then
            visited_rooms = vr
            visited_rooms.load(DATA_DIR)
            write_log(string.format(
                "VisitedRooms loaded, %d existing entries",
                visited_rooms.count()))
            -- Hook save-state load: when the user loads a state, the
            -- game's per-room visited bits revert to whatever was in
            -- the snapshot. Our persistent cache keeps every room
            -- ever entered though, so we'd incorrectly read those
            -- rooms as visited after a load. Reconciliation walks
            -- all 256 rooms of the current dungeon and removes any
            -- cache entry the game now disagrees with.
            --
            -- Schedule the reconcile for a few frames out -- the
            -- onloadstate callback fires synchronously while the
            -- emulator is still re-applying the snapshot, and we
            -- want to read the post-load RAM, not the pre-load.
            local function schedule_reconcile()
                state.pending_reconcile_frame = (state.frame_counter or 0) + 5
            end
            -- BizHawk's event.onloadstate may not exist on all
            -- versions, so guard the bind. event.onsavestate is
            -- bound too, just for symmetry / future use.
            if event and event.onloadstate then
                event.onloadstate(schedule_reconcile)
                write_log("VisitedRooms: onloadstate hook bound")
            else
                write_log("VisitedRooms: event.onloadstate unavailable, save-state loads will leave stale cache entries")
            end
        else
            write_log("VisitedRooms execute failed: " .. tostring(vr))
        end
    else
        write_log("VisitedRooms load failed: " .. tostring(vr_err))
    end

    ctx.emit_sound_command("reset")
    state.started=true
end

-- ============================================================
-- PUBLIC API
-- ============================================================

function M.force_cache_current_room(ctx)
    ctx.write_speech("Exit cache removed. Navigation is always live.")
end

function M.is_exit_cached(room_key)
    return false  -- exit cache removed, always live
end

function M.get_selected_entity()
    return state.entities[state.selected_entity_index]
end

function M.get_location_idx(room_id)
    local m = world_map and world_map.get_screen_manifest(room_id)
    return m and m.location_idx or 0
end

function M.rebuild_cave_entities()
    if state.cur_in_cave then build_entity_list() end
end

function M.deactivate(ctx)
    if state.started then ctx.emit_sound_command("reset") end
    state.stuck_frames = 0
    state.bump_lock_direction = nil
    state.cur_room_id = -1
    state.live_grid_cache = nil
end

-- ============================================================
-- MAIN UPDATE LOOP
-- ============================================================

local function safe_update(ctx)
    if not state.started then startup(ctx) end

    state.frame_counter=state.frame_counter+1

    -- Make settings available to build_entity_list and other helpers
    -- that don't receive ctx directly. Updated every frame so any
    -- toggle in the accessibility menu takes effect immediately.
    state.settings = ctx.settings

    -- Dungeon stair descent hook: GameMode 0x10 fires the moment Link steps
    -- onto a dungeon stair tile, well before GameMode returns to 0x05.
    -- CurLevel is already set to the dungeon number at this point.
    local game_mode = mainmemory.read_u8(ADDR.game_mode)
    local cur_level  = mainmemory.read_u8(ADDR.cur_level)

    -- Track CurLevel changes to detect dungeon entry/exit and overworld return
    if state.prev_cur_level == nil then
        state.prev_cur_level = cur_level
    end

    if game_mode == 0x10 then
        -- Dungeon entry: GameMode 0x10 fires with CurLevel already set to dungeon number
        if cur_level > 0 and not state.dungeon_entry_announced then
            if not state.dungeon_entry_announce_frame then
                state.dungeon_entry_announce_frame = state.frame_counter + 30
            elseif state.frame_counter >= state.dungeon_entry_announce_frame then
                state.dungeon_entry_announced = true
                state.dungeon_entry_announce_frame = nil
                -- Cache the dungeon name in Dungeons.lua, then announce from here
                if dungeons then
                    local name = dungeons.get_dungeon_name(cur_level)
                    ctx.write_speech(string.format("Entering Dungeon %d, %s.", cur_level, name))
                end
            end
        end
    elseif state.prev_cur_level > 0 and cur_level == 0
            and not state.overworld_return_announced then
        -- CurLevel just dropped to 0 -- returned to overworld from dungeon
        -- Delay 30 frames so the transition animation has a moment to settle
        state.overworld_return_announced = true
        state.overworld_return_announce_frame = state.frame_counter + 30
    elseif game_mode == 0x0A and cur_level == 0
            and not state.overworld_return_announced then
        -- Cave exit: CurLevel stays 0 throughout cave, so watch GameMode 0x0A instead
        state.overworld_return_announced = true
        state.overworld_return_announce_frame = state.frame_counter + 30
    elseif game_mode == 0x05 and cur_level == 0 then
        -- Back on overworld in normal gameplay -- reset all transition flags
        state.dungeon_entry_announced      = false
        state.dungeon_entry_announce_frame = nil
        state.overworld_return_announced   = false
    end

    -- Fire deferred overworld return announcement
    if state.overworld_return_announce_frame
            and state.frame_counter >= state.overworld_return_announce_frame then
        state.overworld_return_announce_frame = nil
        ctx.write_speech("Entering Overworld.")
    end

    state.prev_cur_level = cur_level

    -- Kill all activity during room scroll transitions.
    -- Valid play modes: 0x05 (dungeon/overworld), 0x09 (dungeon basement
    -- side-scroll), 0x0B (regular cave), 0x0C (Any Road Cave -- the
    -- three-staircase warp cave runs in its own dedicated mode).
    -- Anything else is a transition or menu.
    if game_mode ~= 0x05 and game_mode ~= 0x0B and game_mode ~= 0x09
            and game_mode ~= 0x0C then
        -- Diagnostic: log unusual game modes that we're bailing on.
        -- Throttled so it only fires when the mode CHANGES, not every frame.
        -- This catches caves like Any Road that may use a non-standard mode.
        if state.last_bailed_mode ~= game_mode then
            state.last_bailed_mode = game_mode
            local f = io.open(DATA_DIR .. "/cave_entry_diag.log", "a")
            if f then
                f:write(string.format("[%s] BAILED on game_mode=%02X room=%02X frame=%d\n",
                    os.date("%H:%M:%S"), game_mode,
                    mainmemory.read_u8(ADDR.room_id), state.frame_counter))
                f:close()
            end
        end
        state.bump_watch = nil
        state.was_moving_last_frame = false
        state.footstep_resume_block_until_frame = state.frame_counter + 10
        return
    else
        state.last_bailed_mode = nil
    end

    local link_x = mainmemory.read_u8(ADDR.obj_x_base)
    local link_y = mainmemory.read_u8(ADDR.obj_y_base)
    state.prev_link_x = state.link_x; state.prev_link_y = state.link_y
    state.link_x = link_x; state.link_y = link_y
    state.link_tile_x, state.link_tile_y = pixel_to_tile(link_x, link_y)

    local in_cave=state.committed_in_cave

    if state.cur_room_id<0 or (state.frame_counter - state.last_room_id_check_frame) >= 3 then
        state.last_room_id_check_frame = state.frame_counter
        local room_id  =mainmemory.read_u8(ADDR.room_id)
        local game_mode=mainmemory.read_u8(ADDR.game_mode)
        local raw_in_cave=(game_mode==0x0B or game_mode==0x0C)

        local effective_cave_changed=false
        if raw_in_cave and not state.committed_in_cave then
            state.committed_in_cave=true; effective_cave_changed=true
        elseif not raw_in_cave and state.committed_in_cave then
            if not state.in_cave_candidate then
                state.in_cave_candidate=true; state.in_cave_candidate_since=state.frame_counter
            end
            if (state.frame_counter-state.in_cave_candidate_since)>=state.in_cave_debounce_frames then
                state.committed_in_cave=false; state.in_cave_candidate=false; effective_cave_changed=true
            end
        elseif raw_in_cave==state.committed_in_cave then
            state.in_cave_candidate=false
        end
        in_cave=state.committed_in_cave

        if room_id~=state.cur_room_id or effective_cave_changed then
            state.room_transition_until_frame=state.frame_counter+CONFIG.room_transition_grace_frames
            state.footstep_resume_block_until_frame=state.frame_counter
            state.bump_watch=nil
            state.last_enemy_radar_frame=-9999; state.last_item_radar_frame=-9999
            -- For dungeon rooms the transition block already announced the room.
            -- Still call on_room_entry to update state, but skip the speech.
            on_room_entry(ctx)
        end
    end

    -- ============================================================
    -- CAVE UPDATE
    -- Use raw game_mode not debounced in_cave, so this never fires on overworld
    -- ============================================================
    local truly_in_cave = (function()
        local m = mainmemory.read_u8(ADDR.game_mode)
        return m == 0x0B or m == 0x0C
    end)()
    if truly_in_cave then
        -- Reset on cave exit
        if not state.cave_last_in_cave then
            state.cave_last_in_cave  = true
            state.cave_rupee_announced = false
            state.cave_dialog_frame  = -1  -- will be set once slots are ready
            state.cave_cached_label  = "Cave"
            state.cave_cached_dialog = nil
            state.cave_cached_loc_idx = 0
            state.cave_cached_quest  = 0

            -- Diagnostic: dump cave entry state once per entry. Helps
            -- diagnose caves where nothing fires (e.g. Any Road Cave).
            local f = io.open(DATA_DIR .. "/cave_entry_diag.log", "a")
            if f then
                local raw_room = mainmemory.read_u8(0x00EB)
                local m = world_map and world_map.get_screen_manifest(raw_room)
                local live_loc = m and m.location_idx or 0
                local slot_str = ""
                for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
                    local t = mainmemory.read_u8(ADDR.obj_type_base + slot)
                    if t ~= 0 then
                        slot_str = slot_str .. string.format(" s%d=%02X", slot, t)
                    end
                end
                f:write(string.format("[%s] CAVE ENTRY frame=%d 00EB=%02X loc=%d mode=%02X slots=%s\n",
                    os.date("%H:%M:%S"), state.frame_counter, raw_room, live_loc,
                    mainmemory.read_u8(ADDR.game_mode),
                    slot_str ~= "" and slot_str or "none"))
                f:close()
            end
        end

        -- Wait for NPC slots to populate before resolving cave identity.
        -- Slots are ready when at least one object slot is nonzero OR 60 frames have passed.
        if state.cave_cached_loc_idx == 0 and state.cave_dialog_frame < 0 then
            local slots_ready = false
            for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
                local t = mainmemory.read_u8(ADDR.obj_type_base + slot)
                if t >= CAVE_NPC_MIN and t <= CAVE_NPC_MAX then
                    slots_ready = true; break
                end
            end
            if not slots_ready then
                -- Safety fallback: resolve after 60 frames regardless
                if not state.cave_entry_frame then
                    state.cave_entry_frame = state.frame_counter
                end
                if (state.frame_counter - state.cave_entry_frame) >= 60 then
                    slots_ready = true
                end
            end
            if slots_ready then
                state.cave_dialog_frame = state.frame_counter
            end
        end

        -- Deferred entry resolution
        if state.cave_cached_loc_idx == 0 and state.cave_dialog_frame >= 0
                and state.frame_counter >= state.cave_dialog_frame then
            local origin_room = mainmemory.read_u8(0x00EB)
            local loc_idx = (function()
                local m = world_map and world_map.get_screen_manifest(origin_room)
                return m and m.location_idx or 0
            end)()
            local room_override = CAVE_ROOM_OVERRIDES[origin_room]
            local save_slot = mainmemory.read_u8(ADDR.cur_save_slot)
            local quest     = mainmemory.read_u8(ADDR.quest_numbers + save_slot)
            local cave      = lookup_cave(loc_idx, quest)
            state.cave_cached_label   = (room_override and room_override.label)
                                     or (cave and cave.label) or "Cave"
            -- Suppress dialog if cave requires letter and it hasn't been
            -- shown yet. Two triggers: an explicit room override with
            -- requires_letter=true (e.g. A14), or any loc 26 cave (the
            -- medicine-woman potion shops) which is gated as a class.
            local letter_shown = mainmemory.read_u8(0x0666) >= 2
            local needs_letter = (room_override and room_override.requires_letter)
                              or (loc_idx == 26 and not room_override)
            if needs_letter and not letter_shown then
                state.cave_cached_dialog = nil
            else
                state.cave_cached_dialog = (room_override and room_override.dialog)
                                         or (cave and cave.dialog) or nil
            end
            state.cave_cached_loc_idx = loc_idx ~= 0 and loc_idx or -1  -- -1 means resolved but unknown
            state.cave_cached_quest   = quest
            state.cave_dialog_frame   = -1
            if (SWORD_CAVE_LOCS[loc_idx] and not room_override)
                    or (room_override and room_override.cave_type == "sword") then
                state.cave_sword_baseline = mainmemory.read_u8(ADDR.sword_level)
            end
            ctx.write_speech("Entering " .. state.cave_cached_label .. ".")
            if state.cave_cached_dialog then
                state.cave_dialog_frame = state.frame_counter + 60
            end
            -- Rebuild entity list now that NPC slots have settled
            build_entity_list()
            if state.selected_entity_index > state.entity_count then
                state.selected_entity_index = 1
            end
        end

        -- Fire deferred NPC dialog
        if state.cave_cached_dialog and state.cave_cached_loc_idx ~= 0
                and state.cave_dialog_frame > 0
                and state.frame_counter >= state.cave_dialog_frame then
            state.cave_dialog_frame = -1
            if cave_npc_present() then
                ctx.write_speech(state.cave_cached_dialog)
                -- Cache for the N hotkey replay. We only set this
                -- when the dialog actually fires (NPC present), so
                -- N stays silent in caves where the line was
                -- gated out (e.g. shops without the letter shown).
                state.last_dialog_spoken = state.cave_cached_dialog
            end
            state.cave_cached_dialog = nil
        end

        -- Shop purchase detection
        if SHOP_LOCS[state.cave_cached_loc_idx]
                and state.frame_counter >= state.cave_shop_check_frame then
            state.cave_shop_check_frame = state.frame_counter + 6
            if mainmemory.read_u8(ADDR.obj_type_base + 1) == 0 then
                build_entity_list()
                if state.selected_entity_index > state.entity_count then
                    state.selected_entity_index = 1
                end
            end
        end

        -- Rupee gain/loss detection: announce on the frame the value first becomes non-zero
        local to_add = mainmemory.read_u8(ADDR.rupees_to_add)
        local to_sub = mainmemory.read_u8(ADDR.rupees_to_sub)
        if not state.cave_rupee_announced then
            if to_add > 0 and state.cave_last_rupees_to_add == 0 then
                state.cave_rupee_announced = true
                ctx.write_speech(to_add == 1 and "Gained 1 Rupee."
                    or string.format("Gained %d Rupees.", to_add))
                -- Secret Rupee Cave: hide rupee entity immediately after collection
                if SECRET_RUPEE_CAVE_LOCS[state.cave_cached_loc_idx] then
                    state.cave_rupee_collected = true
                    build_entity_list()
                    if state.selected_entity_index > state.entity_count then
                        state.selected_entity_index = 1
                    end
                end
            elseif to_sub > 0 and state.cave_last_rupees_to_sub == 0 then
                state.cave_rupee_announced = true
                ctx.write_speech(to_sub == 1 and "Lost 1 Rupee."
                    or string.format("Lost %d Rupees.", to_sub))
                -- Information Cave: fire hint dialog 30 frames after rupee loss
                if state.cave_cached_loc_idx == 27 or state.cave_cached_loc_idx == 28 then
                    local hint
                    if state.cave_cached_loc_idx == 27 then
                        if to_sub <= 10 then
                            hint = "This ain't enough to talk."
                        else
                            hint = "Go up, up, the mountain ahead."
                        end
                    else
                        if to_sub <= 10 then
                            hint = "This ain't enough to talk."
                        elseif to_sub <= 30 then
                            hint = "Go north, west, south, west to the forest of maze."
                        else
                            hint = "Boy, you're rich!"
                        end
                    end
                    state.cave_cached_dialog = hint
                    state.cave_dialog_frame = state.frame_counter + 30
                    -- Hide payment options immediately after paying
                    state.cave_info_paid = true
                    build_entity_list()
                    if state.selected_entity_index > state.entity_count then
                        state.selected_entity_index = 1
                    end
                end
            end
        end
        state.cave_last_rupees_to_add = to_add
        state.cave_last_rupees_to_sub = to_sub

        -- Letter state change detection: rebuild entities and fire dialog
        -- when 0x0666 transitions to >= 2 (letter shown to old woman)
        local cur_letter = mainmemory.read_u8(0x0666)
        if cur_letter >= 2 and state.cave_last_letter_state < 2 then
            build_entity_list()
            if state.selected_entity_index > state.entity_count then
                state.selected_entity_index = 1
            end
            -- Fire the dialog now that the letter has been shown. Two
            -- sources: the room override (e.g. A14's "Buy medicine before
            -- you go") or the loc 26 cave_data dialog for any other
            -- medicine-woman shop the player walks into mid-show-letter.
            local ro = CAVE_ROOM_OVERRIDES[state.cur_room_id]
            if ro and ro.requires_letter and ro.dialog then
                state.cave_cached_dialog = ro.dialog
                state.cave_dialog_frame = state.frame_counter + 60
            elseif state.cave_cached_loc_idx == 26 and not ro then
                local cave = lookup_cave(26, state.cave_cached_quest)
                if cave and cave.dialog then
                    state.cave_cached_dialog = cave.dialog
                    state.cave_dialog_frame = state.frame_counter + 60
                end
            end
        end
        state.cave_last_letter_state = cur_letter

        if state.cave_cached_loc_idx == 17
                and not state.cave_reward_pickup_handled then
            local cur_screen_flag = mainmemory.read_u8(0x067F + state.cur_room_id)
            local cur_collected_bit = bit.band(cur_screen_flag, 0x10)
            if state.cave_last_screen_flag_bit < 0 then
                state.cave_last_screen_flag_bit = cur_collected_bit
            elseif state.cave_last_screen_flag_bit == 0 and cur_collected_bit ~= 0 then
                state.cave_reward_pickup_handled = true
                build_entity_list()
                if state.selected_entity_index > state.entity_count then
                    state.selected_entity_index = 1
                end
            end
            state.cave_last_screen_flag_bit = cur_collected_bit
        end

        -- Letter item pickup detection (loc_idx 24)
        -- InvLetter ($0666): 0=none, 1=have it, 2=shown to old woman
        if state.cave_cached_loc_idx == 24 and not state.cave_letter_collected then
            local has_letter = mainmemory.read_u8(0x0666)
            if has_letter >= 1 then
                state.cave_letter_collected = true
                build_entity_list()
                if state.selected_entity_index > state.entity_count then
                    state.selected_entity_index = 1
                end
            end
        end

        -- Sword pickup detection
        if (SWORD_CAVE_LOCS[state.cave_cached_loc_idx]
                or (CAVE_ROOM_OVERRIDES[state.cur_room_id] and CAVE_ROOM_OVERRIDES[state.cur_room_id].cave_type == "sword"))
                and state.cave_sword_baseline >= 0 then
            local cur = mainmemory.read_u8(ADDR.sword_level)
            if cur ~= state.cave_sword_baseline then
                state.cave_sword_baseline = cur
                build_entity_list()
                if state.selected_entity_index > state.entity_count then
                    state.selected_entity_index = 1
                end
            end
        end

        -- Insert hotkey: speak cave name
        if ctx.just_pressed(CONFIG.map_name_hotkey) then
            ctx.write_speech((state.cave_cached_label or "Cave") .. ".")
        end

        -- N hotkey in caves: removed. Used to be a developer debug
        -- dump (slot types, shop ROM block, loc_idx map written to
        -- cave_debug.log). The cave system is mature enough now that
        -- the dump no longer earns its keep, so N is free for other
        -- uses (or remains a no-op in caves, matching the dungeon
        -- behavior).
    else
        -- nothing to do on overworld for cave state
    end
    -- END CAVE UPDATE

    -- Fire deferred entity rebuilds for overworld, cave, and dungeon
    if state.deferred_rebuild_frame>0 and state.frame_counter>=state.deferred_rebuild_frame then
        state.deferred_rebuild_frame=-1
        build_entity_list()
        if state.selected_entity_index>state.entity_count then state.selected_entity_index=1 end
    end

    -- Fire deferred visited-rooms reconciliation. Scheduled by the
    -- event.onloadstate callback when the user loads a save state.
    -- The callback can't read RAM directly because the emulator is
    -- still mid-restore, so it sets pending_reconcile_frame and we
    -- consume it here once the loaded state has settled.
    --
    -- The reconciler walks all 256 rooms of the CURRENT dungeon and
    -- removes any cache entry where the game's bit 0x20 is now 0 --
    -- meaning the loaded state has the player NOT having visited
    -- there. Result: the cache matches the loaded save's actual
    -- exploration history.
    --
    -- Caveat: the room Link is currently standing in will have its
    -- bit 0x20 set by mode 4 (the room-scroll) before the reconciler
    -- runs, so that single room may incorrectly stay "visited" if
    -- you load a state from before you'd visited it. All OTHER
    -- rooms reconcile correctly. Acceptable: the player will hear
    -- "new room" the next time they cross any other room boundary.
    if state.pending_reconcile_frame
            and state.frame_counter >= state.pending_reconcile_frame
            and visited_rooms then
        state.pending_reconcile_frame = nil
        local level = mainmemory.read_u8(ADDR.cur_level)
        if level > 0 then
            local save_slot = mainmemory.read_u8(ADDR.cur_save_slot)
            local quest     = mainmemory.read_u8(ADDR.quest_numbers + save_slot)
            local base = (level <= 6) and ADDR.dungeon_screen_state_lo
                                       or  ADDR.dungeon_screen_state_hi
            local function bit_for(room_id)
                local b = mainmemory.read_u8(base + room_id)
                return bit.band(b, 0x20) ~= 0
            end
            local removed = visited_rooms.reconcile(
                save_slot, quest, level, bit_for)
            write_log(string.format(
                "VisitedRooms reconcile: removed %d entries from L%d (slot=%d quest=%d)",
                removed, level, save_slot, quest))
        end
    end


    -- Fire deferred dungeon NPC dialog. REMOVED.
    --
    -- Originally this fired 60 frames after dungeon room entry,
    -- looking up the dialog by current $0415 selector and speaking
    -- it if a dungeon NPC was present. Worked, but doubled up with
    -- the PersonTextSelector edge watcher below: when Link entered
    -- a dungeon room and walked toward an NPC, the selector would
    -- transition 0 -> non-zero during the 60-frame window. The
    -- edge watcher fired correctly on that transition, then 60
    -- frames after entry the deferred check would fire AGAIN with
    -- the same selector value, speaking the same line twice.
    --
    -- The edge watcher handles every case the deferred check did,
    -- including "Link entered already at the NPC" (selector is
    -- already non-zero on entry; prev_person_selector is reset to
    -- the current value so the edge watcher won't false-fire, but
    -- the next time the player triggers a dialog box -- e.g. by
    -- walking through and back -- it'll catch the transition).
    -- Keeping just the edge watcher gives one announcement per
    -- dialog activation, no doubles.
    --
    -- The dungeon_dialog_pending / dungeon_dialog_frame state
    -- fields and on_room_entry's scheduling are kept in place
    -- but no longer drive any speech path -- they're inert. We
    -- can clean them up later if it ever feels like clutter.

    -- PersonTextSelector edge watcher. Catches the moment $0415
    -- becomes non-zero -- which IS the moment the dialog box
    -- appears on screen -- and speaks the matching string
    -- immediately. Fires on the 0 -> non-zero edge only, so it
    -- doesn't repeat while the dialog is up.
    --
    -- This is the SOLE dialog firing path for dungeon NPCs.
    -- (Previously there was also a 60-frame deferred check on
    -- room entry, but it doubled up with this watcher when the
    -- selector transitioned during the 60-frame window. Removed.)
    --
    -- Works for caves AND dungeons since the same selector mechanism
    -- is used for both. We only fire the lookup when in a dungeon
    -- (level > 0) right now because the cave system has its own
    -- dialog firing path; once the cave dialogs migrate to the
    -- selector-based table too, the level gate can drop.
    if state.cur_level > 0 then
        local cur_selector = mainmemory.read_u8(0x0415)
        if cur_selector ~= 0 and state.prev_person_selector == 0 then
            local has_npc = dungeon_npc_present()
            local hit = nil
            if dungeons and dungeons.lookup_dialog_by_selector then
                hit = dungeons.lookup_dialog_by_selector(cur_selector)
            end
            local hit_present = hit ~= nil
            local cooldown_active = (cur_selector == state.last_spoken_selector)
                and (state.frame_counter - state.last_spoken_selector_frame) < 180
            local would_speak = has_npc and hit_present and not cooldown_active
            local f = io.open(DATA_DIR .. "/dialog_edge_diag.log", "a")
            if f then
                f:write(string.format(
                    "[%s] frame=%d level=%d room=0x%02X selector=0x%02X has_npc=%s cooldown_active=%s hit_present=%s would_speak=%s last_spoken=0x%02X (frame=%d)\n",
                    os.date("%H:%M:%S"), state.frame_counter,
                    state.cur_level, state.cur_room_id, cur_selector,
                    tostring(has_npc), tostring(cooldown_active), tostring(hit_present), tostring(would_speak),
                    state.last_spoken_selector, state.last_spoken_selector_frame))
                f:close()
            end
            if has_npc and hit_present and not cooldown_active then
                ctx.write_speech(hit)
                state.last_dialog_spoken = hit
                state.last_spoken_selector = cur_selector
                state.last_spoken_selector_frame = state.frame_counter
                if dungeons and dungeons.lookup_transaction_by_selector
                        and dungeons.lookup_transaction_by_selector(cur_selector) then
                    build_entity_list()
                    if state.selected_entity_index > state.entity_count then
                        state.selected_entity_index = 1
                    end
                end
            elseif has_npc and not hit_present and not cooldown_active then
                local key = string.format("%d:%02X:%02X",
                    state.cur_level, state.cur_room_id, cur_selector)
                state.unknown_selectors = state.unknown_selectors or {}
                if not state.unknown_selectors[key] then
                    state.unknown_selectors[key] = true
                    write_log(string.format(
                        "UNKNOWN PERSON SELECTOR 0x%02X in level=%d room=0x%02X -- please check Data/PersonTexts.txt and report",
                        cur_selector, state.cur_level, state.cur_room_id))
                end
            end
        end
        state.prev_person_selector = cur_selector
    end

    if state.cur_level > 0 and not state.cur_in_basement
            and dungeons and dungeons.lookup_transaction_by_selector then
        local sel = mainmemory.read_u8(0x0415)
        local txn_active = dungeons.lookup_transaction_by_selector(sel) ~= nil
        local to_add = mainmemory.read_u8(ADDR.rupees_to_add)
        local to_sub = mainmemory.read_u8(ADDR.rupees_to_sub)
        if txn_active and not state.dungeon_rupee_announced then
            if to_add > 0 and state.dungeon_last_rupees_to_add == 0 then
                state.dungeon_rupee_announced = true
                ctx.write_speech(to_add == 1 and "Gained 1 Rupee."
                    or string.format("Gained %d Rupees.", to_add))
            elseif to_sub > 0 and state.dungeon_last_rupees_to_sub == 0 then
                state.dungeon_rupee_announced = true
                ctx.write_speech(to_sub == 1 and "Lost 1 Rupee."
                    or string.format("Lost %d Rupees.", to_sub))
                build_entity_list()
                if state.selected_entity_index > state.entity_count then
                    state.selected_entity_index = 1
                end
            end
        end
        state.dungeon_last_rupees_to_add = to_add
        state.dungeon_last_rupees_to_sub = to_sub
    end

    -- Digdogger boss state watcher. Scans dungeon enemy slots for a
    -- Digdogger-family type (0x18 / 0x19 / 0x38 / 0x39) and reads
    -- the per-slot IsChild byte ($046B+slot) to classify the boss
    -- as either "big" (invulnerable, recorder needed) or "child"
    -- (small, sword-vulnerable). Announces:
    --   nil   -> big   = silent baseline (entered boss room)
    --   big   -> child = "Digdogger shrinks." -- the key cue
    --   any   -> nil   = silent (boss died, or left room)
    --
    -- Empirically verified: type 0x38+IsChild=0 is big, 0x18+
    -- IsChild=1 is the child after recorder. The 0x19 / 0x39 type
    -- IDs are reserved for Digdogger but haven't been seen in the
    -- captures so far -- treated the same way (any 0x18-style type
    -- with IsChild=1 means "small", any 0x38-style with IsChild=0
    -- means "big"). The check is robust to either pair.
    --
    -- Per Zelda Wiki and walkthroughs the shrink is permanent for
    -- the encounter -- Digdogger does not naturally reform on a
    -- timer. Snapshots that LOOKED like reformations during the
    -- probe runs were save-state loads, not in-game behavior. We
    -- intentionally do NOT announce child -> big transitions: the
    -- only way that happens in vanilla play is a save-state load,
    -- which the player initiated and doesn't need narration for.
    --
    -- Skipped on overworld and basements -- Digdogger only spawns
    -- in dungeon overhead rooms (L5 boss, L7 mini-boss + boss).
    -- Same Q1+Q2 mapping (Q2 has Digdoggers in different rooms
    -- but the type IDs are unchanged in ROM).
    if state.cur_level > 0 and not state.cur_in_basement then
        local cur_state = nil
        for slot = ENEMY_SLOT_FIRST, ENEMY_SLOT_LAST do
            local t = mainmemory.read_u8(ADDR.obj_type_base + slot)
            -- Digdogger family: 0x18, 0x19 (child variants),
            -- 0x38, 0x39 (big variants per ENEMY_TYPE_NAMES).
            if t == 0x18 or t == 0x19 or t == 0x38 or t == 0x39 then
                local is_child = mainmemory.read_u8(0x046B + slot)
                if is_child ~= 0 then
                    cur_state = "child"
                    break  -- child wins -- recorder is in effect
                else
                    cur_state = "big"
                    -- keep scanning in case a child also exists
                    -- alongside a big one (shouldn't happen, but
                    -- this prevents missing a child if the slot
                    -- order is unusual)
                end
            end
        end
        if cur_state ~= state.prev_digdogger_state then
            -- Only the big -> child transition speaks. Other
            -- transitions stay silent: room entry baseline
            -- (nil -> big), boss death (any -> nil), and
            -- save-state load reverts (child -> big).
            if state.prev_digdogger_state == "big" and cur_state == "child" then
                ctx.write_speech("The Digdogger shrinks.")
            end
            state.prev_digdogger_state = cur_state
        end
    end

    -- Boss watchers from Dungeons.lua. Currently includes Gohma's
    -- eye state (open/closed). Each watcher gates itself on its own
    -- boss being present in an enemy slot, so this is cheap when no
    -- relevant boss is in the current room. Scoped to dungeons --
    -- bosses don't spawn in caves, basements, or the overworld.
    if state.cur_level > 0 and not state.cur_in_basement
            and dungeons and dungeons.update_boss_watchers then
        dungeons.update_boss_watchers(state, ctx)
    end

    -- Push-block completion edge detection. Watch RAM 0x04CF for any
    -- transition AWAY FROM 0, which fires the moment Link finishes
    -- pushing the block. The byte advances 0 -> 1 -> 2 (1 = pushed,
    -- 2 = secret triggered) but the game may transition 0 -> 2 in a
    -- single frame if the secret fires immediately, so we don't gate
    -- on cur_push == 1 specifically -- any leave-from-0 counts.
    -- We don't speak anything here -- the game plays its own audio cue
    -- on a successful push, and the entity drops out of the cycle list
    -- on the next rebuild (because slot 0x0B's position will have moved
    -- away from the cached initial), which is signal enough.
    --
    -- The edge schedules a deferred entity rebuild and live-grid
    -- refresh so the "Push block" entity drops promptly and any newly-
    -- spawned stairs / opened shutters get picked up.
    if state.cur_level > 0 then
        local cur_push = mainmemory.read_u8(ADDR.block_push_complete)
        if state.prev_block_push_complete < 0 then
            state.prev_block_push_complete = cur_push
        elseif state.prev_block_push_complete == 0 and cur_push > 0 then
            -- Block just got pushed for the first time. Fires whether
            -- 0x04CF lands on 1 (pushed) or jumps straight to 2
            -- (secret already triggered). Schedule a rebuild so the
            -- push-block entity disappears and any newly-spawned
            -- stairs / opened shutters get picked up. Defer by 30
            -- frames to let the game's own animations settle.
            state.prev_block_push_complete = cur_push
            if state.deferred_rebuild_frame < 0 then
                state.deferred_rebuild_frame = state.frame_counter + 30
            end
            -- Also invalidate the live grid since shutters / stairs
            -- materialize as new walkable / solid tiles.
            state.live_grid_cache = nil
            state.live_grid_build_frame = state.frame_counter + 30
        else
            state.prev_block_push_complete = cur_push
        end
    end

    if not in_cave and (state.frame_counter-state.last_check_frame)>=state.check_interval then
        state.last_check_frame=state.frame_counter

        if state.live_grid_cache==nil and state.frame_counter>=state.live_grid_build_frame then
            get_tile_grid()
            if state.live_grid_cache~=nil then build_entity_list() end
        end

        if state.live_grid_cache~=nil then
            update_enemy_positions()
            local need_rebuild=false; local need_deferred=false
            if check_enemy_state_changed() then need_rebuild=true end
            if not need_rebuild and check_item_state_changed() then need_deferred=true end
            if not need_rebuild and check_room_item_state_changed() then need_deferred=true end
            -- Overworld-only: detect a newly-revealed stair tile on screens
            -- in OVERWORLD_REVEAL_ROOMS. Schedules a deferred rebuild so the
            -- "Stairs" entity appears in real time when the player plays the
            -- recorder, bombs a wall, burns a tree, etc.
            --
            -- 30-frame delay before the rebuild: the stair/cave-mouth
            -- tile flips first, but the surrounding tiles take longer to
            -- settle. Firing immediately would emit an entity that BFS
            -- reports as blocked because the approach tiles are still
            -- solid. 30 frames (~0.5s) is enough for the reveal animation
            -- to finish in practice -- this value worked for revealed
            -- stairs (E3 lake drain) without issue.
            --
            -- Self-throttled: once a stairs entity is in the list, the
            -- helper short-circuits and the scan doesn't run again. Also
            -- guarded by deferred_rebuild_frame so we don't keep stacking
            -- new defers while the first one is still pending.
            if not need_rebuild
                    and state.deferred_rebuild_frame < 0
                    and check_overworld_stair_revealed() then
                state.deferred_rebuild_frame = state.frame_counter + 30
                state.live_grid_cache = nil
                state.live_grid_build_frame = state.frame_counter + 30
            end
            -- Dungeon-only: detect door state changes and key usage.
            -- Two triggers, either fires a rebuild:
            --   1. Tile baseline diff -- catches bombed walls, shutters
            --      opening, or anything else that flips a door face byte.
            --   2. Keys-count drop -- catches the moment Link uses a key,
            --      which is when a locked-door tile flips. This is a
            --      cleaner discrete signal than tile polling.
            if not need_rebuild and state.cur_level > 0 then
                local door_changed = check_door_state_changed()
                local cur_keys = mainmemory.read_u8(ADDR.keys_count)
                local key_used = state.prev_keys_count >= 0
                                 and cur_keys < state.prev_keys_count
                state.prev_keys_count = cur_keys
                if door_changed or key_used then
                    state.live_grid_cache = nil
                    state.live_grid_build_frame = state.frame_counter
                    need_rebuild = true
                end
            end
            if need_rebuild then
                state.deferred_rebuild_frame=-1
                build_entity_list()
                if state.selected_entity_index>(state.entity_count or 0) then
                    state.selected_entity_index=1
                end
            elseif need_deferred and state.deferred_rebuild_frame<0 then
                state.deferred_rebuild_frame=state.frame_counter+10
            end
        end
    end

    drain_speech_queue(ctx)

    -- Tick the waypoint dialogs system: if a dialog is open,
    -- watch for Enter (accept) and Escape (cancel) and fire the
    -- corresponding callback. BizHawk's setproperty for the form's
    -- AcceptButton / CancelButton fails silently on some versions,
    -- so we bind these keys ourselves from the main loop.
    if waypoints then
        waypoints.tick(ctx.keys)
    end

    -- Suppress hotkey handling while a waypoint dialog is open.
    -- Without this gate, typing letters into the label textbox
    -- would also trigger M (coord probe), N (god-mode reveal in the
    -- probe script), B (tile dump), etc. The dialog itself owns the
    -- keyboard while it's the focused window -- the textbox eats
    -- the keystrokes -- but Lua's input.get() still sees them
    -- because BizHawk reports global key state, not focused-window
    -- key state.
    local dialog_open = waypoints and waypoints.is_open()

    local entities = state.entities
    local total    = state.entity_count
    local sel_idx  = state.selected_entity_index
    local sel      = entities[sel_idx]

    if dialog_open then
        -- Skip ALL hotkey handling. The dialog's tick() above
        -- already handled Enter/Escape; everything else is
        -- intentionally inert until the dialog closes.
        -- Fall through to the radar/footstep updates below since
        -- those don't depend on hotkeys.

    elseif ctx.just_pressed(CONFIG.map_name_hotkey) then
        if not in_cave then
            if state.cur_in_basement then
                -- Logic lives in Dungeons.lua (see BASEMENTS section).
                ctx.write_speech(dungeons.basement_location_label())
            elseif state.cur_level > 0 then
                -- Dungeon Insert: build location label on demand.
                -- If the player doesn't own the map for this dungeon,
                -- drop the room coordinates from the announcement and
                -- speak only "Dungeon N, Name." -- matches the
                -- visual game where the inventory map is empty
                -- without the map item.
                local fmt = dungeon_fmt(ctx)
                local live_room_id = mainmemory.read_u8(ADDR.room_id)
                if dungeon_has_map(state.cur_level) then
                    ctx.write_speech(
                        dungeons.location_label(state.cur_level,
                            live_room_id, fmt))
                else
                    -- No map: speak just "Dungeon N, Name." without
                    -- the room coordinates.
                    ctx.write_speech(string.format("Dungeon %d, %s.",
                        state.cur_level,
                        dungeons.get_dungeon_name(state.cur_level)))
                end
            else
                local fresh_room_id=mainmemory.read_u8(ADDR.room_id)
                if fresh_room_id~=state.cur_room_id then on_room_entry(ctx) end
                local fmt = ctx.settings.coordinate_format or "numeric"
                ctx.write_speech(string.format("Overworld, Screen %s.",
                    screen_coord_str(state.cur_room_id, fmt)))
            end
        end

    -- D hotkey: replay the most recently spoken dialog in the current
    -- location. Mnemonic: D for Dialog. The cave and dungeon dialog
    -- systems cache their lines in state.last_dialog_spoken when they
    -- fire; this hotkey reads the cache back. Cache is cleared on every
    -- room transition, so leaving and returning doesn't replay a stale
    -- line. If the room has no dialog (or the player hasn't heard it
    -- yet because slots haven't settled), D is silent.
    elseif ctx.just_pressed("D") then
        if state.last_dialog_spoken then
            ctx.write_speech(state.last_dialog_spoken)
        end

    elseif state.cur_level > 0 and not state.cur_in_basement
            and ctx.just_pressed("C") then
        -- Dungeon C: announce compass info -- progressively richer
        -- as the player acquires items. Mnemonic: C for Compass.
        -- Was previously B (moved to free B for bomb count).
        --   no compass         -> "No compass."
        --   compass alone      -> "Boss room is in the [direction] direction."
        --   compass + map      -> "Boss room is at [col, row]."
        --
        -- Live room read for the same reason as N -- catches the
        -- moment between transitions.
        if not dungeon_has_compass(state.cur_level) then
            ctx.write_speech("No compass.")
        else
            local boss = dungeon_boss_room()
            local live_room_id = mainmemory.read_u8(ADDR.room_id)
            if dungeon_has_map(state.cur_level) then
                local fmt = dungeon_fmt(ctx)
                ctx.write_speech(string.format("Boss room is at %s.",
                    dungeons.format_room_coord(boss, fmt)))
            else
                local dir = dungeon_room_direction(live_room_id, boss)
                if dir == "Here" then
                    ctx.write_speech("Boss room is here.")
                else
                    ctx.write_speech(string.format(
                        "Boss room is in the %s direction.", dir))
                end
            end
        end

    elseif ctx.just_pressed_any and ctx.just_pressed_any(CONFIG.coordinate_probe_hotkeys) then
        -- Tile probe always announces numeric, regardless of the
        -- coordinate_format setting. Tile coords go 0-31 across, which
        -- in spreadsheet mode would emit AA-AF for cols 26-31 -- those
        -- letter pairs come out garbled through NVDA. Numeric is
        -- unambiguous so we lock it here.
        ctx.write_speech(tile_coord_str(state.link_tile_x, state.link_tile_y, "numeric") .. ".")

    elseif waypoints and is_shift_period_pressed(ctx) then
        -- Shift + period: clear ALL waypoints, with a yes/no
        -- confirmation dialog. NVDA reads the native Windows dialog.
        local n = waypoints.count()
        if n == 0 then
            ctx.write_speech("No waypoints to clear.")
        else
            waypoints.confirm(
                "Clear all waypoints",
                string.format("Clear all %d waypoints?", n),
                function()  -- on yes
                    local cleared = waypoints.clear_all()
                    state.waypoint_cursor = 1
                    ctx.write_speech(string.format(
                        "Cleared %d waypoints.", cleared))
                end,
                function()  -- on no
                    ctx.write_speech("Clear cancelled.")
                end,
                ctx.write_speech)
        end

    elseif waypoints and is_shift_comma_pressed(ctx) then
        -- Shift + comma: remove the currently SELECTED waypoint, with
        -- a yes/no confirmation dialog. The selection is whichever
        -- waypoint was last announced by the , or / cycle keys; if
        -- nothing has been cycled yet, defaults to the first waypoint
        -- on the screen.
        local list = waypoints.list_in_room(
            state.cur_level, state.cur_room_id)
        if #list == 0 then
            ctx.write_speech("No waypoints on this screen.")
        else
            local cursor = state.waypoint_cursor or 1
            if cursor < 1 then cursor = 1 end
            if cursor > #list then cursor = #list end
            local target = list[cursor]
            local target_id    = target.id
            local target_label = target.label
            waypoints.confirm(
                "Remove waypoint",
                string.format("Remove %s?", target_label),
                function()  -- on yes
                    local removed = waypoints.remove_by_id(target_id)
                    if removed then
                        ctx.write_speech(string.format(
                            "Removed %s.", removed.label))
                    else
                        ctx.write_speech("Waypoint vanished.")
                    end
                    -- Clamp cursor for the now-shorter list
                    local new_count = #waypoints.list_in_room(
                        state.cur_level, state.cur_room_id)
                    if new_count == 0 then
                        state.waypoint_cursor = 1
                    elseif state.waypoint_cursor > new_count then
                        state.waypoint_cursor = new_count
                    end
                end,
                function()  -- on no
                    ctx.write_speech("Remove cancelled.")
                end,
                ctx.write_speech)
        end

    elseif waypoints and is_shift_slash_pressed(ctx) then
        -- Shift + slash: ADD a waypoint at Link's current tile. After
        -- adding, immediately open a label-edit dialog pre-filled with
        -- the auto-label "Waypoint N". User can type whatever they want
        -- and click OK / press Enter to commit, or Cancel / Escape to
        -- keep the auto-label.
        local wp = waypoints.add(
            state.cur_level, state.cur_room_id,
            state.link_tile_x, state.link_tile_y)
        ctx.write_speech(string.format("Added %s.", wp.label))
        local target_id = wp.id
        local initial   = wp.label
        waypoints.text_input(
            "Label waypoint",
            "Enter waypoint label:",
            initial,
            function(new_text)  -- on ok
                if new_text and new_text ~= "" and new_text ~= initial then
                    if waypoints.set_label(target_id, new_text) then
                        ctx.write_speech(string.format(
                            "Label set to %s.", new_text))
                    end
                end
            end,
            function()  -- on cancel
                ctx.write_speech("Kept default label.")
            end,
            ctx.write_speech)

    elseif waypoints and is_shift_m_pressed(ctx) then
        -- Shift + M: relabel the currently SELECTED waypoint. Opens the
        -- text-input dialog pre-filled with the current label. Useful
        -- for editing labels created earlier without a label edit, or
        -- for renaming.
        local list = waypoints.list_in_room(
            state.cur_level, state.cur_room_id)
        if #list == 0 then
            ctx.write_speech("No waypoints on this screen.")
        else
            local cursor = state.waypoint_cursor or 1
            if cursor < 1 then cursor = 1 end
            if cursor > #list then cursor = #list end
            local target = list[cursor]
            local target_id    = target.id
            local target_label = target.label
            waypoints.text_input(
                "Relabel waypoint",
                "Enter new label:",
                target_label,
                function(new_text)  -- on ok
                    if new_text and new_text ~= "" and new_text ~= target_label then
                        if waypoints.set_label(target_id, new_text) then
                            ctx.write_speech(string.format(
                                "Label set to %s.", new_text))
                        end
                    else
                        ctx.write_speech("Label unchanged.")
                    end
                end,
                function()  -- on cancel
                    ctx.write_speech("Relabel cancelled.")
                end,
                ctx.write_speech)
        end

    elseif waypoints and ctx.just_pressed_any
            and ctx.just_pressed_any(CONFIG.waypoint_navigate_hotkeys) then
        -- Bare period: navigate TO the selected waypoint on the
        -- current screen. Same kind of route announcement that End
        -- gives for entities. Targets the currently-cycled waypoint;
        -- if nothing's been cycled yet, picks the first one.
        local list = waypoints.list_in_room(
            state.cur_level, state.cur_room_id)
        if #list == 0 then
            ctx.write_speech("No waypoints on this screen.")
        else
            local cursor = state.waypoint_cursor or 1
            if cursor < 1 then cursor = 1 end
            if cursor > #list then cursor = #list end
            local wp = list[cursor]
            -- Synthesize an entity-like table for route_steps_to_entity.
            -- BFS uses approach_x/approach_y, so set those to the tile
            -- coords. Treat it as a regular floor target (no
            -- exit_direction) so the BFS path-finder routes there
            -- like any other entity.
            local pseudo = {
                approach_x = wp.tile_x,
                approach_y = wp.tile_y,
                x          = wp.tile_x,
                y          = wp.tile_y,
                label      = wp.label,
                type_name  = "waypoint",
            }
            ctx.write_speech(route_steps_to_entity(pseudo))
        end

    elseif waypoints and ctx.just_pressed_any
            and ctx.just_pressed_any(CONFIG.waypoint_prev_hotkeys) then
        -- Comma: cycle to PREVIOUS waypoint on the current screen.
        local list = waypoints.list_in_room(
            state.cur_level, state.cur_room_id)
        if #list == 0 then
            ctx.write_speech("No waypoints on this screen.")
        else
            state.waypoint_cursor = (state.waypoint_cursor or 1) - 1
            if state.waypoint_cursor < 1 then
                state.waypoint_cursor = #list
            end
            if state.waypoint_cursor > #list then
                state.waypoint_cursor = #list
            end
            local wp = list[state.waypoint_cursor]
            local steps = abs(wp.tile_x - state.link_tile_x) + abs(wp.tile_y - state.link_tile_y)
            local dir = rough_dir(state.link_tile_x, state.link_tile_y, wp.tile_x, wp.tile_y)
            if dir == "Here" then
                ctx.write_speech(string.format("%d, %s, here, %d of %d.",
                    state.waypoint_cursor, wp.label,
                    state.waypoint_cursor, #list))
            else
                ctx.write_speech(string.format("%d, %s, %d steps %s, %d of %d.",
                    state.waypoint_cursor, wp.label, steps, dir,
                    state.waypoint_cursor, #list))
            end
        end

    elseif waypoints and ctx.just_pressed_any
            and ctx.just_pressed_any(CONFIG.waypoint_next_hotkeys) then
        -- Slash: cycle to NEXT waypoint on the current screen.
        local list = waypoints.list_in_room(
            state.cur_level, state.cur_room_id)
        if #list == 0 then
            ctx.write_speech("No waypoints on this screen.")
        else
            state.waypoint_cursor = (state.waypoint_cursor or 0) + 1
            if state.waypoint_cursor > #list then
                state.waypoint_cursor = 1
            end
            if state.waypoint_cursor < 1 then
                state.waypoint_cursor = 1
            end
            local wp = list[state.waypoint_cursor]
            local steps = abs(wp.tile_x - state.link_tile_x) + abs(wp.tile_y - state.link_tile_y)
            local dir = rough_dir(state.link_tile_x, state.link_tile_y, wp.tile_x, wp.tile_y)
            if dir == "Here" then
                ctx.write_speech(string.format("%d, %s, here, %d of %d.",
                    state.waypoint_cursor, wp.label,
                    state.waypoint_cursor, #list))
            else
                ctx.write_speech(string.format("%d, %s, %d steps %s, %d of %d.",
                    state.waypoint_cursor, wp.label, steps, dir,
                    state.waypoint_cursor, #list))
            end
        end

    elseif ctx.just_pressed(CONFIG.repeat_entity_hotkey) then
        if total==0 then ctx.write_speech("No entities on this screen.")
        else emit_entity_ping(ctx,sel); ctx.write_speech(format_callout(sel,sel_idx,total)) end

    elseif ctx.just_pressed(CONFIG.previous_entity_hotkey) then
        if total>0 then
            sel_idx=sel_idx-1; if sel_idx<1 then sel_idx=total end
            state.selected_entity_index=sel_idx; sel=entities[sel_idx]
            state.locked_enemy_slot=(sel and sel.type_name=="enemy") and sel.slot or nil
            emit_entity_ping(ctx,sel); ctx.write_speech(format_callout(sel,sel_idx,total))
        else ctx.write_speech("No entities on this screen.") end

    elseif ctx.just_pressed(CONFIG.next_entity_hotkey) then
        if total>0 then
            sel_idx=sel_idx+1; if sel_idx>total then sel_idx=1 end
            state.selected_entity_index=sel_idx; sel=entities[sel_idx]
            state.locked_enemy_slot=(sel and sel.type_name=="enemy") and sel.slot or nil
            emit_entity_ping(ctx,sel); ctx.write_speech(format_callout(sel,sel_idx,total))
        else ctx.write_speech("No entities on this screen.") end

    elseif ctx.just_pressed(CONFIG.navigate_entity_hotkey) then
        if sel then ctx.write_speech(route_steps_to_entity(sel))
        else ctx.write_speech("No entity selected.") end

    end

    update_enemy_radar(ctx)
    update_item_radar(ctx)

    local paused=mainmemory.read_u8(0x00E0)
    if paused==0 then
        if in_cave or state.live_grid_cache~=nil then
            update_footsteps(ctx)
            update_bumps(ctx)
        end
    end

    if CONFIG.show_debug_overlay then
        local game_mode=mainmemory.read_u8(ADDR.game_mode)
        gui.text(8,8,string.format("room=%02X mode=%02X cave=%s",state.cur_room_id,game_mode,tostring(in_cave)))
        gui.text(8,24,string.format("tile=%d,%d entities=%d sel=%d",state.link_tile_x,state.link_tile_y,total,sel_idx))
    end
end

function M.update(ctx)
    local ok, err = pcall(safe_update, ctx)
    if not ok then
        write_log("RUNTIME ERROR: " .. tostring(err))
        ctx.write_speech("Navigation error, check log.")
    end
end

return M
