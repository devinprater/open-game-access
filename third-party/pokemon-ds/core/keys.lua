-- core/keys.lua — edge-detected keyboard/host hotkeys
--
-- SHARED BY EVERY GAME. Nothing in this file knows which Pokémon game is running; if you find
-- yourself wanting a game-specific fact in here, it belongs in the game file instead.
-- Loaded by a game file with:  local keys = core("keys")
--
-- MOVED here from games/bw.lua on 2026-09-04, verbatim — the comments are the expensive part.
-- Every claim they make about BizHawk's internals was verified against BizHawk's own source at
-- the time, and most of them cost a test round to learn.

-- Edge-detected hotkeys: returns { [key]=true } for keys that went DOWN this frame.
-- held_keys is the same poll's UNEDGED state (what is down right now) — the controller mod layer needs it,
-- because a modifier is about being HELD, not about the frame it was pressed.
local prev_keys = {}
local held_keys = {}
local function poll_keys()
	local now = input.get()
	local pressed = {}
	for k in pairs(now) do
		if not prev_keys[k] then pressed[k] = true end
	end
	prev_keys = now
	held_keys = now
	return pressed
end

-- What is physically down RIGHT NOW, unedged. The controller layer needs this rather than `pressed`,
-- because a modifier is about being HELD, not about the frame it went down.
local function held() return held_keys end

return { poll = poll_keys, held = held }
