package me.magnum.melonds.accessibility

import android.content.Context
import android.net.Uri
import android.util.Log
import java.io.File

/**
 * The Game Boy / Game Boy Color / Game Boy Advance side of the Pokémon Access
 * integration: mGBA as the core, Pokémon Access v3.1.0 (`pokemon.lua` +
 * `gb.lua`/`gba.lua`) as the reader, booted by `oga_bootstrap.lua`.
 *
 * ⛔ THE HOST IS THE SHARED CORE (GbSessionJNI.cpp over pokecore), the same
 * Core/gba_core.cpp the iOS app runs. Until the move, Android had a Game Boy
 * host of its own (MGBARunner/MGBACore/MGBAScriptJNI) that read the script
 * text from here and offered a VisualBoyAdvance-flavoured Lua surface; it is
 * gone, and with it the Android-only fixes that had to be made twice. The
 * shared host boots the reader from a DIRECTORY (`oga_bootstrap.lua` inside
 * it), so this object's job is to put that directory on disk and hand it over.
 *
 * ⛔ THE SCRIPT'S DATA FILES MUST EXIST AS REAL FILES, NOT ASSETS.
 * `pokemon.lua` finds its per-game tables with `loadfile(scriptpath .. "game\\…")`
 * and its messages with `loadfile(scriptpath .. "\\message\\…")`. `loadfile` and
 * `require` cannot read an Android asset stream, so the asset tree is copied to
 * the app's files directory — once per installed build, keyed below.
 *
 * ⛔ THE BACKSLASHES ARE EXPECTED. The script joins its paths with `\` because
 * it was written for Windows. The shared core's file layer and GbSessionJNI's
 * sound path normalise them, so nothing here should try to "fix" the script.
 *
 * ⛔ 64-BIT ONLY, like the DS, NES/SNES and Genesis surfaces: the shared-Core
 * glue is not compiled for armeabi-v7a (android-apply-overlay.sh section 9),
 * so [isCoreLinked] throws UnsatisfiedLinkError there — that throw is the
 * "not in this build" answer, caught by GbGameSession.coreAvailable.
 */
object GbAccessibilityScript
{
    private const val TAG = "PokemonAccess"

    // Where the asset tree lives inside the APK.
    private const val GB_ASSET_ROOT = "lua/gb"
    // Where it is extracted to, relative to filesDir.
    private const val EXTRACT_DIR = "pokemon-access-gb"
    // The reader-set layout version. Bump it when the extraction itself
    // changes; ordinary reader edits are caught by the install stamp below.
    private const val ASSET_VERSION = "shared-core-1"
    // The file the shared host boots from: a tree without it is stale.
    private const val BOOTSTRAP = "oga_bootstrap.lua"

    /** The ROM extensions this path handles. */
    val ROM_EXTENSIONS = setOf("gb", "gbc", "gba")

    // Native side (GbSessionJNI.cpp).
    private external fun nativeGbAvailable(): Boolean
    private external fun setGbSpeechBridge(bridge: Any?)
    private external fun nativeGbSetDebugDir(dir: String)
    private external fun nativeGbDebugCommand(text: String)
    private external fun nativeGbLoadRom(romPath: String, savePath: String): Boolean
    private external fun nativeGbGameCode(): String
    private external fun nativeGbLastError(): String
    private external fun nativeGbBackendId(): String
    private external fun nativeGbSetScriptDir(dir: String)
    private external fun nativeGbStart(): Boolean
    private external fun nativeGbStop()
    private external fun nativeGbRunning(): Boolean
    private external fun nativeGbFrame(): Boolean
    private external fun nativeGbFramesCompleted(): Long
    private external fun nativeGbSetKeys(keys: Int)
    private external fun nativeGbSetHotkey(key: Char, down: Boolean)
    private external fun nativeGbStopSpeech(): Boolean
    private external fun nativeGbReadAudio(out: ShortArray, maxFrames: Int): Int
    private external fun nativeGbSetAudioEnabled(enabled: Boolean)
    private external fun nativeGbCopyFrame(out: IntArray, sizeOut: IntArray): Int

    /** The rate [readAudio] hands audio out at: every Android session's 32768 Hz. */
    const val AUDIO_RATE = 32768

    val isRunning: Boolean get() = nativeGbRunning()
    val frameCount: Long get() = nativeGbFramesCompleted()
    /** The cartridge's game code (e.g. "BPEE"), or "" before a load. */
    val gameCode: String get() = nativeGbGameCode()
    /** The shared core's last failure, for logcat. */
    val lastError: String get() = nativeGbLastError()

    /**
     * True when the Game Boy surface was compiled into the frontend .so;
     * throws UnsatisfiedLinkError where it was not — that throw IS the
     * negative answer, and the caller catches it.
     */
    fun isCoreLinked(): Boolean = nativeGbAvailable()

    /**
     * Picks the core for a ROM by extension. `.nds`/`.dsi`/`.ids` stay on the
     * DS path; anything else is not something this path can run.
     */
    fun handlesExtension(extension: String?): Boolean
    {
        return extension?.lowercase() in ROM_EXTENSIONS
    }

    fun handlesUri(uri: Uri): Boolean
    {
        val name = uri.lastPathSegment ?: uri.toString()
        return handlesExtension(name.substringAfterLast('.', ""))
    }

    /**
     * The in-game debug channel's folder (docs/design/debug-channel.md):
     * a cmd.txt saved there runs against the live game, and the replies
     * land in replies.log beside it. Kept across loads; "" turns it off.
     */
    fun setDebugDir(dir: String)
    {
        nativeGbSetDebugDir(dir)
    }

    /** The same commands without the file; runs on the next frame. */
    fun debugCommand(text: String)
    {
        nativeGbDebugCommand(text)
    }

    /**
     * The stamp that decides whether the extracted tree is current: the
     * layout version plus this install's update time, so an app update that
     * changes any reader file re-extracts, and a relaunch of the same build
     * does not.
     */
    private fun assetStamp(context: Context): String
    {
        val installed = try
        {
            context.packageManager.getPackageInfo(context.packageName, 0).lastUpdateTime
        }
        catch (e: Exception)
        {
            0L
        }
        return "$ASSET_VERSION@$installed"
    }

    /**
     * Copies the whole script tree into the app's files directory and returns
     * its absolute path (no trailing '/'), or null on failure.
     *
     * AssetManager.list() only lists one directory level, so this walks
     * recursively. It happens once per installed build, not once per ROM.
     */
    fun extractAssets(context: Context): String?
    {
        val root = File(context.filesDir, EXTRACT_DIR)
        val stamp = File(root, ".version")
        val want = assetStamp(context)

        if (stamp.isFile && stamp.readText().trim() == want && File(root, BOOTSTRAP).isFile)
        {
            return root.absolutePath
        }

        return try
        {
            if (root.exists()) root.deleteRecursively()
            if (!root.mkdirs() && !root.isDirectory)
            {
                Log.e(TAG, "[pokemon-access-gb] could not create ${root.absolutePath}")
                return null
            }

            val assets = context.assets
            var copied = 0
            fun copy(assetPath: String, target: File)
            {
                val children = assets.list(assetPath) ?: emptyArray()
                if (children.isEmpty())
                {
                    // A file, not a directory.
                    target.parentFile?.mkdirs()
                    assets.open(assetPath).use { input ->
                        target.outputStream().use { output -> input.copyTo(output, 64 * 1024) }
                    }
                    copied++
                    return
                }
                target.mkdirs()
                for (child in children) copy("$assetPath/$child", File(target, child))
            }

            copy(GB_ASSET_ROOT, root)
            if (!File(root, BOOTSTRAP).isFile)
            {
                Log.e(TAG, "[pokemon-access-gb] the bundled reader set has no $BOOTSTRAP")
                return null
            }
            stamp.writeText(want)
            Log.i(TAG, "[pokemon-access-gb] extracted $copied script files to ${root.absolutePath}")
            root.absolutePath
        }
        catch (e: Exception)
        {
            Log.e(TAG, "[pokemon-access-gb] asset extraction failed", e)
            null
        }
    }

    /**
     * Loads a cartridge and boots the reader on the shared core.
     *
     * `romPath` and `savePath` are native paths: the core reads the ROM
     * itself, so the caller must have resolved the SAF content URI to a real
     * file first (see [GbRomResolver]). The order — load, script folder,
     * start — is the iOS app's.
     */
    fun start(context: Context, romPath: String, savePath: String?): Boolean
    {
        val speech = AccessibilityScript.speechBridge(context)
        setGbSpeechBridge(speech)

        val scriptDir = extractAssets(context) ?: return false

        if (!nativeGbLoadRom(romPath, savePath ?: ""))
        {
            Log.e(TAG, "[pokemon-access-gb] load failed: ${nativeGbLastError()}")
            return false
        }
        nativeGbSetScriptDir(scriptDir)
        if (!nativeGbStart())
        {
            Log.e(TAG, "[pokemon-access-gb] reader failed to start: ${nativeGbLastError()}")
            nativeGbStop()
            return false
        }
        Log.i(TAG, "[pokemon-access-gb] reader running on the shared core " +
            "(backend ${nativeGbBackendId()}, game ${nativeGbGameCode()}, scripts $scriptDir)")
        return true
    }

    fun stop()
    {
        nativeGbStop()
    }

    fun shutdown()
    {
        nativeGbStop()
        setGbSpeechBridge(null)
    }

    fun runFrame()
    {
        nativeGbFrame()
    }

    /**
     * Stop the reader talking: the core's announcement queue is cleared and
     * the voice stopped. The game keeps running.
     */
    fun stopSpeech(): Boolean = nativeGbStopSpeech()

    /**
     * The current frame as RGBA8888 ints, plus its size. Returns (pixels, width,
     * height), or null when nothing is loaded.
     */
    fun currentFrame(): Triple<IntArray, Int, Int>?
    {
        val pixels = IntArray(240 * 160)
        val size = IntArray(2)
        val count = nativeGbCopyFrame(pixels, size)
        if (count <= 0) return null
        return Triple(pixels, size[0], size[1])
    }

    /**
     * The game's own audio: up to [maxFrames] interleaved stereo 16-bit frames
     * at [AUDIO_RATE] into [out]. Returns frames written, 0 when nothing is
     * waiting. Safe from the AudioTrack thread: it reads the queue the frame
     * thread fills (Core/gb_audio_queue.h), never the core itself.
     */
    fun readAudio(out: ShortArray, maxFrames: Int): Int = nativeGbReadAudio(out, maxFrames)

    /** The "Game sound" switch. Speech and the reader's cue sounds are not gated by it. */
    fun setAudioEnabled(enabled: Boolean)
    {
        nativeGbSetAudioEnabled(enabled)
    }

    /** Host key down/up, for the script's own hotkeys (letters). */
    fun onKeyEvent(key: Char, down: Boolean)
    {
        val upper = key.uppercaseChar()
        if (upper !in 'A'..'Z') return
        nativeGbSetHotkey(upper, down)
    }

    /** The emulated pad, as mGBA key bits (GbKeys). */
    fun setKeys(keys: Int)
    {
        nativeGbSetKeys(keys)
    }
}
