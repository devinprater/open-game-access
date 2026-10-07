-- VisitedRooms.lua
-- Persistent tracking of dungeon rooms the player has entered.
-- Stored on disk as dungeon_visited.json so progress survives a
-- BizHawk restart. Keyed by save_slot + quest + level + room_id so
-- different save files don't bleed into each other.
--
-- Why we need this on top of the game's own bit 0x20:
--   * The game sets bit 0x20 during mode 4 (room-scroll) BEFORE our
--     room-entry handler runs. So by the time we check, the bit is
--     already set even on first entry. We can't use the bit alone to
--     answer "is this the player's first time here this run."
--   * We DO use bit 0x20 as a sanity check though: if the game says
--     it hasn't been visited, our cache is stale (e.g. save-state
--     load reverted the game's bit) and we should treat it as new.
--
-- Schema: { "<slot>:<quest>:<level>:<room>": true, ... }
-- All keys are 2-hex-digit padded so string sort matches numeric sort.

local M = {}

-- File handle pattern matches Waypoints.lua. The save dir is passed in
-- on load() so this module doesn't need to know the project layout.
local SAVE_PATH = nil
local data      = {}    -- map: key -> true

-- Tiny JSON serializer/parser. Same approach as Waypoints.lua --
-- avoids pulling in a JSON library while still producing
-- human-readable output.

local function escape(s)
    return (s:gsub("\\", "\\\\"):gsub('"', '\\"'))
end

local function serialize()
    local lines = { "{" }
    -- Sort keys so the file diffs cleanly between writes -- helpful
    -- when debugging or sharing saves.
    local keys = {}
    for k in pairs(data) do keys[#keys+1] = k end
    table.sort(keys)
    for i, k in ipairs(keys) do
        local sep = (i < #keys) and "," or ""
        lines[#lines+1] = string.format('  "%s": true%s', escape(k), sep)
    end
    lines[#lines+1] = "}"
    return table.concat(lines, "\n")
end

local function parse(text)
    local out = {}
    if not text or text == "" then return out end
    -- Match `"<key>": true` pairs. Tolerant of whitespace and trailing
    -- commas so files written by hand won't break us.
    for key in text:gmatch('"([^"]+)"%s*:%s*true') do
        out[key] = true
    end
    return out
end

-- Compose the canonical key for a room observation.
function M.key(save_slot, quest, level, room_id)
    return string.format("%02X:%02X:%02X:%02X",
        save_slot or 0, quest or 0, level or 0, room_id or 0)
end

-- Load existing visited map from disk. Called once at startup. The
-- save_dir is the same Data directory waypoints uses. If the file
-- doesn't exist yet we start with an empty map -- first save creates
-- it on the first add.
function M.load(save_dir)
    SAVE_PATH = save_dir .. "/cM4bV7zH2yKgT.json"
    local f = io.open(SAVE_PATH, "r")
    if not f then
        data = {}
        return
    end
    local text = f:read("*a")
    f:close()
    data = parse(text)
end

-- Force-write the current map to disk. Called whenever we add a new
-- room so progress isn't lost on a crash. Cheap (the map is at most
-- ~600 entries for a fully-explored 9-dungeon run).
function M.save()
    if not SAVE_PATH then return end
    local f = io.open(SAVE_PATH, "w")
    if not f then return end
    f:write(serialize())
    f:close()
end

function M.has(key)
    return data[key] == true
end

function M.add(key)
    if data[key] then return false end  -- already present, no save needed
    data[key] = true
    M.save()
    return true
end

-- Remove a single entry. Used by reconcile() when a save state load
-- reverts the game's visited bit.
function M.remove(key)
    if not data[key] then return false end
    data[key] = nil
    return true
end

-- Reconcile our cache against the game's per-room visited bits for a
-- specific save_slot+quest+level. The caller passes a function
-- bit_for(room_id) that returns true if the game's bit 0x20 is set
-- for that room. We walk all 256 possible room IDs and remove any
-- cache entry that the game says is unvisited.
--
-- This handles the save-state-load case: when the player loads a
-- state from before they visited room X, the game's bit reverts to 0
-- but our persistent cache still has the entry. After reconciliation
-- the cache matches the game's truth, so the next entry will speak
-- "New room" again.
--
-- Single save call at the end so we don't thrash the disk if many
-- rooms get pruned at once.
function M.reconcile(save_slot, quest, level, bit_for)
    if level <= 0 or not bit_for then return 0 end
    local removed = 0
    for room_id = 0x00, 0xFF do
        local key = M.key(save_slot, quest, level, room_id)
        if data[key] and not bit_for(room_id) then
            data[key] = nil
            removed = removed + 1
        end
    end
    if removed > 0 then M.save() end
    return removed
end

-- Wipe everything. Not currently called -- here for completeness in
-- case we add a "reset visited" hotkey or test workflow.
function M.clear()
    data = {}
    M.save()
end

function M.count()
    local n = 0
    for _ in pairs(data) do n = n + 1 end
    return n
end

return M
