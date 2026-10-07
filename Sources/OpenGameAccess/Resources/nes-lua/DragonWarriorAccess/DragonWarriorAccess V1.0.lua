local function current_script_dir()
    local source = debug.getinfo(1, "S").source or ""
    if source:sub(1, 1) == "@" then
        source = source:sub(2)
    end
    source = source:gsub("\\", "/")
    return source:match("^(.*)/[^/]+$") or "."
end

local ROOT_DIR = current_script_dir()
local DATA_DIR = ROOT_DIR .. "/Data"
local GLOBAL_KEYS = {
    Delete = true,
    G = true,
    H = true,
}

local FILES = {
    menus = DATA_DIR .. "/984723964987264.lua",
    pathfinder = DATA_DIR .. "/281649378564120.lua",
}

local function load_runtime(path, label)
    local chunk, err = loadfile(path)
    if not chunk then
        error(string.format("[%s] failed to load %s", label, tostring(err)))
    end

    local runtime = chunk()

    if type(runtime) ~= "table" or type(runtime.run_frame) ~= "function" then
        error(string.format("[%s] did not return a runtime", label))
    end

    return runtime
end

local function try_load_runtime(path, label)
    local ok, runtime_or_err = pcall(load_runtime, path, label)
    if not ok then
        console.log(string.format("[DragonWarriorAccess V1.0] %s", tostring(runtime_or_err)))
        return nil, runtime_or_err
    end
    console.log(string.format("[DragonWarriorAccess V1.0] loaded %s runtime from %s", label, path))
    return runtime_or_err, nil
end

local menu_runtime, menu_error = try_load_runtime(FILES.menus, "menus")
local pathfinder_runtime, pathfinder_error = try_load_runtime(FILES.pathfinder, "pathfinder")
local last_keys = {}

local function key_pressed(keys, name)
    return keys[name] and not last_keys[name]
end

local function filtered_keys(keys)
    local copy = {}
    for name, value in pairs(keys or {}) do
        copy[name] = value
    end
    for name in pairs(GLOBAL_KEYS) do
        copy[name] = nil
    end
    return copy
end

while true do
    if not menu_runtime or not pathfinder_runtime then
        if menu_error then
            console.log(string.format("[DragonWarriorAccess V1.0] menu runtime unavailable: %s", tostring(menu_error)))
            menu_error = nil
        end
        if pathfinder_error then
            console.log(string.format("[DragonWarriorAccess V1.0] pathfinder runtime unavailable: %s", tostring(pathfinder_error)))
            pathfinder_error = nil
        end
        emu.frameadvance()
        goto continue
    end

    local keys = input.get()

    if key_pressed(keys, "Delete") and type(menu_runtime.toggle_assist_menu) == "function" then
        menu_runtime.toggle_assist_menu()
    elseif key_pressed(keys, "G") and type(menu_runtime.speak_gold_amount) == "function" then
        menu_runtime.speak_gold_amount()
    elseif key_pressed(keys, "H") and type(menu_runtime.speak_status_summary) == "function" then
        menu_runtime.speak_status_summary()
    end

    _G.DWA_MENU_KEYS = filtered_keys(keys)
    _G.DWA_PATHFINDER_KEYS = filtered_keys(keys)

    menu_runtime.run_frame()
    _G.DWA_NON_GAMEPLAY_ACTIVE = type(menu_runtime.is_non_gameplay_active) == "function"
        and menu_runtime.is_non_gameplay_active()
        or false
    pathfinder_runtime.run_frame()

    last_keys = keys
    emu.frameadvance()
    ::continue::
end
