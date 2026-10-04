package com.devin.opengameaccess

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.util.Log
import android.webkit.JavascriptInterface
import android.webkit.WebView
import me.magnum.melonds.accessibility.GbAccessibilityScript
import me.magnum.melonds.accessibility.GbRomResolver

/**
 * The Game Boy side of the launcher, exposed to assets/index.html.
 *
 * ⛔ WHY THIS EXISTS AT ALL. GbAccessibilityScript and GbRomResolver have been
 * complete and linked into the APK for a while, and NOTHING CALLED THEM: the
 * JNI entry points were reachable only in principle. The launcher is a WebView,
 * so this object is the missing link between the page and the native path —
 * @JavascriptInterface is exactly the seam for it.
 *
 * The split: the page owns the player-facing flow (what is said, what the
 * buttons do); this owns everything the page cannot do — the SAF picker, turning
 * a content:// URI into a path mGBA can open, and the native calls.
 *
 * ⛔ EVERY METHOD IS CALLED ON THE WEBVIEW'S JS THREAD, NOT THE UI THREAD, so
 * anything touching a View has to post. These do not touch Views; they must run
 * off the UI thread anyway, because extracting the reader set is ~195 files.
 */
class GbBridge(
    private val activity: Activity,
    private val webView: WebView,
    private val onPicked: (Uri) -> Unit,
) {
    private companion object {
        const val TAG = "PokemonAccess"
        const val REQUEST_PICK = 0x0AB1
    }

    /** True once a Game Boy ROM is loaded and running in the mGBA core. */
    @Volatile private var running = false

    /** The last failure, for the page to speak instead of guessing. */
    @Volatile private var lastError: String = ""

    /**
     * True when this build can actually run Game Boy ROMs.
     *
     * ⛔ THE PAGE MUST ASK RATHER THAN ASSUME. The old page hardcoded "not
     * compiled into this test build", which was true once and then silently
     * stayed on screen after the core landed. Reporting capability from the
     * library that provides it means the text cannot go stale again.
     */
    @JavascriptInterface
    fun isAvailable(): Boolean {
        // ⛔ ASK THE APK, DO NOT GUESS A LIBRARY NAME. ApplicationInfo's
        // nativeLibraryDir names every .so that actually shipped, so a wrong
        // guess cannot masquerade as "the feature is not in this build" — the
        // exact stale claim this method exists to retire. GbAccessibilityScript's
        // `external fun`s bind to the frontend target's library, which is one of
        // these.
        return try {
            val dir = activity.applicationInfo.nativeLibraryDir ?: return false
            val so = java.io.File(dir).listFiles()?.filter { it.name.endsWith(".so") } ?: return false
            // The JNI exports live in the frontend's own library; a build made
            // with OGA_ALLOW_NO_GBA would not carry it.
            val hasFrontend = so.any { it.name.contains("frontend") }
            if (!hasFrontend) Log.w(TAG, "no frontend .so in ${dir}: ${so.map { it.name }}")
            hasFrontend
        } catch (t: Throwable) {
            Log.w(TAG, "Game Boy path unavailable: ${t.message}")
            false
        }
    }

    /** Extensions this path handles, so the page's picker can filter. */
    @JavascriptInterface
    fun romExtensions(): String = GbAccessibilityScript.ROM_EXTENSIONS.joinToString(",")

    /** Ask the player for a ROM. Returns immediately; pickRomResult arrives later. */
    @JavascriptInterface
    fun pickRom() {
        webView.post {
            val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "*/*"
            }
            // The activity's onActivityResult forwards here via deliverPick.
            pendingPick = true
            activity.startActivityForResult(intent, REQUEST_PICK)
        }
    }

    @Volatile private var pendingPick = false

    /** Called from MainActivity.onActivityResult with the player's choice. */
    fun deliverPick(uri: Uri?) {
        if (!pendingPick) return
        pendingPick = false
        if (uri == null) { reportFailure("No game selected."); return }
        // Hand the URI back to the page, which decides what to say. Doing the
        // resolution here would put player-facing wording in Kotlin and split
        // the voice in two.
        onPicked(uri)
    }

    /**
     * Tell the page a load failed, through the SAME callback the picker uses.
     *
     * ⛔ ONE CHANNEL, DELIBERATELY. This used to post to a second function that
     * the page never defines, so a cancel or an error invoked a name that does
     * not exist and the player heard nothing — and silence is indistinguishable
     * from a crash to someone who cannot see the screen. Everything now arrives
     * as ogaRomPicked(null, "") plus lastError().
     */
    private fun reportFailure(message: String) {
        lastError = message
        Log.w(TAG, message)
        webView.post {
            webView.evaluateJavascript(
                "window.ogaRomPicked && window.ogaRomPicked(null, \"\");", null)
        }
    }

    /**
     * Load a Game Boy ROM and start the Pokémon Access reader.
     *
     * ⛔ THE URI IS NOT A PATH. mGBA opens ROMs through the platform VFS and its
     * reader walks GB banks past the 16 KiB window, so it needs a real file —
     * GbRomResolver materialises the SAF URI into the cache and keys the copy on
     * the document's name and length, which is what tells "the player reopened
     * this ROM" from "the player swapped in a patched hack of the same name".
     */
    @JavascriptInterface
    fun startGame(uriString: String): Boolean {
        val uri = Uri.parse(uriString)
        val romPath = GbRomResolver.resolve(activity, uri)
        if (romPath == null) {
            reportFailure("Could not read that game file.")
            return false
        }
        val savePath = GbRomResolver.savePathFor(activity, romPath)
        val ok = GbAccessibilityScript.start(activity, romPath, savePath)
        running = ok
        if (!ok) {
            reportFailure("The accessibility reader could not start for that game.")
            return false
        }
        Log.i(TAG, "reader running: $romPath")
        return true
    }

    /** One emulated frame, driven by the page's own animation loop. */
    @JavascriptInterface
    fun runFrame() {
        if (running) GbAccessibilityScript.runFrame()
    }

    /** The emulated pad, as mGBA key bits. See GbAccessibilityScript.setKeys. */
    @JavascriptInterface
    fun setKeys(keys: Int) {
        if (running) GbAccessibilityScript.setKeys(keys)
    }

    /** A script hotkey letter (P pathfind, K read item, ...), down or up. */
    @JavascriptInterface
    fun setHotkey(key: String, down: Boolean) {
        if (running && key.isNotEmpty()) GbAccessibilityScript.onKeyEvent(key[0], down)
    }

    @JavascriptInterface
    fun stopGame() {
        running = false
        GbAccessibilityScript.stop()
    }

    @JavascriptInterface
    fun lastError(): String = lastError

    /** Minimal JSON string escaping: the page parses these with JSON.parse. */
    private fun js(s: String): String {
        val b = StringBuilder("\"")
        for (c in s) when (c) {
            '"' -> b.append("\\\"")
            '\\' -> b.append("\\\\")
            '\n' -> b.append("\\n")
            '\r' -> b.append("\\r")
            '\t' -> b.append("\\t")
            else -> if (c < ' ') b.append(String.format("\\u%04x", c.code)) else b.append(c)
        }
        return b.append("\"").toString()
    }
}
