-- oga_audio.lua — a stubbed `audio` module for the readers' positional sound cues.
--
-- ⛔ CORRECTING AN EARLIER MISTAKE. I previously recorded that `audio.` was "never called —
-- verified by grep" and bypassed the DLL load on that basis. That grep only covered
-- pokemon.lua. Grepping the WHOLE reader set shows:
--
--     audio.play    42 calls
--     audio.stop     1
--     audio.pitch    1
--
-- They are real and they matter: they are POSITIONAL cues. The call signature is
-- `audio.play(path, ?, pan, volume)` where pan is -100..100 and volume 0..100, so the
-- reader conveys DIRECTION with sound — e.g. gb.lua:230 plays a boulder sound panned to
-- the obstacle's position, and :516 pans a menu-select sound by which row is selected.
--
-- For a screen-reader player this is secondary to speech but it is not decoration: it is
-- the same information encoded in space rather than words, and some users rely on it.
--
-- ⛔ SO THIS IS A STUB, NOT AN IMPLEMENTATION — AND THAT IS A REAL GAP TO CLOSE, NOT A
-- THING TO HIDE. Loading the original audio.dll is impossible: it is a 32-bit Windows
-- binary using the BASS audio library, bound to the host process, and mGBA's Lua cannot
-- load native modules. Providing real positional audio under mGBA means playing the WAV
-- files from the HOST side with pan applied — which is exactly what the Android bridge
-- already does with its sound callback:
--
--     script->setSoundCallback([](const char* path, int pan, int volume, void*) { ... });
--
-- So the correct fix is a host sound callback, not a Lua emulation of BASS. Until that is
-- wired, this stub records every cue so the calls can be verified and replayed later, and
-- returns without error so the reader keeps running.
--
-- The WAV files exist: `sounds/common/` and `sounds/gb/` are in the reader tree.

local log_fn = function() end

local audio_stub = {}

-- Cue log, capped so a long session cannot grow without bound.
local cues = {}
local MAX_CUES = 500

local function record(evt, path, pan, volume)
  if #cues < MAX_CUES then
    cues[#cues + 1] = { evt = evt, path = path, pan = pan, volume = volume }
  end
  log_fn(string.format("[sound] %s %s pan=%s vol=%s",
        evt, tostring(path), tostring(pan), tostring(volume)))
end

-- audio.play(path, flags, pan, volume)
-- ⛔ TOLERATE MISSING ARGS. The readers do arithmetic for pan that can go out of range or
-- produce nil when a menu has no rows (note the division by #screen.tile_lines at
-- gb.lua:516). A stub that errors on that would abort the reader for a cosmetic cue, so
-- clamp and continue instead.
function audio_stub.play(path, flags, pan, volume)
  if type(pan) == "number" then
    if pan < -100 then pan = -100 elseif pan > 100 then pan = 100 end
  else
    pan = 0
  end
  if type(volume) ~= "number" then volume = 100 end
  record("play", path, pan, volume)
  return true
end

function audio_stub.stop()
  record("stop", nil, 0, 0)
  return true
end

-- audio.pitch(value) — a global playback-rate control in the original.
function audio_stub.pitch(value)
  record("pitch", nil, value, 0)
  return true
end

-- ── handoff API ───────────────────────────────────────────────────────────────
-- The host (OGA) can install a real sink and take over delivery. Kept here rather than in
-- the bootstrap so the audio concern stays in one file.
function audio_stub.set_sink(fn) log_fn = fn or function() end end

-- Expose what was played, for tests and for a host that wants to replay cues.
function audio_stub.cues() return cues end
function audio_stub.clear() cues = {} end

_G.audio = audio_stub

-- Also register as a module: gb.lua uses the global, but `require "audio"` appears in some
-- of the reader's own boot code paths.
package.preload["audio"] = function() return audio_stub end
package.loaded["audio"]  = audio_stub

return audio_stub
