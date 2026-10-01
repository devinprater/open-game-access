-- core/dump.lua — writing a diagnostic dump to a file, and saying so
--
-- SHARED BY EVERY GAME. Nothing in this file knows which Pokémon game is running; if you find
-- yourself wanting a game-specific fact in here, it belongs in the game file instead.
-- Loaded by a game file with:  local dump = core("dump")
--
-- ⭐ WHY A FILE AND NOT THE CONSOLE (Ola, 2026-09-05: "the dump does not say dumped zone x, making me
-- confused on if I actually dumped or not ... maybe the dump thing should be in the core folder?").
-- Two separate problems, and he named both:
--   1. A dump that only writes to the Lua console gives a blind player NO WAY TO TELL IT HAPPENED.
--      He has to go and find the console, scroll it with a screen reader, and hope. A dump he cannot
--      confirm is a dump he cannot trust, and he will press the key again and again wondering.
--   2. A console is a bad place to READ a long dump. A file he can open in an editor and move through
--      at his own pace is the whole reason Black and White's building dump has always written one.
-- So: the data goes to a file, and exactly one short line is SPOKEN to confirm it.
--
-- ⚠ THIS IS THE ONE PLACE THE SEAMLESS RULE BENDS, deliberately. The tool otherwise speaks only the
-- game's own content and never its own state — but a diagnostic that cannot say "I ran" is not a
-- diagnostic, and this key exists only for the times something is already wrong. It is also the only
-- thing in the file that speaks at all; everything else here just writes.
--
-- The confirmation QUEUES rather than interrupts: pressing the dump key does not replace anything the
-- player was in the middle of hearing, it adds to it.

-- Where dumps go. One file per session-ish (appended), beside the other dev files Ola already knows.
local DIR = "C:/Users/Amethyst/"

-- Append `lines` (a list of strings) to `name`, then say `what` so the player knows it happened.
-- Returns true on success. On failure it says so out loud too — a silent failure here is the exact
-- confusion this module exists to remove.
local function write(name, lines, what, say)
	local path = DIR .. name
	local f = io.open(path, "a")
	if not f then
		if say then say("Dump failed: cannot write " .. name, false) end
		return false
	end
	f:write(table.concat(lines, "\n") .. "\n")
	f:close()
	if say then say("Dumped " .. what, false) end
	return true
end

return { write = write, dir = DIR }
