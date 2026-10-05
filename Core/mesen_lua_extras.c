/* mesen_lua_extras.c — the four symbols Mesen's Lua FORK adds that the app's stock lua-5.4.7 lacks.
 *
 * WHY THIS FILE EXISTS. Mesen's Core/Debugger/ScriptingContext.cpp is compiled into the link (the
 * Emulator owns a Debugger unconditionally, so it cannot be left out), and it references four
 * things from Mesen's own Lua tree:
 *
 *     lua_setwatchdogtimer    -- a watchdog hook, declared in Mesen's Lua/lua.h
 *     SANDBOX_ALLOW_LOADFILE  -- a global guarding `loadfile`, in Lua/lauxlib.h
 *     luaopen_socket_core     -- LuaSocket, in Lua/luasocket.h
 *     luaopen_mime_core       -- MIME support, in Lua/mime.h
 *
 * ⛔ THE OBVIOUS MOVE IS WRONG AND WOULD COST A LOT. Compiling Mesen's 52-file Lua tree would give
 * the link TWO complete Lua implementations -- Mesen's fork and the app's lua-5.4.7 -- and this repo
 * has already paid for that mistake twice (the lua and xxhash duplicate-definition bugs noted in
 * core-sources.sh). Here the situation is sharper: the app HOSTS its accessibility scripts in
 * lua-5.4.7 and hand-writes an mGBA-shaped API for readers. Mesen's Lua exists only for Mesen's
 * DEBUGGER, which this project does not use.
 *
 * So the honest thing is to satisfy the symbols without a second interpreter, and to make each stub
 * REFUSE LOUDLY rather than silently succeed:
 *
 *   * the watchdog is a debugger-only safety net (abort a runaway script). With no debugger running
 *     there is no hook to install, so storing the request and never firing it is correct -- but it
 *     says so out loud if anything ever asks.
 *   * SANDBOX_ALLOW_LOADFILE starts at 0, exactly as Mesen's own lauxlib.c initialises it. Anything
 *     that tries loadfile without setting it is DENIED -- the safe default, matching upstream.
 *   * the two module openers abort with a message, because returning a fake "opened" would hand
 *     LuaSocket to a script that then finds no functions and reports success.
 *
 * ⛔ WHEN THIS IS THE WRONG FILE TO KEEP: if a game ever needs Mesen's own scripting layer (the
 * OGA plan for that is: move readers from BizHawk to MesenCE scripting WHOLESALE, with no
 * intermediate bridge), then delete this file and compile Mesen's Lua tree instead -- and remove
 * the app's lua objects from the same link in the same commit, or the duplicate-definition bug is
 * back. That decision is a deliberate one, not a cleanup.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Mesen's Lua headers, for the exact signatures. Kept OUT of the app's include path (only this file
 * includes them), so the app's own Lua headers stay authoritative everywhere else. */
#include "lua.h"
#include "lauxlib.h"
#include "luasocket.h"
#include "mime.h"

/* ---- the watchdog ----------------------------------------------------------
 * Mesen's lua_setwatchdogtimer installs a hook that fires every `count` VM instructions. Without a
 * debugger there is nothing to fire it into, so record the request and say so once. Never pretend
 * the hook is installed: a script that believes it has a watchdog while it does not could hang the
 * app, and the log line is what turns that into a diagnosable event instead of a freeze.
 */
void lua_setwatchdogtimer(lua_State* L, lua_WatchDogHook func, int count) {
    (void) L; (void) func; (void) count;
    static bool warned = false;
    if (!warned) {
        warned = true;
        fprintf(stderr, "[mesen_lua_extras] lua_setwatchdogtimer called, but this build hosts the "
                        "app's Lua, not a debugger. No hook is installed. See mesen_lua_extras.c.\n");
    }
}

/* ---- the loadfile sandbox --------------------------------------------------
 * ⛔ INITIALISED TO 0, MATCHING Mesen's Lua/lauxlib.c:821. Zero means DENIED. Mesen only lifts it for
 * its own trusted Lua API. Defaulting this to 1 to "make loadfile work" would quietly remove the
 * sandbox from any script path that reaches Mesen's lauxlib.
 */
int SANDBOX_ALLOW_LOADFILE = 0;

/* ---- LuaSocket / MIME ------------------------------------------------------
 * Refusing beats faking. A stub that returned 0 ("success, one module pushed") would leave a script
 * holding a socket table with no functions in it. These abort with a real message instead.
 */
#define MESEN_LUA_EXTRA_REFUSE(fn)                                              \
    int fn(lua_State *L) {                                                      \
        (void) L;                                                               \
        fprintf(stderr, "[mesen_lua_extras] %s requested: this build hosts the app's Lua, "  \
                        "not Mesen's fork. Refusing rather than returning a module with no "   \
                        "functions in it. See mesen_lua_extras.c.\n", #fn);      \
        abort();                                                                \
    }

MESEN_LUA_EXTRA_REFUSE(luaopen_socket_core)
MESEN_LUA_EXTRA_REFUSE(luaopen_mime_core)
