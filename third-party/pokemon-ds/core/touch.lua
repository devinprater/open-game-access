-- core/touch.lua — pressing the DS touch screen from Lua
--
-- SHARED BY EVERY GAME. Nothing in this file knows which Pokémon game is running; if you find
-- yourself wanting a game-specific fact in here, it belongs in the game file instead.
-- Loaded by a game file with:  local touch = core("touch")
--
-- MOVED here from games/bw.lua on 2026-09-04, verbatim — the comments are the expensive part.
-- Every claim they make about BizHawk's internals was verified against BizHawk's own source at
-- the time, and most of them cost a test round to learn.

-- Ola cannot use the touch screen, and parts of BW demand it — the Musical dress-up tutorial blocks him
-- outright, and the Feeling Check, the C-Gear and the Xtransceiver are the same shape of problem. His call
-- (2026-08-19): build the pressing MECHANISM generally, and map screens onto it one at a time.
--
-- This is that mechanism and nothing else: it knows how to press, hold, move and release, and it knows
-- nothing about any screen. A screen module queues a gesture and watches the game's own reaction.
--
-- WHY IT IS A QUEUE AND NOT A FUNCTION CALL: the game reads the touch panel once per frame, so a gesture
-- only exists as a SEQUENCE of frames. Anything that has to be held, moved and released — which is every
-- gesture the Musical wants — must be driven from the frame loop, one step per frame.
--
-- ⛔ THE COORDINATES CANNOT GO THROUGH joypad.setanalog, AND THIS COST A WHOLE TEST ROUND (Ola,
-- 2026-08-19: every synthetic gesture left the game's interaction state at 0). setanalog writes a STICKY
-- AXIS HOLD, and the input chain is `UdLRControllerAdapter.Xor(StickyController)`, whose axis rule is:
-- physical neutral -> take sticky; sticky neutral -> take physical; BOTH NON-NEUTRAL -> RETURN NEUTRAL
-- (BitwiseAdapters.cs:50-65). The DS touch axes are bound to the mouse, so with the pointer over the window
-- both sides are non-neutral and our coordinate was replaced by the neutral one — (128, 96), the middle of
-- the screen. The press was arriving all along; it was landing on empty space.
-- So we use the one Lua path that writes axes as REAL OVERRIDES: joypad.setfrommnemonicstr parses a movie
-- mnemonic and calls ButtonOverrideAdapter.SetButton AND .SetAxis (JoypadApi.cs:49-50), and
-- Controller.Overrides() writes those into the active controller's own axis map (Controller.cs:202),
-- replacing whatever the mouse said. Overrides are wiped every frame, so a held gesture re-sends per frame.
-- The mnemonic layout is read from the code, not guessed: SetFromMnemonic walks Definition.ControlsOrdered,
-- which is AXES first then BoolButtons (ControllerDefinition.GenOrderedControls), each axis terminated by a
-- comma and each button one character with '.' = released. For this core that is
--     |TouchX,TouchY,MicVolume,GBALightSensor,<17 button characters>|
-- with the core's own button order (MelonDS.cs): Up Down Left Right Start Select B A Y X L R LidOpen
-- LidClose Touch Microphone Power — Touch is the 15th character.
-- ⚠ A mnemonic sets EVERY button, so while a gesture runs the player's own presses are suppressed. That is
-- acceptable for the handful of frames a gesture lasts, and it also keeps a stray press from disturbing it —
-- but it is why nothing is sent at all when the queue is empty.
-- The watchdog matters: if a module queues a gesture and then its screen tears down, nothing would ever
-- release the touch, and the game would see a finger pressed forever.
-- It used to live on games/bw.lua's `reg` table purely to dodge that file's 200-local ceiling. Here it
-- is its own chunk with its own budget, so it is a plain module — but a game file can still park it on
-- `reg` (bw.lua does) and every existing call site keeps working unchanged.
local M = { q = nil, i = 0, held = 0, stamp = -1 }
-- Busy in EITHER mode — a queued gesture or a hold being driven by hand. A future screen module asking
-- "is the touch free?" must not be told yes while a finger is down (codex-review 2026-08-19).
function M.busy() return M.q ~= nil or M.held > 0 end
function M.mnemonic(x, y, down)
	local b = {}
	for i = 1, 17 do b[i] = "." end
	if down then b[15] = "T" end                            -- 15th button character = Touch
	return string.format("|%d,%d,100,0,%s|", x, y, table.concat(b))
end
function M.cancel()
	local was = M.held > 0
	M.q, M.i, M.held = nil, 0, 0
	if was then joypad.setfrommnemonicstr(M.mnemonic(128, 96, false)) end   -- one released frame
	joypad.setanalog({ ["Touch X"] = "", ["Touch Y"] = "" })   -- and never leave a sticky hold behind
end
-- A gesture is a list of frames: {x, y} for "pressed there", false for "released".
function M.gesture(frames) M.q, M.i = frames, 0; return true end
function M.tap(x, y, hold)
	local g = { false }                                     -- one released frame first, so the press is an EDGE
	for _ = 1, (hold or 6) do g[#g + 1] = { x, y } end
	g[#g + 1] = false
	return M.gesture(g)
end
-- Press at (x1,y1), travel to (x2,y2), release there. This is the shape the Musical needs: what makes it a
-- pickup is the MOVEMENT while held, not how long it is held.
function M.drag(x1, y1, x2, y2, steps)
	local n = steps or 6
	local g = { false, { x1, y1 }, { x1, y1 }, { x1, y1 } }
	for i = 1, n do
		g[#g + 1] = { math.floor(x1 + (x2 - x1) * i / n), math.floor(y1 + (y2 - y1) * i / n) }
	end
	for _ = 1, 3 do g[#g + 1] = { x2, y2 } end
	g[#g + 1] = false
	return M.gesture(g)
end
-- TWO WAYS TO USE THIS. A simple press is a QUEUED gesture (tap/drag above). Anything whose next
-- coordinate depends on what the game did — picking a prop up and only then learning where it may be put —
-- drives the frames itself with hold()/release(), one call per frame, and step() stays out of the way. The
-- stamp is what tells the watchdog "a module is holding this on purpose this frame" from "nobody is driving
-- it any more", which is the difference between a gesture and a finger stuck to the screen.
function M.hold(x, y)
	M.q = nil                                               -- one owner at a time: driving frames by
	joypad.setfrommnemonicstr(M.mnemonic(x, y, true))       -- hand supersedes any queued gesture
	M.held, M.stamp = M.held + 1, emu.framecount()
end
function M.release()
	M.q = nil
	joypad.setfrommnemonicstr(M.mnemonic(128, 96, false))
	M.held, M.stamp = 0, emu.framecount()
end
function M.step()
	if not M.q then
		if M.held > 0 and M.stamp ~= emu.framecount() then M.cancel() end
		return                                                      -- (watchdog: never leave a finger down)
	end
	M.i = M.i + 1
	local f = M.q[M.i]
	if f then
		joypad.setfrommnemonicstr(M.mnemonic(f[1], f[2], true))
		M.held = M.held + 1
	else
		joypad.setfrommnemonicstr(M.mnemonic(128, 96, false))
		M.held = 0
	end
	if M.i >= #M.q then M.cancel() end
end

return M
