package com.devin.opengameaccess

import android.content.res.AssetManager

/**
 * MelonCore — JNI bindings to the melonDS-lua emulator core.
 *
 * The native side (libmelonds_bridge.so) wraps melonDS-android's
 * MelonDSAndroid facade and the melonDS-lua script engine:
 *   - setup + loadRom + run in an emulator thread
 *   - loads bizhawk_compat.lua + main.lua into the core's Lua state,
 *     wrapped in a coroutine resumed from the core's per-frame _Update()
 *
 * Method list mirrors the core's own JNI (MelonDSAndroidJNI.cpp):
 * setupEmulator / loadRom / loop / onKeyPress / onKeyRelease / touchScreen /
 * pause / resume / cleanup, plus our script entry points.
 */
object MelonCore {

    @Volatile private var loaded = false

    fun ensureLoaded(): Boolean = try {
        if (!loaded) {
            System.loadLibrary("melonds_bridge")
            loaded = true
        }
        true
    } catch (_: UnsatisfiedLinkError) {
        false
    }

    // ---- emulator lifecycle (mirrors MelonEmulator.kt of melonDS-android) ----
    external fun nativeSetup(configJson: String): Boolean
    external fun nativeLoadRom(romPath: String, savePath: String): Boolean
    external fun nativeStart()
    external fun nativePause()
    external fun nativeResume()
    external fun nativeStop()
    external fun nativePressKey(key: Int)
    external fun nativeReleaseKey(key: Int)
    external fun nativeTouchScreen(x: Int, y: Int)
    external fun nativeReleaseScreen()

    // ---- script engine (melonDS-lua) ----
    /** Load the accessibility script (compat shim + main.lua) into the core's Lua state. */
    external fun nativeLoadScript(source: String): Boolean
    /** Called by the core once per frame; resumes the script coroutine. */
    external fun nativeFrameUpdate()

    // DS key constants (match melonDS KeyMapping order: Up Down Left Right Start
    // Select B A Y X L R)
    object Keys {
        const val UP = 0; const val DOWN = 1; const val LEFT = 2; const val RIGHT = 3
        const val START = 4; const val SELECT = 5
        const val B = 6; const val A = 7; const val Y = 8; const val X = 9
        const val L = 10; const val R = 11
    }
}
