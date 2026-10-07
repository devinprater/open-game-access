-- oga_nes_reader.lua — the OGA host wrapper for the NES readers (Zelda 1 Access, Dragon Warrior
-- Access). Loaded as the core's entry script; it installs the BizHawk-shaped surface the readers
-- expect, then hands control to the reader's own entry file.
--
-- ⛔ WHY A WRAPPER AND NOT THE READER DIRECTLY. Both readers do
--
--     local SCRIPT_PATH = debug.getinfo(1, "S").source:sub(2)
--     local ROOT_DIR    = SCRIPT_PATH:match("^(.*)[/\\][^/\\]+$")
--     local DATA_DIR    = ROOT_DIR .. "/Data"
--
-- i.e. they find their own siblings RELATIVE TO THE FILE THAT IS RUNNING. Loading the reader via
-- loadfile() from here gives it THIS file as "the running chunk", so ROOT_DIR would resolve to the
-- wrapper's directory and every Data/ lookup would fail. So the wrapper does what the mods' own
-- loaders do: read the reader's source and run it with a chunk NAME that is the reader's real path.
-- That is the same trick the mods use on each other, and it keeps their relative-path logic intact.
--
-- ⛔ THE READER PATH IS DERIVED FROM THIS FILE, NEVER HARD-CODED. It used to be an absolute
-- `/home/devin/oga-work/...` path — the developer's own machine. On a device that path does not
-- exist, so `assert(io.open(READER))` failed and the console booted with no reader at all. The
-- readers themselves already solve this with debug.getinfo(1,"S").source; do the same here, which
-- also means the whole set can live in a writable per-device directory (the readers WRITE into
-- their own Data/ dir — speech, settings, waypoints — and an app bundle is read-only).
--
-- ⛔ THE NES RAM BASE IS 0, NOT THE DS's 0x02000000. mainmemory.* in OGA's shim is offset-relative
-- from a RAM_BASE that shim hardcodes to the DS mapping. Rather than edit that shared shim (the DS
-- path depends on it), this wrapper defines the NES-correct bindings itself. Both readers call
-- mainmemory.read_u8 with NES offsets (0x0010..0x06FF), so on this console the offset IS the address.
--
-- ⛔ WHAT IS DELIBERATELY ABSENT: the mods' PowerShell SoundBridge. Both ship one for spatial cues
-- and footsteps, and OGA has no equivalent path yet. The readers pcall around it, so speech works
-- and cues stay silent. Recorded as a known gap, not an oversight.

-- ---------------------------------------------------------------- locate ourselves
local function script_dir()
    local src = debug.getinfo(1, "S").source or ""
    if src:sub(1, 1) == "@" then src = src:sub(2) end
    src = src:gsub("\\", "/")
    return src:match("^(.*)/[^/]+$") or "."
end

local HERE = script_dir()
local READER = HERE .. "/Zelda1Access.lua"

-- ---------------------------------------------------------------- the NES memory surface
-- memory.* is bound by the core (Core/mesen_core.cpp): read_u8, read_u16_le, read_u32_le,
-- read_bytes_as_array, write_u8, usememorydomain. mainmemory.* is BizHawk's OFFSET-relative view,
-- which on the NES is the same as the address because the CPU space IS flat 64 KB from 0.

mainmemory = mainmemory or {}

local function in_ram(off) return off ~= nil and off >= 0 and off < 0x800 end

function mainmemory.read_u8(off)
    if not in_ram(off) then return 0 end
    local ok, v = pcall(memory.read_u8, off)
    if ok and v then return v end
    return 0
end

function mainmemory.read_u16_le(off)
    if not in_ram(off) then return 0 end
    local ok, v = pcall(memory.read_u16_le, off)
    if ok and v then return v end
    return 0
end

function mainmemory.read_u32_le(off)
    if not in_ram(off) then return 0 end
    local ok, v = pcall(memory.read_u32_le, off)
    if ok and v then return v end
    return 0
end

function mainmemory.read_bytes_as_array(off, n)
    local ok, arr = pcall(memory.read_bytes_as_array, off, n)
    if ok and arr then return arr end
    local out = {}
    for i = 0, (n or 0) - 1 do out[i + 1] = mainmemory.read_u8(off + i) end
    return out
end

-- Writes are accepted and discarded: the readers use them for their own bookkeeping and guard them
-- in pcall. Letting a script write guest RAM would break the read-only contract every adapter here
-- holds to. (Core/mesen_core.cpp's memory.write_u8 does the same, and says why.)
function mainmemory.write_u8(off, v)
    if in_ram(off) then pcall(memory.write_u8, off, v) end
end

-- ---------------------------------------------------------------- misc BizHawk surface
console = console or {}
console.writeline = console.writeline or function(s) if console.log then console.log(tostring(s)) end end
console.clear = console.clear or function() end
gui = gui or {}
gui.text = gui.text or function() end
gui.drawText = gui.drawText or function() end

joypad = joypad or {}
joypad.get = joypad.get or function() return joypad.get(0) end
joypad.getimmediate = joypad.getimmediate or function() return {} end
joypad.setanalog = joypad.setanalog or function() end
joypad.setfrommnemonicstr = joypad.setfrommnemonicstr or function() end

emu = emu or {}
emu.framecount = emu.framecount or function() return 0 end
emu.getsystemid = emu.getsystemid or function() return "NES" end
emu.getversion = emu.getversion or function() return "OGA-NES" end
emu.registerexit = emu.registerexit or function() end
emu.registerafter = emu.registerafter or function() end
emu.registerbefore = emu.registerbefore or function() end
emu.yield = emu.yield or emu.frameadvance or function() end

memory.usememorydomain = memory.usememorydomain or function(d) return d end
memory.getcurrentmemorydomain = memory.getcurrentmemorydomain or function() return "System Bus" end
memory.readbyte = memory.readbyte or memory.read_u8
memory.readword = memory.readword or memory.read_u16_le
memory.writebyte = memory.writebyte or function() end
memory.getmemorydomainsize = memory.getmemorydomainsize or function() return 0x10000 end

input = input or {}
input.get = input.get or function() return joypad.get and joypad.get() or {} end
input.read = input.read or input.get

savestate = savestate or {}
savestate.save = savestate.save or function() end
savestate.load = savestate.load or function() end

-- ---------------------------------------------------------------- the reader's speech file
-- ⛔ ASK THE READER, DO NOT GUESS -- AND SEARCH ITS WHOLE TREE, NOT JUST ITS ENTRY FILE.
--
-- Both mods also ship a sound_bridge_command.txt whose contents look like "<seq>|text", identical in
-- shape to real speech, so a content test picks the sound file and the player hears nothing while
-- the log says speech happened. And the declaration is not always in the entry file: Zelda declares
-- `local SPEECH_FILE = ...` at file scope, while Dragon Warrior declares `speech_file = ...` inside a
-- Data MODULE and its entry file never mentions it. Reading only the entry file left DW silent.
--
-- ⛔ oga.listfiles, NOT io.popen("ls"). The shell version was developer-only: iOS sandboxes an app
-- out of fork/exec, so popen returns nil and the candidate list collapses to the entry file alone --
-- which is exactly the DW case above, i.e. Dragon Warrior would have shipped silent. The core walks
-- the directory itself (Core/mesen_core.cpp) and hands back the names.
do
    local DATA_DIR = HERE .. "/Data"

    -- Every source in the reader's tree, entry file first.
    local candidates = { READER }
    if oga and oga.listfiles then
        local files = oga.listfiles(DATA_DIR)
        if type(files) == "table" then
            for i = 1, #files do
                local p = files[i]
                if type(p) == "string" and p:sub(-4) == ".lua" then
                    candidates[#candidates + 1] = p
                end
            end
        end
    end

    local expr, found_in = nil, nil
    for _, path in ipairs(candidates) do
        local f = io.open(path, "rb")
        if f then
            local body = f:read("*a")
            f:close()
            if body then
                local e = body:match("SPEECH_FILE%s*=%s*([^\n\r]+)")
                       or body:match("speech_file%s*=%s*([^\n\r]+)")
                if e then
                    e = e:gsub(",%s*$", ""):gsub("%s+$", "")
                    expr, found_in = e, path
                    break
                end
            end
        end
    end

    local resolved = nil
    if expr then
        -- ⛔ EVALUATE THE READER'S OWN EXPRESSION, do not re-implement its path arithmetic. The first
        -- attempt took the literal suffix and joined it to the reader's directory, which dropped the
        -- "/Data" that DATA_DIR contributes -- a path that does not exist, so the poll read nothing
        -- while the log cheerfully said "speech file set".
        local fn = load("return " .. expr, "speech_path", "t", {
            DATA_DIR = DATA_DIR, ROOT_DIR = HERE,
        })
        if fn then
            local ok, v = pcall(fn)
            if ok and type(v) == "string" then resolved = v end
        end
    end

    if resolved and oga and oga.set_speech_file then
        oga.set_speech_file(resolved)
    else
        -- LOUDLY, because a reader that works and cannot be heard looks exactly like a reader that
        -- works. Say which files were searched so the reason is diagnosable.
        console.log("[oga] could not resolve the reader's speech file. Declared expression: " ..
                    tostring(expr) .. "  (searched " .. tostring(#candidates) .. " files)")
    end
end

-- ---------------------------------------------------------------- run the reader
-- Read the reader's source and run it with ITS OWN PATH as the chunk name, so debug.getinfo(1,"S")
-- inside the reader sees the reader's file, not this one. This is what makes their Data/ lookups
-- work without editing a single line of the mod.
local f = assert(io.open(READER, "rb"))
local src = f:read("*a")
f:close()

-- A leading '@' marks a file-backed chunk; load(chunk, @path) is exactly what loadfile does.
local chunk, err = load(src, "@" .. READER)
if not chunk then error("reader failed to compile: " .. tostring(err)) end
return chunk()