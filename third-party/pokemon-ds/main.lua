-- main.lua — THE LOADER. This is the script you open; it is the only one you ever open.
--
-- It does exactly one job: work out which game is running, then hand over to the file that
-- reads that game. One file per GAME SET (Ola, 2026-09-04) — Black and White share games/bw.lua,
-- Diamond and Pearl will share games/dp.lua, Platinum gets games/platinum.lua, and so on down the
-- roadmap. Nothing game-specific lives here, and nothing here needs touching to add a game
-- except one row in the GAMES table below.
--
-- HOW TO RUN: load a supported game in our EmuHawk, then Lua Console -> Open Script -> this file.
--
-- WHY THIS SPLIT IS POSSIBLE AT ALL. For a year this project shipped as one 23,000-line file
-- because main.lua's header asserted that BizHawk's `require`/`dofile` path handling was too
-- fragile to survive being split. That assertion was never checked, and it was wrong. BizHawk's
-- own source:
--   * LuaConsole.cs:940     item.Start(LuaImp.SpawnCoroutineAndSandbox(item.Path))
--   * LuaLibraries.cs:326   LuaSandbox.CreateSandbox(thread, Path.GetDirectoryName(file))
--   * LuaSandbox.cs:53      Sandbox() sets the OS working directory to that folder around EVERY
--                           resume, and restores it afterwards.
--   * EnvironmentSandbox.cs is a no-op stub — nothing strips dofile/loadfile/require away.
-- So while our code runs, the working directory is already this file's own folder, and a plain
-- relative path resolves. Ola confirmed it in the running emulator on 2026-09-04 (Lua 5.4;
-- loadfile of a subfolder path returned a function and the argument passed through) — the source
-- reading and the live check agree, which is why we then moved 23,000 lines onto it.
--
-- THE OTHER PRIZE, worth knowing before you add a game: each file loaded this way is its OWN
-- CHUNK, so it gets its own fresh 200-local budget. The ceiling the Pokédex reader hit in
-- games/bw.lua cannot follow us into a new game file.

------------------------------------------------------------------- GAMES -----
-- ROM header game code -> the file that reads it, and the name that file is handed.
-- The code is the four bytes at header offset 0x0C. Every one below was read off Ola's own
-- cartridge dumps, not looked up.
--
-- A game with NO row here is unknown to us. A game whose row points at a file that does not
-- exist yet is known but not written yet, and says so — those are the ones on the roadmap.
--
-- NAMING (Ola, 2026-09-04): a game-set file takes the community's own abbreviation for that set —
-- bw, dp, hgss, bw2 (and dpp if Platinum were ever folded in with Diamond and Pearl, which it is
-- not). A game that stands alone gets its full name instead, which is why Platinum is platinum.lua.
local GAMES = {
	-- Gen V — Black and White. Done, both games first-class.
	IRAO = { file = "games/bw.lua", game = "white",    label = "Pokemon White"    },
	IRBO = { file = "games/bw.lua", game = "black",    label = "Pokemon Black"    },
	-- Gen IV — in progress. Platinum is the lead target; Diamond and Pearl follow as one port
	-- round (they share all 87 overlay bases and differ in only two file IDs).
	CPUE = { file = "games/platinum.lua", game = "platinum", label = "Pokemon Platinum" },
	ADAE = { file = "games/dp.lua", game = "diamond",  label = "Pokemon Diamond"  },
	APAE = { file = "games/dp.lua", game = "pearl",    label = "Pokemon Pearl"    },
	-- On the roadmap, no reader yet:
	IPKE = { file = "games/hgss.lua", game = "heartgold",  label = "Pokemon HeartGold"  },
	IPGE = { file = "games/hgss.lua", game = "soulsilver", label = "Pokemon SoulSilver" },
	IREO = { file = "games/bw2.lua", game = "black2",     label = "Pokemon Black 2"    },
	IRDO = { file = "games/bw2.lua", game = "white2",     label = "Pokemon White 2"    },
}

------------------------------------------------------------------ DETECT -----
-- The DS header carries the game code at 0x0C. Reading it through the "ROM" domain is a DS
-- thing; when the roadmap reaches the 3DS core this is the one function that grows a platform
-- branch, because everything below it works off the code string alone.
local function rom_code()
	local ok, code = pcall(function()
		local c = {}
		for i = 0, 3 do c[i + 1] = string.char(memory.read_u8(0x0C + i, "ROM")) end
		return table.concat(c)
	end)
	return ok and code or nil
end

-- Fallback for when the ROM domain is missing or unreadable: identify the game by the loaded file's
-- name instead.
--
-- ⛔ THIS IS THE DANGEROUS PATH, so it fails closed. Getting it wrong does not merely stay quiet —
-- it loads ANOTHER GAME'S absolute addresses, which then read nonsense and (with INSTANT_TEXT) get
-- WRITTEN to. The first version of this table checked the substring "black 2" before "black", which
-- looked careful and was still wrong: Ola's actual dump is named "Pokemon - Black Version 2 (USA,
-- Europe)", which does not contain "black 2" at all, so Black 2 fell through to plain Black. The
-- test that missed it used a name I had invented rather than one that exists on his disk.
-- Hence three rules here:
--   1. It must look like a Pokémon ROM at all ("Black Sigil" is not Black).
--   2. Sequels are matched in BOTH orderings the dumps use — "Black Version 2" and "Black 2".
--   3. If the name names two different games, or none, return nil. Ambiguity NEVER resolves to a
--      base game; a silent loader that says why beats a confident reader on wrong addresses.
local function code_from_name()
	local ok, name = pcall(function() return gameinfo.getromname() end)
	if not ok or type(name) ~= "string" then return nil end
	local n = string.lower(name)
	if not n:find("pok", 1, true) then return nil end        -- rule 1

	local hits = {}
	local function claim(code) hits[#hits + 1] = code end
	-- rule 2: "%s*version%s*2" catches "Black Version 2"; "%s*2" catches "Black 2". Neither matches
	-- a plain "Black Version (USA) (Rev 2)", because there the 2 is not adjacent to the title.
	if     n:match("black%s*2") or n:match("black%s*version%s*2") then claim("IREO")
	elseif n:find("black", 1, true)                               then claim("IRBO") end
	if     n:match("white%s*2") or n:match("white%s*version%s*2") then claim("IRDO")
	elseif n:find("white", 1, true)                               then claim("IRAO") end
	if n:find("heartgold",  1, true) then claim("IPKE") end
	if n:find("soulsilver", 1, true) then claim("IPGE") end
	if n:find("platinum",   1, true) then claim("CPUE") end
	if n:find("diamond",    1, true) then claim("ADAE") end
	if n:find("pearl",      1, true) then claim("APAE") end

	if #hits == 1 then return hits[1] end                     -- rule 3
	return nil
end

------------------------------------------------------------------- SPEAK -----
-- The tool speaks only game content (the SEAMLESS rule), and the game file owns the single
-- startup line. So the loader stays silent when it succeeds. It speaks ONLY when it cannot hand
-- over — because the alternative is a blind player sitting in front of a script that is doing
-- nothing, with no way to tell that from a quiet moment in the game.
local function fail(spoken, logged)
	console.log("[loader] " .. (logged or spoken))
	if speech and speech.say then pcall(speech.say, spoken, true) end
end

-------------------------------------------------------------------- LOAD -----
local code = rom_code() or code_from_name()
if not code then
	return fail("Could not identify the game.",
	            "could not read the ROM header, and the ROM name matched nothing known")
end

local entry = GAMES[code]
if not entry then
	return fail("This game is not supported.", "unknown ROM code " .. code)
end

-- Does the file exist? Separating "not written yet" from "written but broken" matters: the first is
-- a roadmap answer and the second is a bug, and telling Ola the wrong one sends him looking in the
-- wrong place. So only a genuine "no such file" (errno 2) earns "not supported yet"; a reader that
-- exists but cannot be opened — packaging, permissions, a bad working directory — is a DEFECT and
-- says so.
-- The io.open call is wrapped in a closure, not passed to pcall directly: `pcall(io.open, ...)`
-- evaluates `io.open` BEFORE pcall runs, so if `io` were ever absent the guard would raise the very
-- error it exists to catch. Deferring it into the function body is what actually protects it.
local okio, fh, oerr, ocode = pcall(function() return io.open(entry.file, "r") end)
if okio and fh then
	fh:close()
elseif okio and ocode == 2 then                      -- ENOENT: simply not written yet
	return fail(entry.label .. " is not supported yet.",
	            entry.label .. " (" .. code .. ") recognised, but " .. entry.file .. " does not exist yet")
else
	return fail("The reader for this game could not be opened.",
	            "opening " .. entry.file .. " failed: " .. tostring(okio and oerr or fh) ..
	            " (code " .. tostring(okio and ocode or "io unavailable") .. ")")
end

local chunk, err = loadfile(entry.file)
if not chunk then
	return fail("The reader for this game failed to load.", "loadfile(" .. entry.file .. ") failed: " .. tostring(err))
end

console.log("[loader] " .. entry.label .. " (" .. code .. ") -> " .. entry.file)

------------------------------------------------------------------- CORE ------
-- `core("name")` loads scripts/core/<name>.lua and returns what it returns, once. The core folder
-- holds the parts that are the SAME FOR EVERY GAME (Ola, 2026-09-04) — reading memory, speaking,
-- hotkeys, the controller layer, synthetic touch — each in its own file, so a new game inherits
-- them instead of reimplementing them.
--
-- It lives HERE rather than in each game file for the reason the folder exists at all: otherwise
-- every game file would carry its own copy of the bootstrap, which is the duplication we are
-- removing. The game file receives it as its second chunk argument.
--
-- ⛔ IT FAILS LOUDLY, NOT SILENTLY. A missing or broken core file means the reader cannot work at
-- all, and a blind player cannot tell a dead script from a quiet moment in the game — so it speaks
-- before it raises, exactly as every other failure path in this loader does.
local core_cache = {}
local function core(name)
	local mod = core_cache[name]
	if mod ~= nil then return mod end
	local path = "core/" .. name .. ".lua"
	local fn, lerr = loadfile(path)
	if not fn then
		fail("An accessibility core file is missing.", "loadfile(" .. path .. ") failed: " .. tostring(lerr))
		error(path .. ": " .. tostring(lerr), 0)
	end
	mod = fn()
	if mod == nil then
		fail("An accessibility core file is broken.", path .. " returned nothing")
		error(path .. " returned nothing", 0)
	end
	core_cache[name] = mod
	return mod
end

-- Hand over. The game file runs its own frame loop and never returns, so nothing follows this.
-- Deliberately NOT wrapped in pcall: emu.frameadvance() yields this coroutine every frame, and a
-- plain Lua call is transparent to that in a way a guard would only risk breaking. A runtime
-- error inside the game file therefore behaves exactly as it did before the split — BizHawk
-- catches it and prints it to the Lua console.
chunk(entry.game, core)

-- Nothing should ever get here: a game file runs its own frame loop and never returns. If one does,
-- the tool is now doing nothing, and to a blind player that is indistinguishable from a quiet moment
-- in the game — the worst failure this tool has. So say so.
-- ⚠ NOT YET COVERED, and deliberately: a runtime error *inside* the reader still reaches only
-- BizHawk's console. Wrapping the call in pcall is the obvious fix and is NOT safe to assume —
-- emu.frameadvance() yields this coroutine through NLua's Thread.Yield -> State.Yield(0), i.e.
-- lua_yield with NO continuation (ExternalProjects/NLua/src/LuaThread.cs:81), and whether that
-- survives an enclosing pcall has to be measured in the real emulator, not argued from the source.
-- If it does not, adding the guard would kill every script instead of rescuing one. Needs a probe.
fail("The reader stopped.", "the game file returned instead of running its frame loop")
