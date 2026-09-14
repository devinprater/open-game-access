package com.devin.pokemonaccess

import android.content.res.AssetManager
import java.io.File

/**
 * LuaScriptRunner — runs Ola's pokemon-access main.lua on melonDS-lua.
 *
 * melonDS-lua calls the global _Update() once per emulated frame. main.lua is
 * written for BizHawk's `while true do ... emu.frameadvance() end` model, so
 * we run the script inside a coroutine:
 *
 *   1. loadstring(bizhawk_compat.lua + main.lua)  — the shim defines
 *      mainmemory / joypad / input / emu / console / speech and makes
 *      emu.frameadvance() a coroutine.yield.
 *   2. resume the coroutine; main.lua runs until frameadvance() yields.
 *   3. melonDS calls _Update() -> we resume the coroutine one step.
 *
 * main.lua stays byte-identical to the BizHawk original.
 */
object LuaScriptRunner {

    @Volatile var running: Boolean = false
        private set

    fun loadScript(assets: AssetManager, scriptPath: String): String? {
        return try {
            val compat = assets.open("lua/bizhawk_compat.lua").bufferedReader().readText()
            val main = assets.open(scriptPath).bufferedReader().readText()
            compat + "\n" + main
        } catch (e: Exception) {
            e.message
        }
    }

    /** Install the script into melonDS-lua's engine (native side does the rest). */
    external fun nativeLoadScript(source: String): Boolean
    external fun nativeStopScript()

    fun stop() {
        running = false
        try { nativeStopScript() } catch (_: UnsatisfiedLinkError) {}
    }

    init {
        // lib name matches the melonDS-lua Android port's CMake target
        System.loadLibrary("melonds_lua_bridge")
    }
}
