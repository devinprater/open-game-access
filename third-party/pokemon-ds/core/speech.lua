-- core/speech.lua — one place that talks, and the interrupt-vs-queue rule
--
-- SHARED BY EVERY GAME. Nothing in this file knows which Pokémon game is running; if you find
-- yourself wanting a game-specific fact in here, it belongs in the game file instead.
-- Loaded by a game file with:  local speech = core("speech")
--
-- MOVED here from games/bw.lua on 2026-09-04, verbatim — the comments are the expensive part.
-- Every claim they make about BizHawk's internals was verified against BizHawk's own source at
-- the time, and most of them cost a test round to learn.
-- ⛔ THE RULE (CLAUDE.md Rule 1, as Ola sharpened it on 2026-09-04): QUEUE what is on screen
-- TOGETHER; INTERRUPT whatever has REPLACED what came before. Arriving at a new screen interrupts,
-- a second element of the SAME screen queues behind the first, a cursor move interrupts, and a
-- second PAGE of a message interrupts — because pressing A wipes the box, so page one is gone.
-- A message and menu shown together follow that rule even if they become readable on different
-- frames: the initial item queues behind the message; moving the cursor interrupts. Track the
-- landing per menu, including submenus, rather than once per whole application (2026-09-16).
-- Leaving a screen stops its current and queued speech BEFORE the next screen's readers run.
-- A menu joining a question that remains visible is a continuation, not a screen exit.
-- CONTENT ORDER (Amethyst, 2026-09-15; CLAUDE.md Rule 1): counted-item labels are
-- "10 Repel", then description/details, then the list index. No comma after the quantity.
-- Descriptions precede indices by default. Selected tabs introduce their contents and speak
-- first; the contents queue behind them. Compose this in the reader, using the game's text.
-- Pokedex tab headers say only the tab name, with no index (Amethyst's follow-up, 2026-09-15).
local last_said = ""
-- say(text[, interrupt]). interrupt omitted/true = cut current speech and speak now (default — used
-- while NAVIGATING, so fast cursor moves collapse to the latest). interrupt == false = QUEUE after
-- whatever is speaking (used for the message / edit field / what you LAND on, so they play in order
-- without cutting each other). Mirrors the project's prism queue convention (cf. the Digimon Story:
-- Time Stranger mod's SpeechManager.Speak(text, interrupt)). The readers determine which elements
-- coexist; this shared output function must not turn navigation into queued speech.
local function say(m, interrupt)
	local s = tostring(m)
	last_said = s
	console.writeline(s)
	if speech and speech.say then
		if interrupt == false then speech.say(s, false) else speech.say(s) end
	end
end
local function stop_speech()
	if speech and speech.stop then speech.stop() end
end
local current_screen
local function screen(next_screen, continue_from)
	if next_screen == current_screen then return end
	if not (continue_from and continue_from[current_screen or false]) then stop_speech() end
	current_screen = next_screen
end
-- Silent dev trail (console only, never spoken) — used for context transitions.
local function dbg(m) console.writeline("[ctx] " .. tostring(m)) end

-- The last thing said, for the repeat hotkey. An accessor rather than the bare value, because the
-- value is replaced on every line and a copy taken at load time would be frozen at "".
local function last() return last_said end

-- Set it WITHOUT speaking. A reader that has just watched the game put a line on screen by itself,
-- or that wants the repeat key to hand back something other than the last utterance, adjusts the
-- memory rather than the voice. games/bw.lua's battle level-up reader does exactly this.
local function set_last(s) last_said = tostring(s) end

return { say = say, stop = stop_speech, screen = screen, dbg = dbg, last = last, set_last = set_last }
