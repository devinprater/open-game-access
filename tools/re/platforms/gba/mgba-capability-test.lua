-- mgba-capability-test.lua — determine what mGBA's Lua API actually provides.
--
-- ⛔ WHY THIS RUNS BEFORE ANY SHIM IS WRITTEN. The 166-file Pokémon Access reader set is
-- structured as MODULES ACROSS DIRECTORIES (gb.lua requires game/common/gsc.lua, which
-- pulls per-language files). If mGBA's Lua cannot load sibling files, the reader set
-- cannot run as-is and needs a packaging step. Discovering that halfway through writing a
-- shim would waste the shim. So: establish the host API surface FIRST, empirically, from
-- the emulator itself rather than from documentation.
--
-- It writes findings to a file because mGBA's Lua stdout is not reliably visible, and
-- prints the same information for the console.

local out = {}
local function say(s)
  out[#out + 1] = s
end

say("=== mGBA Lua capability report ===")
say("_VERSION: " .. tostring(_VERSION))

-- 1. which globals exist?
local interesting = {
  "emu", "memory", "joypad", "console", "savestate", "movie", "client",
  "callbacks", "gui", "bit", "table", "string", "io", "os", "loadfile",
  "dofile", "require", "package", "debug", "ffi", "socket",
}
say("--- globals ---")
for _, name in ipairs(interesting) do
  say(string.format("  %-12s %s", name, type(_G[name])))
end

-- 2. if there is a console/emu table, enumerate its members: this is the real API surface
local function members(tblName)
  local t = _G[tblName]
  if type(t) ~= "table" and type(t) ~= "userdata" then return end
  say("--- members of " .. tblName .. " ---")
  local ok, err = pcall(function()
    local keys = {}
    -- userdata tables in mGBA are usually metatable-backed; try both paths.
    for k, v in pairs(t) do keys[#keys + 1] = tostring(k) .. " (" .. type(v) .. ")" end
    if #keys == 0 then
      local mt = getmetatable(t)
      if mt and mt.__index then
        if type(mt.__index) == "table" then
          for k, v in pairs(mt.__index) do keys[#keys + 1] = tostring(k) .. " (" .. type(v) .. ")" end
        else
          keys[#keys + 1] = "__index is a function"
        end
      end
    end
    table.sort(keys)
    for _, k in ipairs(keys) do say("    " .. k) end
    if #keys == 0 then say("    (no enumerable members)") end
  end)
  if not ok then say("    error enumerating: " .. tostring(err)) end
end

members("emu")
members("console")
members("memory")
members("joypad")
members("savestate")

-- 3. can it load a sibling file? THE critical question for the reader set.
say("--- file loading ---")
local testPath = os.getenv("MGBATEST_DIR") or "."
say("  cwd-ish testPath: " .. testPath)

local function try(label, fn)
  local ok, err = pcall(fn)
  say(string.format("  %-28s %s", label, ok and "OK" or ("FAILED: " .. tostring(err))))
  return ok
end

try("loadfile on .lua", function()
  local f = loadfile(testPath .. "/probe_sibling.lua")
end)
try("dofile", function() dofile(testPath .. "/probe_sibling.lua") end)
try("require with package.path", function()
  package.path = testPath .. "/?.lua;" .. package.path
  require("probe_sibling")
end)
try("io.open read", function()
  local f = io.open(testPath .. "/probe_sibling.lua", "r")
  if not f then error("io.open returned nil") end
  f:close()
end)

-- 4. memory access shapes worth probing (the shim needs these)
say("--- memory access probes (wrapped in pcall; absence is information) ---")
try("emu:read8", function() if emu and emu.read8 then emu:read8(0) else error("no emu.read8") end end)
try("emu.read8", function() if emu and emu.read8 then emu.read8(0) else error("no emu.read8") end end)
try("memory.readbyte", function() if memory and memory.readbyte then memory.readbyte(0) else error("no memory.readbyte") end end)
try("console:log", function() if console and console.log then console:log("probe") else error("no console.log") end end)

-- 5. callbacks: does it have a frame loop hook?
say("--- callbacks ---")
try("callbacks.add frame", function()
  if callbacks and callbacks.add then callbacks:add("frame", function() end)
  else error("no callbacks.add") end
end)
try("emu:onFrame", function() if emu and emu.onFrame then emu:onFrame(function() end) else error("no emu.onFrame") end end)

-- write the report
local path = (os.getenv("MGBATEST_OUT") or ".") .. "/mgba-capabilities.txt"
local f = io.open(path, "w")
if f then
  f:write(table.concat(out, "\n"), "\n")
  f:close()
end

-- mGBA has no print(); emit through whatever exists.
if console and console.log then
  for _, line in ipairs(out) do console:log(line) end
elseif print then
  print(table.concat(out, "\n"))
end
