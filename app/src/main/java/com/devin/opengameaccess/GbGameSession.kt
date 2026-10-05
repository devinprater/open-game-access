package com.devin.opengameaccess

import android.app.Activity
import android.content.Context
import android.net.Uri
import android.util.Log
import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalView
import me.magnum.melonds.accessibility.AccessibilityScript
import me.magnum.melonds.accessibility.GbAccessibilityScript
import me.magnum.melonds.accessibility.GbRomResolver

/**
 * The Game Boy session, driven directly from Compose.
 *
 * ⛔ THIS REPLACES `GbBridge`. That class existed only because the launcher was a
 * WebView: it was the `@JavascriptInterface` seam between `assets/index.html` and
 * the native path, and its whole job was turning JS calls into Kotlin calls. With
 * Compose there is no seam to bridge, so this is a plain object the screen calls.
 *
 * ⛔ AND IT FIXES A REAL BUG THE BRIDGE HAD. `AccessibilitySpeech` deliberately
 * routes announcements through the running screen reader's live region when one is
 * active — two voices on one output interrupt each other, so the screen reader
 * should be the only voice. That path needs `setAnnouncementView`, and the old
 * MainActivity NEVER CALLED IT. So `announcementView` stayed null, the code fell
 * through to its "no view yet" branch and spoke through TextToSpeech anyway — the
 * exact double-voice problem the guard exists to prevent. [attachAnnouncementView]
 * below is the missing call.
 */
object GbGameSession {

    private const val TAG = "PokemonAccess"

    /** True once a ROM is loaded and the reader is running. */
    @Volatile
    private var running = false

    /** The last failure, for the screen to speak instead of guessing. */
    @Volatile
    private var lastError: String = ""

    fun romExtensions(): Set<String> = GbAccessibilityScript.ROM_EXTENSIONS

    /**
     * True when this build can actually run Game Boy ROMs.
     *
     * ⛔ ASK THE APK, DO NOT GUESS A LIBRARY NAME. `ApplicationInfo.nativeLibraryDir`
     * names every .so that actually shipped, so a wrong guess cannot masquerade as
     * "the feature is not in this build". Measured on the shipped v0.5.0 APK: the
     * library is `libmelonDS-android-frontend.so`, which does contain "frontend",
     * so the check passes today — but the check is a name match, so it is stated
     * here rather than left implicit.
     */
    fun coreAvailable(context: Context): Boolean {
        return try {
            val dir = context.applicationInfo.nativeLibraryDir ?: return false
            val libs = java.io.File(dir).listFiles()?.filter { it.name.endsWith(".so") } ?: return false
            val hasFrontend = libs.any { it.name.contains("frontend") }
            if (!hasFrontend) Log.w(TAG, "no frontend .so in $dir: ${libs.map { it.name }}")
            hasFrontend
        } catch (t: Throwable) {
            Log.w(TAG, "Game Boy path unavailable: ${t.message}")
            false
        }
    }

    /**
     * Point announcements at the live-region view. The missing call described above.
     *
     * Must be called with a view that is attached, so Compose calls it from a
     * SideEffect once composition is live — the view is what the screen reader
     * announces from, and an unattached one announces nothing.
     */
    fun attachAnnouncementView(view: View) {
        AccessibilityScript.setAnnouncementView(view)
    }

    /**
     * Load a Game Boy ROM and start the reader. Returns null on success, or a
     * player-facing message on failure.
     *
     * ⛔ THE URI IS NOT A PATH. mGBA opens ROMs through the platform VFS and its
     * reader walks GB banks past the 16 KiB window, so it needs a real file —
     * [GbRomResolver] materialises the SAF URI into the cache and keys the copy on
     * the document's name and length, which is what tells "the player reopened this
     * ROM" from "the player swapped in a patched hack of the same name".
     */
    fun start(activity: Activity, uri: Uri): String? {
        val romPath = GbRomResolver.resolve(activity, uri)
        if (romPath == null) {
            lastError = "Could not read that game file."
            return lastError
        }
        val savePath = GbRomResolver.savePathFor(activity, romPath)
        val started = GbAccessibilityScript.start(activity, romPath, savePath)
        running = started
        if (!started) {
            lastError = "The accessibility reader could not start for that game."
            return lastError
        }
        Log.i(TAG, "reader running: $romPath")
        lastError = ""
        return null
    }

    fun stop() {
        running = false
        GbAccessibilityScript.stop()
    }

    /** One emulated frame. Driven from the Compose frame clock, see [rememberFrameClock]. */
    fun runFrame() {
        if (running) GbAccessibilityScript.runFrame()
    }

    /**
     * A script hotkey. Edge-triggered: the script's own edge detector wants ONE
     * press, so the release is posted a frame later rather than never.
     */
    fun hotkey(key: Char, onRelease: () -> Unit) {
        if (!running) return
        GbAccessibilityScript.onKeyEvent(key, true)
        // ~80 ms, matching the page's setTimeout: long enough for the script's
        // frame to see the press, short enough to feel like a tap.
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(
            { GbAccessibilityScript.onKeyEvent(key, false); onRelease() },
            80L,
        )
    }

    fun setKeys(mask: Int) {
        if (running) GbAccessibilityScript.setKeys(mask)
    }

    /** True once a game is running, so the pad and commands can appear. */
    val isRunning: Boolean get() = running
}

/**
 * The mGBA key bits, in the core's own order.
 *
 * ⛔ THESE ARE THE CORE'S VALUES, NOT A UI CONVENTION. mGBA's GBAKey order is
 * A B Select Start Right Left Up Down R L, and a wrong bit is a wrong button with
 * no error anywhere — the player presses Up and the game reads Right.
 */
object GbKeys {
    const val A = 1
    const val B = 2
    const val SELECT = 4
    const val START = 8
    const val RIGHT = 16
    const val LEFT = 32
    const val UP = 64
    const val DOWN = 128
    const val R = 256
    const val L = 512
}

/**
 * Drives one emulated frame per Compose frame while a game runs.
 *
 * ⛔ THE CLOCK IS THE APP'S, AND IT IS THE SAME CADENCE THE READER ASSUMES. The page
 * used requestAnimationFrame; the iOS app uses a CADisplayLink. Driving frames from
 * a frame clock (rather than a free-running native thread) keeps the reader's
 * speech callbacks on the same thread that produced them. `withFrameNanos` is
 * Compose's frame signal and suspends between frames instead of spinning.
 */
@Composable
fun rememberFrameClock(running: Boolean) {
    if (!running) return
    androidx.compose.runtime.LaunchedEffect(Unit) {
        while (true) {
            androidx.compose.runtime.withFrameNanos { }
            GbGameSession.runFrame()
        }
    }
}
