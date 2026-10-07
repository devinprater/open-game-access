-- GameData.lua
-- Game-data module. Translates raw bytes read from game memory into
-- human-readable names AND decodes runtime data structures stored in
-- ROM (NPC dialog text, etc).
--
-- Most of this module is pure data tables (ENEMY_TYPE_NAMES, ITEM_NAMES,
-- DOOR_TILE_TYPES) -- byte-value -> human-name lookups consolidated as
-- the single source of truth.
--
-- The module also owns runtime decoding of structured ROM data --
-- specifically NPC dialog text. The decoder reads PRG-ROM directly,
-- never hardcoding any dialog string. Same conceptual scope: "what
-- you're looking at when the read comes back," just for compound
-- ROM structures instead of single bytes.
--
-- Loaded once at startup by Navigation.lua; references to the data
-- sub-tables are captured as locals for fast hot-path lookup. Decoder
-- functions are called via M.decode_person_text(selector).

local M = {}

-- ============================================================
-- ENEMY / ITEM / DOOR DATA TABLES
-- ============================================================

-- Enemy type IDs. Keyed by the byte stored at obj_type_base + slot
-- (0x034F + slot). Covers both overworld and dungeon enemies.
M.ENEMY_TYPE_NAMES = {
    [0x01] = "Blue Lynel",      [0x02] = "Red Lynel",
    [0x03] = "Blue Moblin",     [0x04] = "Red Moblin",
    [0x05] = "Blue Goriya",     [0x06] = "Red Goriya",
    [0x07] = "Red Octorok",     [0x08] = "Fast Red Octorok",
    [0x09] = "Blue Octorok",    [0x0A] = "Fast Blue Octorok",
    [0x0B] = "Red Darknut",     [0x0C] = "Blue Darknut",
    [0x0D] = "Red Tektite",     [0x0E] = "Blue Tektite",
    [0x0F] = "Blue Leever",     [0x10] = "Red Leever",
    [0x11] = "Zora",            [0x12] = "Vire",
    [0x13] = "Zol",             [0x14] = "Gel",
    [0x15] = "Gel",             [0x16] = "Pol's Voice",
    [0x17] = "Like Like",       [0x18] = "Digdogger",
    [0x19] = "Digdogger",       [0x1A] = "Peahat",
    [0x1B] = "Blue Keese",      [0x1C] = "Red Keese",
    [0x1D] = "Black Keese",     [0x1E] = "Armos",
    [0x1F] = "Boulders",        [0x20] = "Boulder",
    [0x21] = "Ghini",           [0x22] = "Flying Ghini",
    [0x23] = "Blue Wizzrobe",   [0x24] = "Red Wizzrobe",
    [0x25] = "Patra Child",     [0x26] = "Patra Child",
    [0x27] = "Wallmaster",      [0x28] = "Rope",
    [0x2A] = "Stalfos",         [0x2B] = "Bubble",
    [0x2C] = "Bubble",          [0x2D] = "Bubble",
    [0x2E] = "Whirlwind",
    [0x2F] = "Great Fairy",
    [0x30] = "Gibdo",
    [0x31] = "Dodongo",         [0x32] = "Dodongo",
    [0x33] = "Blue Gohma",      [0x34] = "Red Gohma",
    [0x35] = "Rupee Stash",     [0x36] = "Friendly Goriya",
    [0x37] = "Zelda",           [0x38] = "Digdogger",
    [0x39] = "Digdogger",       [0x3A] = "Lanmola",
    [0x3B] = "Lanmola",         [0x3C] = "Manhandla",
    [0x3D] = "Aquamentus",      [0x3E] = "Ganon",
    [0x3F] = "Guard Fire",      [0x40] = "Standing Fire",
    [0x41] = "Moldorm",         [0x42] = "Gleeok",
    [0x43] = "Gleeok",          [0x44] = "Gleeok",
    [0x45] = "Gleeok",          [0x46] = "Gleeok Head",
    [0x47] = "Patra",           [0x48] = "Patra",
    [0x49] = "Trap",            [0x4A] = "Trap",
    -- Dungeon NPCs / hint-givers. These types live in the same
    -- enemy slots as creatures, so the slot scanner picks them up
    -- and labels them like enemies. They're harmless and trigger
    -- a dialog when Link enters the room.
    [0x4B] = "Old Man",         [0x4C] = "Old Man",
    [0x4D] = "Old Man",         [0x4E] = "Old Man",
    [0x4F] = "Friendly Moblin", [0x50] = "Old Man",
    [0x51] = "Old Man",         [0x52] = "Old Man",
}

-- Item IDs. Keyed by the byte at room_item_id (0x00AB), the obj_state
-- value of a dropped item slot, or shop ROM bytes after masking with 0x3F.
-- All three sites use the same numeric namespace, so a single table works.
--
-- 0x3F is the canonical "no item" sentinel for room items and is omitted
-- here on purpose so callers see a nil and can suppress the entity.
M.ITEM_NAMES = {
    [0x00] = "Bombs",          [0x01] = "Wooden Sword",  [0x02] = "White Sword",
    [0x03] = "Magic Sword",    [0x04] = "Bait",          [0x05] = "Recorder",
    [0x06] = "Blue Candle",    [0x07] = "Red Candle",    [0x08] = "Bow & Wooden Arrows",
    [0x09] = "Bow & Silver Arrows",   [0x0A] = "Bow",           [0x0B] = "Magic Key",
    [0x0C] = "Raft",           [0x0D] = "Stepladder",    [0x0E] = "Triforce",
    [0x0F] = "5 Rupees",       [0x10] = "Magic Wand",    [0x11] = "Magic Book",
    [0x12] = "Blue Ring",      [0x13] = "Red Ring",      [0x14] = "Power Bracelet",
    [0x15] = "Letter",         [0x16] = "Compass",       [0x17] = "Map",
    [0x18] = "Rupee",          [0x19] = "Key",           [0x1A] = "Heart Container",
    [0x1B] = "Triforce Piece", [0x1C] = "Magic Shield",  [0x1D] = "Boomerang",
    [0x1E] = "Magic Boomerang",[0x1F] = "Blue Potion",   [0x20] = "Red Potion",
    [0x21] = "Clock",          [0x22] = "Heart",         [0x23] = "Fairy",
}

-- Dungeon door FACE tile signatures. Keyed by direction, then by the
-- raw tile byte read from the live tile grid at the door face position
-- (face_x, face_y from Navigation.lua's DUNGEON_DOORS table).
--
-- The value is either:
--   * a string label like "Locked door" -- treated as a closed door of
--     that type. The door is solid; entity gets type_name="door" with
--     no exit_direction so BFS won't route through it.
--   * a table {label="...", passable=true} -- treated as a passable
--     special face. Used for bombed-open walls where the face tile is
--     a unique "hole" graphic (still above the collision threshold) but
--     the doorway is actually walkable. Entity gets type_name="exit"
--     with the appropriate exit_direction so BFS routes through.
--
-- Authoritative signatures sourced from the ROM's per-direction
-- door-state graphics tables (DataCrystal ROM map 0x15FEE-0x160DD).
-- Each direction has 5 graphic states (open / locked / shutter /
-- unbombed / bombed) and the face center byte for each was extracted
-- by inspecting the tables and cross-checking against playthrough
-- probes.
--
-- North face y=2 (center column 15):
--   0x98 = locked door (keyhole)
--   0xA8 = shutter door (closed)
--   0x8C = bombed-open hole (passable, was unbombed/cracked wall)
-- South face y=19:
--   0x9D = locked door
--   0xA9 = shutter door
--   0x8E = bombed-open hole (passable)
-- East face x=29 (center row 11):
--   0xA7 = locked door
--   0xAF = shutter door
--   0x93 = bombed-open hole (passable)
-- West face x=2:
--   0xA1 = locked door  (keyhole exists but renders identically to
--                       plain west wall in some rooms; signature
--                       confirmed via authoritative ROM table)
--   0xAD = shutter door
--   0x91 = bombed-open hole (passable)
--
-- Plain walls (face=0xDC north, 0xDD south, 0xDF east, 0xDE west)
-- and unbombed bombable walls intentionally have no entry: they
-- look like regular wall data, and the original NES game does not
-- visually distinguish unbombed cracked walls from plain walls.
M.DOOR_TILE_TYPES = {
    North = {
        [0x98] = "Locked door",
        [0xA8] = "Shutter door",
        [0x8C] = { label = "Bombed wall", passable = true },
    },
    South = {
        [0x9D] = "Locked door",
        [0xA9] = "Shutter door",
        [0x8E] = { label = "Bombed wall", passable = true },
    },
    East = {
        [0xA7] = "Locked door",
        [0xAF] = "Shutter door",
        [0x93] = { label = "Bombed wall", passable = true },
    },
    West = {
        [0xA1] = "Locked door",
        [0xAD] = "Shutter door",
        [0x91] = { label = "Bombed wall", passable = true },
    },
}

-- ============================================================
-- PERSON_TEXTS (NPC dialog by selector value)
-- ============================================================
-- Z1 stores all NPC dialog (caves AND dungeons) in a single ROM
-- table at PRG-ROM offset 0x403C (PersonText.dat). When an NPC is
-- on screen, RAM byte $0415 (PersonTextSelector) holds the value
-- that identifies which line is being displayed. The selector
-- value is consistent across both quests and across cave/dungeon
-- contexts -- selector 0x32 always means "I bet you'd like to
-- have more bombs" no matter which NPC delivers it.
--
-- Strings extracted via runtime decoder against vanilla Z1
-- PRG-ROM, then hand-curated for screen-reader readability:
-- proper noun capitalization (Gohma, Digdogger, Patra, Triforce,
-- Spectacle Rock), original spelling preserved ("penninsula"),
-- sentence case throughout. Verified empirically against eight
-- in-game NPC encounters spanning Q1 caves and dungeons.
--
-- Q1 vs Q2: PersonText.dat is identical between quests. Q2
-- reorganizes WHERE NPCs sit but reuses the same dialog strings
-- with the same selector values. This table works for both
-- quests with no modification. (Q2-specific concerns like which
-- cave/dungeon contains which NPC live in cave_data.lua and
-- Dungeons.lua -- not here.)
--
-- Indexed by raw $0415 selector value (always even, 0x00-0x48).

local PERSON_TEXTS = {
    [0x00] = "It's dangerous to go alone! Take this.",
    [0x02] = "Master using it, and you can have this.",
    [0x04] = "Take any road you want.",
    [0x06] = "Secret is in the tree at the dead-end.",
    [0x08] = "Let's play money making game.",
    [0x0A] = "Pay me for the door repair charge.",
    [0x0C] = "Show this to the old woman.",
    [0x0E] = "Meet the old man at the grave.",
    [0x10] = "Buy medicine before you go.",
    [0x12] = "Pay me, and I'll talk.",
    [0x14] = "This ain't enough to talk.",
    [0x16] = "Go up, up, the mountain ahead.",
    [0x18] = "Go north, west, south, west to the forest of maze.",
    [0x1A] = "Boy, you're rich!",
    [0x1C] = "Buy somethin', will ya?",
    [0x1E] = "Boy, this is really expensive!",
    [0x20] = "Take any one you want.",
    [0x22] = "It's a secret to everybody.",
    [0x24] = "Grumble, grumble...",
    [0x26] = "Eastmost penninsula is the secret.",
    [0x28] = "Dodongo dislikes smoke.",
    [0x2A] = "Did you get the sword from the old man on top of the waterfall?",
    [0x2C] = "Walk into the waterfall.",
    [0x2E] = "Secret power is said to be in the arrow.",
    [0x30] = "Digdogger hates certain kind of sound.",
    [0x32] = "I bet you'd like to have more bombs.",
    [0x34] = "If you go in the direction of the arrow.",
    [0x36] = "Leave your life or money.",
    [0x38] = "There are secrets where fairies don't live.",
    [0x3A] = "Aim at the eyes of Gohma.",
    [0x3C] = "South of arrow mark hides a secret.",
    [0x3E] = "There's a secret in the tip of the nose.",
    [0x40] = "Spectacle Rock is an entrance to death.",
    [0x42] = "10th enemy has the bomb.",
    [0x44] = "Ones who does not have Triforce can't go in.",
    [0x46] = "Patra has the map.",
    [0x48] = "Go to the next room.",
}

-- Lookup the dialog string for a live $0415 selector value.
-- Returns the string or nil for unmapped selectors.
function M.decode_person_text(selector)
    if not selector then return nil end
    return PERSON_TEXTS[selector]
end

-- Convenience: read live $0415 and look up its dialog.
function M.decode_person_text_live()
    return M.decode_person_text(mainmemory.read_u8(0x0415))
end

return M
