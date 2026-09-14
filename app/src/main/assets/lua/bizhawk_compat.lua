-- bizhawk_compat.lua — BizHawk API shim for the Pokémon Access core.
--
-- Loaded BEFORE main.lua in the SAME Lua chunk (the app concatenates them), so
-- every top-level `local` declared here is spent from the 200-locals-per-chunk
-- budget that main.lua itself needs — and main.lua is already AT that ceiling
-- (it guards it explicitly; see its own "200-LOCALS CEILING" note). Declaring
-- even four locals here made the whole chunk fail to load:
--
--     main.lua:16449: too many local variables (limit is 200) in main function
--
-- The whole shim therefore lives inside ONE `do ... end` block: its locals are
-- released when the block ends, so the shim contributes ZERO standing locals to
-- the chunk, and every function it defines is assigned to a GLOBAL (mainmemory,
-- joypad, input, memory, emu, console, gameinfo, speech, _Update) — which is
-- exactly the namespace main.lua expects anyway.
--
-- Nothing here may be moved to the top level "for clarity": that is what broke it.

do

local RAM_BASE = 0x02000000
local RAM_SIZE = 0x400000

-- ---------- mainmemory (BizHawk: offsets relative to 0x02000000) ----------
mainmemory = mainmemory or {}

local function in_ram(off)
    return off ~= nil and off >= 0 and off < RAM_SIZE
end

-- The core's own memory library is bound as `memory.read_u8(addr, domain)`;
-- BizHawk's mainmemory works in offsets from Main RAM's base.
function mainmemory.read_u8(off)
    if not in_ram(off) then return 0 end
    local ok, v = pcall(memory.read_u8, RAM_BASE + off, "Main RAM")
    if ok and v then return v end
    return 0
end

function mainmemory.read_u16_le(off)
    if not in_ram(off) or not in_ram(off + 1) then return 0 end
    local ok, v = pcall(memory.read_u16_le, RAM_BASE + off, "Main RAM")
    if ok and v then return v end
    return 0
end

function mainmemory.read_u32_le(off)
    if not in_ram(off) or not in_ram(off + 3) then return 0 end
    local ok, v = pcall(memory.read_u32_le, RAM_BASE + off, "Main RAM")
    if ok and v then return v end
    return 0
end

function mainmemory.write_u8(off, v)
    if in_ram(off) then pcall(memory.write_u8, RAM_BASE + off, v, "Main RAM") end
end

-- BizHawk returns a 1- or 0-indexed table depending on the core; main.lua
-- handles BOTH ("arr[0] ~= nil and 0 or 1"), so the core's 1-indexed table is
-- passed straight through.
function mainmemory.read_bytes_as_array(off, n)
    if type(n) ~= "number" or n <= 0 then return {} end
    local ok, arr = pcall(memory.read_bytes_as_array, RAM_BASE + off, n, "Main RAM")
    if ok and type(arr) == "table" then return arr end
    return {}
end

-- ---------- joypad ----------
-- melonDS-lua exposes input.GetJoy()/NDSTapDown/Up; BizHawk's joypad.set writes
-- a per-frame button override that this core does not have. main.lua's
-- controller-mod layer uses set{} to BLOCK buttons while a modifier is held, so
-- an empty function degrades that layer rather than breaking the script.
joypad = joypad or {}

function joypad.set() end
function joypad.setanalog() end

function joypad.getimmediate()
    local joy = (input.GetJoy and input.GetJoy()) or {}
    local out = {}
    for k, v in pairs(joy) do out[k] = v end
    return out
end

-- "|TouchX,TouchY,MicVolume,0,<17 buttons>|" — the synthetic-touch mnemonic
-- main.lua builds for screens that need the panel (the Musical, the town map).
-- Touch is the 15th button character; 'T' means pressed.
function joypad.setfrommnemonicstr(s)
    if type(s) ~= "string" then return end
    local x, y = s:match("^|%s*(-?%d+)%s*,%s*(-?%d+)")
    if not x then return end
    if s:find("T", 1, true) then
        input.NDSTapDown(tonumber(x), tonumber(y))
    else
        input.NDSTapUp()
    end
end

-- ---------- memory domain wrapper ----------
-- main.lua reads the ROM header for its game-code check:
--     memory.read_u8(0x0C + i, "ROM")
-- The core's library is address-first/domain-last and already has a ROM domain,
-- so this only has to translate the domain NAME ("ROM" -> "ROM") and fall back
-- to Main RAM if the domain is unavailable, keeping the script alive.
if not memory._pa_wrapped then
    memory._pa_wrapped = true
    local core_read_u8 = memory.read_u8
    function memory.read_u8(addr, domain)
        if domain == "ROM" then
            local ok, v = pcall(core_read_u8, addr, "ROM")
            if ok and v then return v end
        end
        return core_read_u8(addr, domain)
    end
end

-- ---------- emu ----------
-- melonDS-lua runs the whole script once, then calls a global _Update() each
-- frame. main.lua is a BizHawk script shaped `while true do ... emu.frameadvance()
-- end`, so frameadvance() yields the coroutine the core resumes from _Update().
-- That is what keeps main.lua byte-identical to the original.
emu = emu or {}

function emu.framecount()
    emu._fc = (emu._fc or 0) + 1
    return emu._fc
end

function emu.frameadvance()
    coroutine.yield()
end

-- ---------- console / gameinfo ----------
console = console or {}
function console.writeline(s) print(tostring(s)) end

gameinfo = gameinfo or {}
if not gameinfo.getromname then
    -- detect_game() prefers the ROM header via memory.read_u8(.., "ROM"), so
    -- this fallback only matters if that read fails.
    function gameinfo.getromname() return "" end
end

-- ---------- speech ----------
-- The core installs a global `hermes_tts` whose speak(text, mode) drives the
-- platform's text-to-speech; speech.say is the name main.lua calls.
speech = speech or {}
if hermes_tts then
    speech.say = function(text, interrupt)
        hermes_tts.speak(tostring(text), (interrupt == false) and "queue" or "interrupt")
    end
    speech.stop = function() hermes_tts.stop() end
end

-- ---------- input ----------
-- main.lua's poll_keys() edge-detects a table of letter names ("R","U","J","K",
-- "L","I","O","C","N","P","E","B"). The core's input.HeldKeys() keys the same
-- table by ASCII code, so translate.
local QT = {
    [65]="A",[66]="B",[67]="C",[68]="D",[69]="E",[70]="F",[71]="G",[72]="H",
    [73]="I",[74]="J",[75]="K",[76]="L",[77]="M",[78]="N",[79]="O",[80]="P",
    [81]="Q",[82]="R",[83]="S",[84]="T",[85]="U",[86]="V",[87]="W",[88]="X",
    [89]="Y",[90]="Z",
}

function input.get()
    local raw = (input.HeldKeys and input.HeldKeys()) or {}
    local out = {}
    for code, v in pairs(raw) do
        if v then
            local name = QT[code]
            if name then out[name] = true end
        end
    end
    return out
end

end -- do block: no top-level locals may escape (see the header note)
