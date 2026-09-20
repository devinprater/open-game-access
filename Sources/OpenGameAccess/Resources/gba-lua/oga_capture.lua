-- oga_capture.lua — RUN THE REAL READER in mGBA and capture what it SPEAKS to a file.
--
-- This is the A5 deliverable: real spoken lines produced by the unmodified v3.1.0 reader
-- running against a real game in a real emulator — no stub host, no simulated frames.
--
-- ⛔ THE SINK NAME IS `oga_say`, read from the bootstrap's own tolk stub:
--
--     tolk = { output = function(s) if _G.oga_say then _G.oga_say(tostring(s), false) ... end }
--
-- Note the SECOND argument (interrupt flag) and that the bootstrap also falls back to
-- console:log("[speech] " .. s) when no sink is installed. Installing `oga_say` FIRST is
-- what routes speech to a file instead of the console.
--
-- Run via:  mgba.exe --script oga_capture.lua <rom>

local OUT  = "C:/Users/Devin Prater/AppData/Local/Temp/oga-speech.txt"
local LOGF = "C:/Users/Devin Prater/AppData/Local/Temp/oga-boot-log.txt"

local f  = io.open(OUT, "w")
local lf = io.open(LOGF, "w")
local function log(s) if lf then lf:write(s, "\n"); lf:flush() end end

local spoken = 0
_G.oga_say = function(text, interrupt)
  spoken = spoken + 1
  local s = tostring(text)
  if f then f:write(s .. "\n"); f:flush() end
  log("SPEAK[" .. spoken .. "] " .. s)
end

local realLog = console and console.log
if console then
  console.log = function(m) log("LOG " .. tostring(m)); if realLog then realLog(m) end end
end

log("=== capture wrapper ===")
log("frame at load: " .. tostring(emu and emu:currentFrame()))
if emu then
  log("platform: " .. tostring(emu:platform()))
  log("title: " .. tostring(emu:getGameTitle()))
  log("code: " .. tostring(emu:getGameCode()))
end

-- Give the game a moment to get past its boot screens before the reader starts,
-- so identification reads a settled cartridge state rather than frame 1.
for _ = 1, 120 do pcall(function() emu:runFrame() end) end
log("frame after warmup: " .. tostring(emu and emu:currentFrame()))

local ok, err = pcall(function()
  dofile("C:/Users/Devin Prater/Dropbox/programs/pokemon-access/lua/oga_bootstrap.lua")
end)
log("bootstrap: ok=" .. tostring(ok) .. " err=" .. tostring(err))
log("frame at end: " .. tostring(emu and emu:currentFrame()))
log("utterances captured: " .. spoken)
log("=== end ===")

if f then f:close() end
if lf then lf:close() end
