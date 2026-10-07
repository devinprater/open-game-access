local M = {}

local WAYPOINTS_FILE = nil

local data = { next_id = 1, waypoints = {} }

-- ============================================================
-- JSON SERIALIZER
-- ============================================================

local function is_array(t)
    if type(t) ~= "table" then return false end
    local n = 0
    for k, _ in pairs(t) do
        if type(k) ~= "number" then return false end
        if k > n then n = k end
    end
    for i = 1, n do
        if t[i] == nil then return false end
    end
    return true
end

local function escape_string(s)
    s = s:gsub("\\", "\\\\")
    s = s:gsub("\"", "\\\"")
    return "\"" .. s .. "\""
end

local function encode(v, indent_level)
    indent_level = indent_level or 0
    local pad = string.rep("  ", indent_level)
    local inner_pad = string.rep("  ", indent_level + 1)

    if v == nil then
        return "null"
    elseif type(v) == "boolean" then
        return v and "true" or "false"
    elseif type(v) == "number" then
        if v == math.floor(v) and v < 1e15 and v > -1e15 then
            return string.format("%d", v)
        end
        return string.format("%g", v)
    elseif type(v) == "string" then
        return escape_string(v)
    elseif type(v) == "table" then
        if is_array(v) then
            if #v == 0 then return "[]" end
            local parts = {}
            for i, e in ipairs(v) do
                parts[#parts + 1] = inner_pad .. encode(e, indent_level + 1)
            end
            return "[\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "]"
        else
            local keys = {}
            for k, _ in pairs(v) do keys[#keys + 1] = k end
            table.sort(keys)
            if #keys == 0 then return "{}" end
            local parts = {}
            for _, k in ipairs(keys) do
                parts[#parts + 1] = inner_pad .. escape_string(tostring(k))
                                 .. ": " .. encode(v[k], indent_level + 1)
            end
            return "{\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "}"
        end
    end
    return "null"
end

-- ============================================================
-- JSON DECODER
-- ============================================================

local function skip_ws(s, i)
    while i <= #s and s:sub(i, i):match("%s") do i = i + 1 end
    return i
end

local decode_value

local function decode_string(s, i)
    if s:sub(i, i) ~= "\"" then return nil, i end
    i = i + 1
    local out = {}
    while i <= #s do
        local c = s:sub(i, i)
        if c == "\"" then
            return table.concat(out), i + 1
        elseif c == "\\" then
            local nxt = s:sub(i + 1, i + 1)
            if nxt == "\\" or nxt == "\"" then
                out[#out + 1] = nxt
                i = i + 2
            else
                out[#out + 1] = c .. nxt
                i = i + 2
            end
        else
            out[#out + 1] = c
            i = i + 1
        end
    end
    return nil, i
end

local function decode_number(s, i)
    local start = i
    if s:sub(i, i) == "-" then i = i + 1 end
    while i <= #s and s:sub(i, i):match("[0-9.eE%-+]") do i = i + 1 end
    local num = tonumber(s:sub(start, i - 1))
    return num, i
end

local function decode_object(s, i)
    if s:sub(i, i) ~= "{" then return nil, i end
    i = skip_ws(s, i + 1)
    local out = {}
    if s:sub(i, i) == "}" then return out, i + 1 end
    while i <= #s do
        i = skip_ws(s, i)
        local key
        key, i = decode_string(s, i)
        if not key then return nil, i end
        i = skip_ws(s, i)
        if s:sub(i, i) ~= ":" then return nil, i end
        i = skip_ws(s, i + 1)
        local val
        val, i = decode_value(s, i)
        if val == nil and s:sub(i, i) ~= "n" then
            return nil, i
        end
        out[key] = val
        i = skip_ws(s, i)
        local c = s:sub(i, i)
        if c == "," then
            i = skip_ws(s, i + 1)
        elseif c == "}" then
            return out, i + 1
        else
            return nil, i
        end
    end
    return nil, i
end

local function decode_array(s, i)
    if s:sub(i, i) ~= "[" then return nil, i end
    i = skip_ws(s, i + 1)
    local out = {}
    if s:sub(i, i) == "]" then return out, i + 1 end
    while i <= #s do
        i = skip_ws(s, i)
        local val
        val, i = decode_value(s, i)
        if val == nil and s:sub(i, i) ~= "n" then
            return nil, i
        end
        out[#out + 1] = val
        i = skip_ws(s, i)
        local c = s:sub(i, i)
        if c == "," then
            i = skip_ws(s, i + 1)
        elseif c == "]" then
            return out, i + 1
        else
            return nil, i
        end
    end
    return nil, i
end

decode_value = function(s, i)
    i = skip_ws(s, i)
    local c = s:sub(i, i)
    if c == "\"" then
        return decode_string(s, i)
    elseif c == "{" then
        return decode_object(s, i)
    elseif c == "[" then
        return decode_array(s, i)
    elseif c == "t" then
        if s:sub(i, i + 3) == "true" then return true, i + 4 end
    elseif c == "f" then
        if s:sub(i, i + 4) == "false" then return false, i + 5 end
    elseif c == "n" then
        if s:sub(i, i + 3) == "null" then return nil, i + 4 end
    elseif c == "-" or c:match("[0-9]") then
        return decode_number(s, i)
    end
    return nil, i
end

-- ============================================================
-- FILE I/O
-- ============================================================

local function read_file(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local s = f:read("*a")
    f:close()
    return s
end

local function write_file(path, s)
    local f = io.open(path, "w")
    if not f then return false end
    f:write(s)
    f:close()
    return true
end

local function save()
    if not WAYPOINTS_FILE then return false end
    return write_file(WAYPOINTS_FILE, encode(data) .. "\n")
end

local function load_from_disk()
    local s = read_file(WAYPOINTS_FILE)
    if not s or s == "" then return end
    local parsed, _ = decode_value(s, 1)
    if type(parsed) ~= "table" then return end
    if type(parsed.waypoints) == "table" then
        data.waypoints = parsed.waypoints
    end
    if type(parsed.next_id) == "number" then
        data.next_id = parsed.next_id
    end
end

-- ============================================================
-- WAYPOINT DATA API
-- ============================================================

local function renumber()
    for i, wp in ipairs(data.waypoints) do
        wp.id = i
        if wp.label == nil or wp.label:match("^Waypoint %d+$") then
            wp.label = string.format("Waypoint %d", i)
        end
    end
    data.next_id = #data.waypoints + 1
end

function M.load(data_dir)
    WAYPOINTS_FILE = data_dir .. "/sD6wF3pX9rJqN.json"
    data = { next_id = 1, waypoints = {} }
    load_from_disk()
    if #data.waypoints > 0 then
        renumber()
        save()
    end
end

function M.add(level, room_id, tile_x, tile_y)
    local wp = {
        level   = level,
        room_id = room_id,
        tile_x  = tile_x,
        tile_y  = tile_y,
    }
    data.waypoints[#data.waypoints + 1] = wp
    renumber()
    save()
    return wp
end

function M.remove_at(level, room_id, tile_x, tile_y)
    for i, wp in ipairs(data.waypoints) do
        if wp.level == level
                and wp.room_id == room_id
                and wp.tile_x == tile_x
                and wp.tile_y == tile_y then
            local removed = wp
            table.remove(data.waypoints, i)
            renumber()
            save()
            return removed
        end
    end
    return nil
end

function M.remove_by_id(target_id)
    for i, wp in ipairs(data.waypoints) do
        if wp.id == target_id then
            local removed = wp
            table.remove(data.waypoints, i)
            renumber()
            save()
            return removed
        end
    end
    return nil
end

function M.clear_all()
    local n = #data.waypoints
    data.waypoints = {}
    data.next_id = 1
    save()
    return n
end

function M.set_label(target_id, new_label)
    if type(new_label) ~= "string" or new_label == "" then
        return false
    end
    for _, wp in ipairs(data.waypoints) do
        if wp.id == target_id then
            wp.label = new_label
            save()
            return true
        end
    end
    return false
end

function M.list_in_room(level, room_id)
    local out = {}
    for _, wp in ipairs(data.waypoints) do
        if wp.level == level and wp.room_id == room_id then
            out[#out + 1] = wp
        end
    end
    table.sort(out, function(a, b) return a.id < b.id end)
    return out
end

function M.count()
    return #data.waypoints
end

-- ============================================================
-- DIALOGS
-- ============================================================

local current = nil
local prev_enter  = false
local prev_escape = false

local function close_silent()
    if current and current.form then
        pcall(function() forms.destroy(current.form) end)
    end
    current = nil
end

function M.text_input(title, prompt, initial_text, on_ok, on_cancel, write_speech)
    close_silent()

    local form
    form = forms.newform(360, 140, title, function()
        if current and current.form == form then
            current.pending = "cancel"
        end
    end)

    forms.label(form, prompt, 12, 12, 330, 20)

    local tb = forms.textbox(form, initial_text or "",
        330, 22, nil, 12, 38, false, false, nil)

    local ok_btn = forms.button(form, "OK", function()
        if current and current.form == form then
            current.pending = "accept"
        end
    end, 188, 75, 80, 24)

    local cancel_btn = forms.button(form, "Cancel", function()
        if current and current.form == form then
            current.pending = "cancel"
        end
    end, 270, 75, 80, 24)

    pcall(function() forms.setproperty(form, "AcceptButton", ok_btn) end)
    pcall(function() forms.setproperty(form, "CancelButton", cancel_btn) end)

    pcall(function()
        forms.setproperty(form, "ActiveControl", tb)
        forms.setproperty(tb, "SelectionStart",  0)
        forms.setproperty(tb, "SelectionLength", #(initial_text or ""))
        forms.setproperty(tb, "Focused",         true)
    end)

    current = {
        kind      = "text",
        form      = form,
        textbox   = tb,
        on_accept = on_ok,
        on_cancel = on_cancel,
        pending   = nil,
    }

    if write_speech then
        write_speech(string.format("%s. %s.", title, prompt))
    end
end

function M.confirm(title, message, on_yes, on_no, write_speech)
    close_silent()

    local form
    form = forms.newform(360, 140, title, function()
        if current and current.form == form then
            current.pending = "cancel"
        end
    end)

    forms.label(form, message, 12, 16, 330, 40)

    local yes_btn = forms.button(form, "Yes", function()
        if current and current.form == form then
            current.pending = "accept"
        end
    end, 188, 75, 80, 24)

    local no_btn = forms.button(form, "No", function()
        if current and current.form == form then
            current.pending = "cancel"
        end
    end, 270, 75, 80, 24)

    pcall(function() forms.setproperty(form, "AcceptButton", yes_btn) end)
    pcall(function() forms.setproperty(form, "CancelButton", no_btn) end)
    pcall(function() forms.setproperty(form, "ActiveControl", no_btn) end)

    current = {
        kind      = "confirm",
        form      = form,
        textbox   = nil,
        on_accept = on_yes,
        on_cancel = on_no,
        pending   = nil,
    }

    if write_speech then
        write_speech(string.format("%s. %s.", title, message))
    end
end

function M.tick(keys)
    keys = keys or {}

    local enter_now  = keys.Enter  == true or keys.Return == true
    local escape_now = keys.Escape == true

    if current and current.pending == nil then
        if enter_now and not prev_enter then
            current.pending = "accept"
        elseif escape_now and not prev_escape then
            current.pending = "cancel"
        end
    end

    prev_enter  = enter_now
    prev_escape = escape_now

    if current and current.pending then
        local action = current.pending
        local on_accept = current.on_accept
        local on_cancel = current.on_cancel
        local kind = current.kind
        local tb = current.textbox
        local form = current.form

        local text_value = ""
        if action == "accept" and kind == "text" and tb then
            pcall(function() text_value = forms.gettext(tb) or "" end)
        end

        current = nil
        pcall(function() forms.destroy(form) end)

        if action == "accept" then
            if kind == "text" then
                if on_accept then on_accept(text_value) end
            else
                if on_accept then on_accept() end
            end
        else
            if on_cancel then on_cancel() end
        end
    end
end

function M.is_open()
    return current ~= nil
end

function M.close()
    close_silent()
end

return M
