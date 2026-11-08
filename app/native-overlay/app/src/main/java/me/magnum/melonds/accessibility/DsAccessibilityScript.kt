package me.magnum.melonds.accessibility

import android.net.Uri
import android.util.Log

/**
 * The Nintendo DS side: the shared Core DS host (`Core/pokecore.cpp` —
 * the same translation unit the iOS app ships) driven through the
 * poke_* C ABI in `Sources/CPokeCore/include/pokecore.h`, via
 * DsSessionJNI.cpp.
 *
 * This is a sibling of [PspAccessibilityScript], not a variant of it:
 * that one hosts PPSSPP behind its own adapter host; this one's host
 * carries everything itself — the melonDS instance (direct boot over
 * the FreeBIOS path, software renderer), the embedded Lua engine the
 * Pokémon Black/White reader script runs in, the native adapter attach
 * (the shared Core registry's DS adapters: Fire Emblem: Shadow Dragon,
 * Dragon Ball Z: Attack of the Saiyans, Dragon Quest IX), the
 * announcement queue and the battle-cue snapshots. The script text
 * (BizHawk compat shim + main.lua, concatenated by DsGameSession from
 * the APK assets) is handed over with [setScript] before each load:
 * pokecore's contract is set-once-before-load, the order iOS's
 * GameSession uses at init.
 *
 * ⛔ NO RUNTIME ASSETS TO EXTRACT. Where the PSP path must stage a
 * filesystem tree for its VFS, pokecore boots a cartridge from two file
 * paths (the ROM and its save) and needs nothing else: the BIOS is the
 * core's own generated path unless a session supplies one, and there is
 * no firmware file in the APK.
 *
 * ⛔ AVAILABILITY IS A NATIVE QUESTION, NOT A PACKAGE QUESTION — the
 * PSP probe's reasoning, verbatim: the DS host shares the SAME frontend
 * .so as everything else, so the package cannot tell the cores apart;
 * instead [isCoreLinked] calls a JNI function that exists exactly when
 * the host does. On armeabi-v7a the DS glue is not compiled (the
 * overlay's section 9 mirrors the PSP subset's stance), this whole
 * native surface is absent, and the call throws UnsatisfiedLinkError,
 * which the caller (DsGameSession.coreAvailable) turns into a plain
 * "not in this build". Fail-closed, like every other probe in this app.
 */
object DsAccessibilityScript
{
    private const val TAG = "DsAccess"

    /** The cartridge-image extensions this path handles. */
    val ROM_EXTENSIONS = setOf("nds")

    // Native side (DsSessionJNI.cpp).
    private external fun nativeDsAvailable(): Boolean
    private external fun setDsSpeechBridge(bridge: Any?)
    private external fun nativeDsSetScript(script: String)
    private external fun nativeDsLoadRom(romPath: String, savePath: String): Boolean
    private external fun nativeDsGameCode(): String
    private external fun nativeDsLastError(): String
    private external fun nativeDsStart(): Boolean
    private external fun nativeDsStop()
    private external fun nativeDsRunning(): Boolean
    private external fun nativeDsFrame(): Boolean
    private external fun nativeDsFramesCompleted(): Long
    private external fun nativeDsSetButton(button: Int, down: Boolean)
    private external fun nativeDsTouch(x: Int, y: Int, down: Boolean)
    private external fun nativeDsSetHotkey(key: Char, down: Boolean)
    private external fun nativeDsReadAudio(out: ShortArray, maxFrames: Int): Int
    private external fun nativeDsSetAudioEnabled(enabled: Boolean)
    private external fun nativeDsSaveState(path: String): Boolean
    private external fun nativeDsLoadState(path: String): Boolean
    private external fun nativeDsAdapterId(): String
    private external fun nativeDsAdapterName(): String
    private external fun nativeDsAdapterReady(): Boolean
    private external fun nativeDsAdapterCommand(command: Int): Boolean
    private external fun nativeDsCuePoll(ints: IntArray, floats: FloatArray): Boolean

    /**
     * True when the DS host is linked into this build. Throws
     * UnsatisfiedLinkError where it is not — that throw IS the negative
     * answer, and it belongs to the caller to catch.
     */
    fun isCoreLinked(): Boolean = nativeDsAvailable()

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
     * Hands the reader script (compat shim + main.lua, concatenated) to
     * the native host. Applied to the next core the host creates, i.e.
     * call before [loadRom] — the session does, at every start, so a
     * changed script can never leak into a running game.
     */
    fun setScript(script: String)
    {
        nativeDsSetScript(script)
    }

    /**
     * Creates the core and loads a cartridge image. [savePath] is the
     * game's battery save file; the caller owns choosing it —
     * DsGameSession puts it beside the ROM's cached copy (DsRomResolver)
     * so saves survive between sessions without going through SAF.
     */
    fun loadRom(romPath: String, savePath: String): Boolean
    {
        val ok = nativeDsLoadRom(romPath, savePath)
        if (ok)
        {
            Log.i(TAG, "loaded $romPath (game code ${nativeDsGameCode()})")
        }
        else
        {
            Log.e(TAG, "load failed for $romPath: ${nativeDsLastError()}")
        }
        return ok
    }

    /** The loaded game's cartridge code (e.g. YDQE), or "" before a load. */
    fun gameCode(): String = nativeDsGameCode()

    /** The core's own words for the last failure, or "". */
    fun lastError(): String = nativeDsLastError()

    fun startCore(): Boolean = nativeDsStart()

    /** Stops AND destroys the core. Safe to call with nothing loaded. */
    fun stopCore()
    {
        nativeDsStop()
    }

    val isRunning: Boolean get() = nativeDsRunning()

    /** One emulated frame. False when the core stopped answering. */
    fun runFrame(): Boolean = nativeDsFrame()

    val framesCompleted: Long get() = nativeDsFramesCompleted()

    /** Latch one DS button (a POKE_BTN_* id from pokecore.h). */
    fun setButton(button: Int, down: Boolean)
    {
        nativeDsSetButton(button, down)
    }

    /**
     * Press or release the touch screen at (x, y) in the bottom
     * screen's own coordinates (0..255, 0..191).
     */
    fun setTouch(x: Int, y: Int, down: Boolean)
    {
        nativeDsTouch(x, y, down)
    }

    /**
     * A reader-script hotkey: one upper-case letter, the set the DS
     * script groups name (J/K/L/I/O reading, P path, C Where Am I,
     * E tiles, R stop speech, U repeat). Edge-triggered by the script;
     * the session's tapHotkey presses and releases.
     */
    fun setHotkey(key: Char, down: Boolean)
    {
        nativeDsSetHotkey(key, down)
    }

    /**
     * Drains the console's audio into [out] (interleaved stereo s16,
     * drained by the session's AudioTrack thread — pokecore mixes at
     * 32768 Hz, the rate its iOS and PSP-Android counterparts use).
     * Returns the number of FRAMES written; 0 when the core has nothing
     * to give (or game sound is off).
     */
    fun readAudio(out: ShortArray, maxFrames: Int): Int = nativeDsReadAudio(out, maxFrames)

    /** The game-sound switch (Settings): pokecore gates its own output. */
    fun setAudioEnabled(enabled: Boolean)
    {
        nativeDsSetAudioEnabled(enabled)
    }

    fun saveState(path: String): Boolean = nativeDsSaveState(path)

    fun loadState(path: String): Boolean = nativeDsLoadState(path)

    /**
     * Installs the app's shared speech bridge with the DS host (or
     * clears it, with null, at session end). Adapter and script speech
     * lands on the same AccessibilitySpeech instance the GB/PSP hosts
     * speak through.
     */
    fun setSpeechBridge(bridge: Any?)
    {
        setDsSpeechBridge(bridge)
    }

    /** The loaded game's adapter id (e.g. "dq9"), or "" when the game has no reader. */
    fun adapterId(): String = nativeDsAdapterId()

    /** The loaded game's adapter display name, or "" when the game has no reader. */
    fun adapterName(): String = nativeDsAdapterName()

    /**
     * Whether the selected adapter has attached and reports ready. The
     * native side attaches lazily on this call (the adapter's memory
     * anchors only exist once the game has booted), so the session
     * polls it once a frame rather than expecting a one-shot answer.
     */
    fun adapterReady(): Boolean = nativeDsAdapterReady()

    /**
     * Sends one oga::Command (its raw value — see DsAdapterCommands in
     * DsGameSession) to the selected adapter. False is a refusal: no
     * adapter, out of range, or not ready yet; the caller decides what
     * a refusal says.
     */
    fun adapterCommand(command: Int): Boolean = nativeDsAdapterCommand(command)

    /**
     * One frame of battle-cue data from the host's single shared
     * snapshot: the lock-on beacon state, the free-flight field answer,
     * and the one-shot health events (hit beeps and gauge-full earcons)
     * the adapter raised since the last frame — plus the Active Guard
     * window (Attack of the Saiyans): a continuous answer, not an
     * event. Field-for-field the PSP poll's packing and meanings; see
     * PspAccessibilityScript.CuePoll.
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
        val guardActive: Boolean,
        val guardFrac: Float,
        val guardBeat: Boolean,
    )

    // The poll's scratch arrays, reused across frames: the session
    // confines cuePoll to its frame thread, the same confinement the
    // native surface's other calls already assume.
    private val cueInts = IntArray(7)
    private val cueFloats = FloatArray(8)

    /**
     * Polls the cue snapshot for this frame, or null when there is
     * nothing to cue (no adapter selected for this game) — null is
     * silence, never an error, exactly the fail-closed shape of the
     * PSP poll. Call once per frame, after the frame has run: the
     * native side shares ONE snapshot across its four cue answers and
     * the adapter consumes its one-frame events into it, so a second
     * call in the same frame would answer for a moment that has
     * already been voiced.
     */
    fun cuePoll(): CuePoll?
    {
        if (!nativeDsCuePoll(cueInts, cueFloats)) return null
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
            guardActive = cueInts[5] != 0,
            guardFrac = cueFloats[7],
            guardBeat = cueInts[6] != 0,
        )
    }
}
