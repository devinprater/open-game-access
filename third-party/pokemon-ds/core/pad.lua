-- core/pad.lua — the controller accessibility layer: hold a trigger, drive the readers with the pad.
--
-- SHARED BY EVERY GAME. Nothing here knows which Pokémon game is running; the only game-specific
-- thing — which screens the right trigger drives — is REGISTERED by the game file (M.register_rt).
-- Loaded by a game file with:  local pad = core("pad")
--
-- MOVED here from games/bw.lua on 2026-09-04, verbatim apart from that registration. The comments are
-- the expensive part: every claim about BizHawk's internals was verified against BizHawk's own source,
-- and the traps called out below each cost a test round.
local M = { rt_screens = {} }

-- Ola's design (2026-08-04): HOLD THE LEFT TRIGGER to turn the pad into an accessibility layer. While it
-- is held the game receives NO input at all, and the pad drives whatever list is currently live (the
-- overworld nav list out in the field, the battle HP list in a battle, anything we add later).
--
-- WHY THIS WORKS (verified in BizHawk's own source, not assumed — the DS has no L2 to bind the trigger to):
--   * READ:  input.get() returns RAW HOST inputs by name, filled in InputManager.ProcessInput BEFORE any
--            binding or hotkey lookup (HostInputCoalescer.Receive, InputManager.cs:173 -> InputApi.cs:21).
--            So an unbound trigger still reaches Lua. Names come from SDL2Gamepad.cs: an SDL-recognised
--            game controller gets the prefix "X1 " (Ola's pad does — confirmed live with pad-probe.lua),
--            a generic joystick would get "J1 " with B1/POV0U-style names instead.
--            The trigger is DIGITISED for us at 32768/6.5 (~15% pull, SDL2Gamepad.cs:179) -> a plain on/off
--            modifier, no analog handling needed.
--   * BLOCK: joypad.set{X=false} writes a hard override (JoypadApi.cs:69) that the controller chain applies
--            AFTER the physical latch (InputManager.cs:271, latch at :240), so the core never sees the press.
--            Overrides are wiped every frame (MainForm.cs:2944) and this script's loop runs after that wipe
--            (UpdateToolsBefore, :2952) and before the core reads input (FrameAdvance, :3017) -> re-asserting
--            the block each frame from our own loop lands on exactly the right frame.
--
-- ⛔ THE FACE-BUTTON NAMES ARE THE XBOX/SDL LAYOUT, NOT THE DS LAYOUT. This is the "two small ID spaces are
-- not the same ID space" trap in pad form: SDL normalises every pad to the 360 layout, so its Y is the TOP
-- face button and its X is the LEFT one — the exact opposite of the DS, where X is on top. Ola asked for the
-- TOP face button (called X on a DS), so the host name we want is "X1 Y". Never map these by letter.
local PAD         = "X1 "                    -- host-input prefix for Ola's pad (see above)
local PAD_MOD     = PAD .. "LeftTrigger"     -- the modifier: hold it to enter the accessibility layer
local PAD_MOD2    = PAD .. "RightTrigger"    -- RESERVED (Ola, Q3) for a second layer — battles. Unused today:
                                             -- nothing is bound to it and it blocks nothing, so it stays free
                                             -- until we give it actions.
-- Pad button -> the virtual key the readers already listen for. Mapping to the EXISTING letters is what
-- keeps this modular: every reader (overworld nav, battle HP, and whatever comes next) is untouched and
-- automatically gains controller support, and the keyboard keys keep working exactly as before.
-- One table, one entry per LAYER. (It was one table because games/bw.lua sat at Lua's 200-local cap;
-- here it is one table because keeping the two maps side by side makes them easy to compare.)
M.ACTIONS = {
	LT = {                            -- LEFT trigger: the general accessibility layer
		[PAD .. "DpadLeft"]  = "I",   -- previous category  (battle: your side)
		[PAD .. "DpadRight"] = "O",   -- next category      (battle: enemy side)
		[PAD .. "DpadUp"]    = "J",   -- previous item
		[PAD .. "DpadDown"]  = "L",   -- next item
		[PAD .. "LeftThumb"] = "P",   -- pathfind to the selected item, press again to stop (L3 = left stick click)
		[PAD .. "Y"]         = "C",   -- place name + coordinates      (SDL Y = the TOP face button, DS "X")
	},
	-- RIGHT trigger: screens we drive with SYNTHETIC TOUCH (Ola, 2026-08-19). Same letters as the left
	-- layer on purpose — up and down step a list, left and right pick between choices everywhere in this
	-- tool, so the Musical inherits the habit — plus one new letter for "do it".
	-- ⚠ THE FACE-BUTTON NAMES ARE THE XBOX/SDL LAYOUT, NOT THE DS ONE (the same trap as the note above):
	-- Ola asked for the DS's A, the RIGHT-hand face button, and SDL calls that one B.
	RT = {
		[PAD .. "DpadUp"]    = "J",   -- previous prop
		[PAD .. "DpadDown"]  = "L",   -- next prop
		[PAD .. "DpadLeft"]  = "I",   -- previous action
		[PAD .. "DpadRight"] = "O",   -- next action
		[PAD .. "B"]         = "E",   -- confirm  (SDL B = the RIGHT face button = the DS's A)
	},
}

-- Register a screen that the RIGHT-trigger layer drives. `up` is a predicate the game file supplies;
-- `blocking` says whether the game's own input must be shut off while that screen is live.
-- ⚠ blocking is NOT a detail. The Musical reads no buttons of its own, so its layer stays open; the
-- fly map reads the D-pad for its own cursor and A for its own confirm, so a non-blocking layer there
-- sent every chord to the game as well and the cursor walked away from the place we had just
-- announced (codex-review 2026-08-24, confirmed).
function M.register_rt(up, blocking)
	M.rt_screens[#M.rt_screens + 1] = { up = up, blocking = blocking and true or false }
end

-- The per-frame step (the design and the BizHawk evidence are above).
-- Call once per frame BEFORE dispatching to the modules. Two jobs:
--   1. While the trigger is held, translate pad presses into the virtual keys the readers already use.
--   2. Keep the game's own buttons shut, and keep them shut past the release.
-- ⛔ THE RELEASE LATCH (Ola, Q4). Letting go of the trigger while still holding a direction must NOT hand
-- that direction to the game — you would take a step you never asked for. So on release we keep blocking
-- whatever is STILL physically down, until it is genuinely let go. Reading "still physically down" needs
-- care: at the moment our loop runs, the emulator's controller already has LAST frame's overrides baked
-- into it (RunControllerChain applies them at MainForm.cs:1004, and the wipe at :2944 only empties the
-- adapter, not the controller). A naive joypad.getimmediate() therefore reports our own blocked buttons as
-- released and the latch would never engage. joypad.set{} first UnSets every override AND re-latches the
-- controller straight from the physical input (JoypadApi.cs:68/79), so the read that follows is the true
-- state of the pad. Doing it through the emulator like this keeps us binding-AGNOSTIC: we never need to
-- know which pad button Ola has bound to which DS button.
-- ⛔ AND WE ALWAYS OWE ONE CLEARING PASS AFTER THE LAST BLOCKED FRAME (codex-review 2026-08-04, confirmed
-- against the source). An override does NOT expire on its own: RunControllerChain applies whatever is in
-- the adapter (MainForm.cs:1004) BEFORE the wipe at :2944, and the rest of the chain only reads THROUGH to
-- ActiveController — MovieSession.LatchInputToUser is literally `MovieOut.Source = MovieIn`
-- (MovieSession.cs:321), no re-latch anywhere. So a frame on which we call no joypad function at all
-- inherits the PREVIOUS frame's block, and the frame right after you let go of the trigger would eat your
-- input. It only showed up when NOTHING was latched (with something latched we call joypad.set anyway and
-- self-heal), which is the common case of tapping the trigger while touching nothing else. pad_dirty
-- tracks whether we wrote anything last frame and buys back the one clearing call we owe.
local pad_latched = {}     -- DS buttons held shut past the trigger's release, until physically let go
local pad_dirty   = false  -- did we write overrides last frame? (drives the clearing pass described above)
local pad_buttons = nil    -- every bool button the CORE has, asked once (see the Q6 note in the config)
function M.step(pressed, held)
	local mod_held = held[PAD_MOD] and true or false

	if mod_held then
		for host, key in pairs(M.ACTIONS.LT) do
			if pressed[host] then pressed[key] = true end
		end
	end
	-- SECOND LAYER — the RIGHT trigger, for screens we drive with synthetic touch (Ola, 2026-08-19). The
	-- Musical's half is deliberately NOT blocking: that screen reads no buttons at all, so there is nothing
	-- to block, and gating it on the screen being up keeps the right trigger free everywhere else.
	-- ⛔ THE FLY MAP BREAKS THAT ASSUMPTION AND SO IT DOES BLOCK (codex-review 2026-08-24, MEDIUM,
	-- confirmed). Unlike the Musical, the town map reads the D-pad for its own free cursor and reads A for
	-- its own confirm — so on a non-blocking layer every RT chord ALSO reached the game: stepping to a place
	-- the cursor already held queued no gesture at all and simply moved the cursor away from the destination
	-- we had just announced, a direction still held when an eight-frame gesture ended came back as a fresh
	-- native press, and a physical A went straight to the app's own commit. Blocking, with the left layer's
	-- release latch, is what keeps RT+D-pad "the list" and the bare D-pad "the cursor" — which is exactly
	-- what Ola asked for. The Musical keeps its non-blocking behaviour, unchanged and already confirmed.
	-- ⭐ THE SCREENS THEMSELVES ARE REGISTERED BY THE GAME FILE (M.register_rt below), because WHICH
	-- screens exist is the one genuinely game-specific thing in this layer. Each says whether it wants
	-- the layer to BLOCK; behaviour is otherwise identical to the two hard-coded screens this was
	-- extracted from, and the blocking rule is unchanged — any registered screen that asks to block
	-- makes the layer blocking, exactly as the fly map did while the Musical did not.
	local rt_any, rt_block = false, false
	if held[PAD_MOD2] then
		for _, s in ipairs(M.rt_screens) do
			local ok, up = pcall(s.up)          -- a game predicate must never kill the frame loop
			if ok and up then
				rt_any = true
				if s.blocking then rt_block = true end
			end
		end
	end
	if rt_any then
		for host, key in pairs(M.ACTIONS.RT) do
			if pressed[host] then pressed[key] = true end
		end
	end
	local blocking = mod_held or rt_block

	-- Idle: no blocking layer is held and nothing is still latched. Zero cost in the common case (walking
	-- around), except for the single clearing call owed on the first idle frame after we last blocked.
	if not blocking and next(pad_latched) == nil then
		if pad_dirty then joypad.set({}); pad_dirty = false end
		return
	end

	joypad.set({})                          -- drop our overrides + re-latch from physical (see above)
	local phys = joypad.getimmediate()      -- now the TRUE physical state of every DS button
	if not pad_buttons then                 -- bool entries are buttons, number entries are axes
		local list = {}                     -- (Emulation.Common/Extensions.cs:410-411)
		for k, v in pairs(phys) do if type(v) == "boolean" then list[#list + 1] = k end end
		if #list > 0 then pad_buttons = list end   -- never cache an empty answer (no core = try again)
	end
	local block = {}
	if blocking then
		for _, b in ipairs(pad_buttons or {}) do
			block[b] = false
			pad_latched[b] = phys[b] and true or nil   -- remember what to hold past the release
		end
	else
		for b in pairs(pad_latched) do
			if phys[b] then block[b] = false else pad_latched[b] = nil end
		end
	end
	if next(block) ~= nil then joypad.set(block); pad_dirty = true else pad_dirty = false end
end

-- The host-input prefix is exported because game files build their own chord names from it — the
-- Poké Mart money key in games/bw.lua is `PAD .. "LeftThumb"`. Exporting it keeps that one string
-- in a single place, which is the whole point of this folder.
M.PAD = PAD

return M
