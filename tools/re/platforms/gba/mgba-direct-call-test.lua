-- oga-direct-call-test.lua — isolate READER LOGIC from INPUT DISPATCH.
--
-- The diagnostic proved: data=true, game=firered, lang=en, on_map=true, and input.read()
-- returns the scheduled key. Yet no speech. So either
--   (a) the reader's command functions themselves produce nothing/error, or
--   (b) the key-set matching in handle_user_actions never matches.
--
-- This calls the reader's own command functions DIRECTLY from inside its loop, bypassing
-- input dispatch entirely, and reports both the speech produced and any error raised.
--
-- If a direct call speaks -> reader logic is fine and the bug is in key matching.
-- If a direct call errors  -> we finally see the real error instead of silence.

local OUT = "C:/Users/Devin Prater/AppData/Local/Temp/oga-speech.txt"
local LOG = "C:/Users/Devin Prater/AppData/Local/Temp/oga-log.txt"
local f  = io.open(OUT, "w")
local lf = io.open(LOG, "w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
_G.oga_say = function(t) if f then f:write(tostring(t).."\n"); f:flush() end end

console = { log = function(_, m) w(m) end }

local REAL = emu
local function frame(n) for _ = 1, n do pcall(function() REAL:runFrame() end) end end
local function pad(k, hold) REAL:setKeys(1 << k); frame(hold or 4); REAL:setKeys(0); frame(3) end

w("=== boot to overworld ===")
frame(260)
for i = 1, 150 do pad(0, 4) end
for i = 1, 120 do pad(3, 4); pad(0, 4) end
for i = 1, 300 do pad(0, 4) end
w("=== in game at frame " .. tostring(REAL:currentFrame()) .. " ===")

-- Ask poll_hooks (inside the reader's loop) to do the direct calls, because the reader's
-- globals only exist once its chunk has run, and its loop owns the thread afterwards.
_G.oga_direct_calls = true

dofile("C:/Users/Devin Prater/Dropbox/programs/pokemon-access/lua/oga_bootstrap.lua")
w("=== reader returned ===")
