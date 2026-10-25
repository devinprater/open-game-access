package me.magnum.melonds.accessibility

import android.content.Context
import android.net.Uri
import android.util.Log
import java.io.File

/**
 * The PlayStation Portable side: PPSSPP (the audited OGA subset) as the
 * core, driven through the psp_* C ABI in `Core/psp_core.h`.
 *
 * This is a sibling of [GbAccessibilityScript], not a variant of it: that
 * one hosts a Lua reader around mGBA; this one hosts the native game
 * adapters (Dissidia, Birth by Sleep, ...) through a native host
 * (PspAdapterHost.cpp) that selects one by DISC_ID at load, pumps it per
 * frame, and forwards its speech to the app's shared AccessibilitySpeech
 * instance — the bridge PspGameSession installs at session start.
 *
 * ⛔ THE RUNTIME ASSETS MUST EXIST AS REAL FILES, NOT APK ASSETS.
 * `psp_core` boots PPSSPP headless over a `DirectoryReader` VFS, which
 * reads a filesystem tree; the bundled `ppsspp-assets` set (compat.ini,
 * the PPGe atlas, the VFPU LUTs — see `PPSPP_ASSETS` in
 * scripts/core-sources.sh) is therefore extracted into filesDir once per
 * version stamp below. The extraction is the stage-2 probe's pattern
 * (PspProbe) and shares its directory AND stamp, so whichever of the two
 * runs first stages the tree for both; the core itself fails loudly
 * naming any missing manifest file at load, so a partial extraction
 * cannot slip through silently.
 *
 * ⛔ AVAILABILITY IS A NATIVE QUESTION, NOT A PACKAGE QUESTION. The GB
 * probe reads the APK's own entries because GB presence is a packaging
 * fact. The PSP core shares the SAME frontend .so as everything else,
 * so the package cannot tell the cores apart — instead [isCoreLinked]
 * calls a JNI function that exists exactly when the core does. On
 * armeabi-v7a the subset is empty by design (the audited lists were
 * never ported to 32-bit ARM), this whole native surface is absent, and
 * the call throws UnsatisfiedLinkError, which the caller
 * (PspGameSession.coreAvailable) turns into a plain "not in this build".
 * Fail-closed, like every other probe in this app.
 */
object PspAccessibilityScript
{
    private const val TAG = "PspAccess"

    // Where the asset tree lives inside the APK (staged into the frontend
    // assets by scripts/android-apply-overlay.sh section 8).
    private const val ASSET_ROOT = "ppsspp-assets"
    // Where it is extracted to, relative to filesDir. Shared with PspProbe.
    private const val EXTRACT_DIR = "ppsspp-assets"
    // The version stamp: any change to this forces a re-extract. Shared
    // with PspProbe, which extracts the same tree to the same place.
    private const val ASSET_VERSION = "stage2-1"

    /** The disc-image extensions this path handles. */
    val ROM_EXTENSIONS = setOf("iso", "cso", "pbp")

    // Native side (PspSessionJNI.cpp, stage 3 surface).
    private external fun nativePspAvailable(): Boolean
    private external fun nativePspLoadRom(romPath: String, saveDir: String, assetDir: String): Boolean
    private external fun nativePspGameCode(): String
    private external fun nativePspLastError(): String
    private external fun nativePspStart(): Boolean
    private external fun nativePspStop()
    private external fun nativePspRunning(): Boolean
    private external fun nativePspFrame(): Boolean
    private external fun nativePspFramesCompleted(): Long
    private external fun nativePspSetButton(button: Int, down: Boolean)
    private external fun nativePspReadAudio(out: ShortArray, maxFrames: Int): Int
    private external fun nativePspSaveState(path: String): Boolean
    private external fun nativePspLoadState(path: String): Boolean

    // Native side (PspSessionJNI.cpp, stage 4 adapter surface — thin
    // forwards to PspAdapterHost.cpp).
    private external fun setPspSpeechBridge(bridge: Any?)
    private external fun nativePspAdapterId(): String
    private external fun nativePspAdapterName(): String
    private external fun nativePspAdapterReady(): Boolean
    private external fun nativePspAdapterCommand(command: Int): Boolean
    private external fun nativePspCuePoll(ints: IntArray, floats: FloatArray): Boolean
    private external fun nativePspSetHudGaugeCues(useSound: Boolean)

    /**
     * True when the PSP core is linked into this build. Throws
     * UnsatisfiedLinkError where it is not — that throw IS the negative
     * answer, and it belongs to the caller to catch.
     */
    fun isCoreLinked(): Boolean = nativePspAvailable()

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
     * Copies the bundled ppsspp-assets tree into the files directory and
     * returns its absolute path, or null on failure. AssetManager.list()
     * only lists one directory level, so this walks recursively — the
     * same walk GbAccessibilityScript.extractAssets and PspProbe use.
     */
    fun extractAssets(context: Context): String?
    {
        val root = File(context.filesDir, EXTRACT_DIR)
        val stamp = File(root, ".version")

        if (stamp.isFile && stamp.readText().trim() == ASSET_VERSION &&
            File(root, "compat.ini").isFile
        )
        {
            return root.absolutePath
        }

        return try
        {
            if (root.exists()) root.deleteRecursively()
            if (!root.mkdirs() && !root.isDirectory)
            {
                Log.e(TAG, "could not create ${root.absolutePath}")
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

            copy(ASSET_ROOT, root)
            stamp.writeText(ASSET_VERSION)
            Log.i(TAG, "extracted $copied PPSSPP asset files to ${root.absolutePath}")
            root.absolutePath
        }
        catch (e: Exception)
        {
            Log.e(TAG, "PPSSPP asset extraction failed", e)
            null
        }
    }

    /**
     * Creates the core and loads a disc image. [saveDir] is the memstick
     * root (savedata lives under PSP/SAVEDATA below it); the caller owns
     * choosing it — PspGameSession puts it under filesDir so saves
     * survive cache eviction.
     */
    fun loadRom(romPath: String, saveDir: String, assetDir: String): Boolean
    {
        val ok = nativePspLoadRom(romPath, saveDir, assetDir)
        if (ok)
        {
            Log.i(TAG, "loaded $romPath (disc id ${nativePspGameCode()})")
        }
        else
        {
            Log.e(TAG, "load failed for $romPath: ${nativePspLastError()}")
        }
        return ok
    }

    /** The loaded game's DISC_ID (e.g. ULUS10437), or "" before a load. */
    fun gameCode(): String = nativePspGameCode()

    /** The core's own words for the last failure, or "". */
    fun lastError(): String = nativePspLastError()

    fun startCore(): Boolean = nativePspStart()

    /** Stops AND destroys the core. Safe to call with nothing loaded. */
    fun stopCore()
    {
        nativePspStop()
    }

    val isRunning: Boolean get() = nativePspRunning()

    /** One emulated frame. False when the core stopped answering. */
    fun runFrame(): Boolean = nativePspFrame()

    val framesCompleted: Long get() = nativePspFramesCompleted()

    /** Latch one PSP button (a PSP_BTN_* id from Core/psp_core.h). */
    fun setButton(button: Int, down: Boolean)
    {
        nativePspSetButton(button, down)
    }

    /**
     * Drains mixed audio into [out] (interleaved stereo s16 at the
     * core's 32768 Hz). Returns the number of FRAMES written; 0 when the
     * ring is dry.
     */
    fun readAudio(out: ShortArray, maxFrames: Int): Int = nativePspReadAudio(out, maxFrames)

    fun saveState(path: String): Boolean = nativePspSaveState(path)

    fun loadState(path: String): Boolean = nativePspLoadState(path)

    /**
     * Installs the app's shared speech bridge with the adapter host (or
     * clears it, with null, at session end). Adapter speech lands on the
     * same AccessibilitySpeech instance the DS/GB hosts speak through.
     */
    fun setSpeechBridge(bridge: Any?)
    {
        setPspSpeechBridge(bridge)
    }

    /** The loaded game's adapter id (e.g. "khbbs"), or "" when the game has no reader. */
    fun adapterId(): String = nativePspAdapterId()

    /** The loaded game's adapter display name, or "" when the game has no reader. */
    fun adapterName(): String = nativePspAdapterName()

    /**
     * Whether the selected adapter has attached and reports ready. The
     * native side attaches lazily on this call (the adapter's memory
     * anchors only exist once the game has booted), so the session
     * polls it once a frame rather than expecting a one-shot answer.
     */
    fun adapterReady(): Boolean = nativePspAdapterReady()

    /**
     * Sends one oga::Command (its raw value — see PspAdapterCommands)
     * to the selected adapter. False is a refusal: no adapter, out of
     * range, or not ready yet; the caller decides what a refusal says.
     */
    fun adapterCommand(command: Int): Boolean = nativePspAdapterCommand(command)

    /**
     * One frame of battle-cue data from the native host's single
     * snapshot: the lock-on beacon state, the free-flight field answer,
     * and the one-shot health events (hit beeps and gauge-full earcons)
     * the adapter raised since the last frame.
     *
     * [state] is the beacon state (0 nothing, 1 battle with no lock,
     * 2 enemy, 3 EX core, 4 the one-frame EX-gain pulse) and [dist] the
     * locked target's distance in world units. [field] marks the
     * free-flight answer live, with [kind] (1 enemy, 2 city, 3 ally),
     * [bearing] in degrees clockwise from the direction of travel,
     * [fieldDist], and [headingLive] false when the bearing came from
     * a stale heading. [healthBits] is the one-shot bitmask: 1 foe
     * beep (with [foeHz]/[foePan]), 2 own beep (with [ownHz]/[ownPan]),
     * 4 Focus full, 8 D-Link full.
     */
    data class CuePoll(
        val state: Int,
        val dist: Float,
        val field: Boolean,
        val kind: Int,
        val bearing: Float,
        val fieldDist: Float,
        val headingLive: Boolean,
        val healthBits: Int,
        val foeHz: Float,
        val foePan: Float,
        val ownHz: Float,
        val ownPan: Float,
    )

    /**
     * Push the player's HUD gauge-cue choice into the Birth by Sleep
     * adapter (oga::KhbbsHudGaugeCues): true = gauge-full moments
     * sound as earcons, false = the adapter speaks its lines instead.
     * The session resolves the effective value (the game's Per Game
     * Settings override, else the global default) and calls this once
     * when a session starts — settings apply at start
     * (docs/design/per-game-settings.md). Only that adapter reads the
     * flag; the session gates the call on its adapter id anyway.
     */
    fun setHudGaugeCues(useSound: Boolean)
    {
        nativePspSetHudGaugeCues(useSound)
    }

    // The poll's scratch arrays, reused across frames: the session
    // confines cuePoll to its frame thread, the same confinement the
    // native surface's other calls already assume.
    private val cueInts = IntArray(5)
    private val cueFloats = FloatArray(7)

    /**
     * Polls the cue snapshot for this frame, or null when there is
     * nothing to cue (no adapter, the reader not attached yet, or the
     * adapter refused the snapshot) — null is silence, never an error,
     * exactly the fail-closed shape of the adapter's other answers.
     * Call once per frame, after the frame has run: the native side
     * takes ONE snapshot per call and the adapter consumes its
     * one-frame events into it, so a second call in the same frame
     * would answer for a moment that has already been voiced.
     */
    fun cuePoll(): CuePoll?
    {
        if (!nativePspCuePoll(cueInts, cueFloats)) return null
        return CuePoll(
            state = cueInts[0],
            dist = cueFloats[0],
            field = cueInts[1] != 0,
            kind = cueInts[2],
            bearing = cueFloats[2],
            fieldDist = cueFloats[1],
            headingLive = cueInts[3] != 0,
            healthBits = cueInts[4],
            foeHz = cueFloats[3],
            foePan = cueFloats[4],
            ownHz = cueFloats[5],
            ownPan = cueFloats[6],
        )
    }
}
