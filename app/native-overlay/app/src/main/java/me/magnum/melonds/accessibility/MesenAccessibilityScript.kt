package me.magnum.melonds.accessibility

import android.net.Uri
import android.util.Log

/**
 * The NES and Super Nintendo side: the shared Core host
 * (`Core/pokecore.cpp` — the same translation unit the iOS app ships)
 * driven through the poke_* C ABI in
 * `Sources/CPokeCore/include/pokecore.h`, via MesenSessionJNI.cpp.
 *
 * This is a sibling of [DsAccessibilityScript], not a variant of it:
 * that one hosts the DS backend of the same multi-console host; this
 * one's loads reach the host's NES and SNES branches, which drive
 * Core/mesen_core.cpp and Core/snes_core.cpp — the MesenCE glue iOS
 * ships — compiled into this build by the overlay's section 10 (the
 * oga-mesen subproject) with the stubs that used to refuse their ABIs
 * removed. What the host carries for these consoles:
 *  - the NES reader sets (Dragon Warrior Access, Zelda 1 Access): Lua
 *    mods running in mesen_core's own Lua host. The core names the set
 *    a ROM needs by its CRC32 ([readerSet]); the session stages a
 *    writable copy and hands its directory back ([setScriptDir])
 *    between the load and the start, the order iOS's GameSession
 *    uses. The readers' speech reaches the app through the host's
 *    speech callback, which pokecore registers at NES load.
 *  - the SNES native adapter (A Link to the Past), selected by the
 *    shared registry from the ROM's CRC when the host attaches it.
 *
 * ⛔ THE LOAD'S SECOND PATH IS A FOLDER, NOT A SAVE FILE. Where the DS
 * surface hands the host a .sav path, this one hands it Mesen's HOME
 * FOLDER: the host's NES/SNES branches pass it to
 * FolderUtilities::SetHomeFolder, without which Mesen refuses every
 * ROM, and the game's battery save lives inside it. MesenRomResolver
 * owns choosing it (a per-game directory in internal storage).
 *
 * ⛔ AVAILABILITY IS A NATIVE QUESTION, NOT A PACKAGE QUESTION — the
 * DS/PSP probe's reasoning, verbatim: the Mesen surface shares the
 * SAME frontend .so as everything else, so the package cannot tell
 * the cores apart; instead [isCoreLinked] calls a JNI function that
 * exists exactly when the surface does. On armeabi-v7a the surface is
 * not compiled (the overlay's section 10 mirrors the DS/PSP stance),
 * the call throws UnsatisfiedLinkError, and the caller
 * (MesenGameSession.coreAvailable) turns that into a plain "not in
 * this build". Fail-closed, like every other probe in this app.
 */
object MesenAccessibilityScript
{
    private const val TAG = "MesenAccess"

    /** The cartridge-image extensions of the two consoles. */
    val NES_EXTENSIONS = setOf("nes", "fds", "unf")
    val SNES_EXTENSIONS = setOf("sfc", "smc", "fig", "swc")
    val ROM_EXTENSIONS = NES_EXTENSIONS + SNES_EXTENSIONS

    // Native side (MesenSessionJNI.cpp).
    private external fun nativeMesenAvailable(): Boolean
    private external fun setMesenSpeechBridge(bridge: Any?)
    private external fun nativeMesenLoadRom(romPath: String, homePath: String): Boolean
    private external fun nativeMesenGameCode(): String
    private external fun nativeMesenLastError(): String
    private external fun nativeMesenBackendId(): String
    private external fun nativeMesenReaderSet(): String
    private external fun nativeMesenSetScriptDir(dir: String)
    private external fun nativeMesenStart(): Boolean
    private external fun nativeMesenStop()
    private external fun nativeMesenRunning(): Boolean
    private external fun nativeMesenFrame(): Boolean
    private external fun nativeMesenFramesCompleted(): Long
    private external fun nativeMesenSetButton(button: Int, down: Boolean)
    private external fun nativeMesenTapButton(button: Int, frames: Int)
    private external fun nativeMesenReadAudio(out: ShortArray, maxFrames: Int): Int
    private external fun nativeMesenSetAudioEnabled(enabled: Boolean)
    private external fun nativeMesenSaveState(path: String): Boolean
    private external fun nativeMesenLoadState(path: String): Boolean
    private external fun nativeMesenAdapterId(): String
    private external fun nativeMesenAdapterName(): String
    private external fun nativeMesenAdapterReady(): Boolean
    private external fun nativeMesenAdapterCommand(command: Int): Boolean
    private external fun nativeMesenCuePoll(ints: IntArray, floats: FloatArray): Boolean
    private external fun nativeMesenCopyFrame(out: IntArray, sizeOut: IntArray): Int

    /**
     * True when the Mesen surface is linked into this build. Throws
     * UnsatisfiedLinkError where it is not — that throw IS the
     * negative answer, and it belongs to the caller to catch.
     */
    fun isCoreLinked(): Boolean = nativeMesenAvailable()

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
     * Creates the core and loads a cartridge image. [homePath] is
     * Mesen's home folder for this game (see the class doc) — the
     * caller owns choosing it; MesenRomResolver puts it under the
     * app's internal storage so battery saves survive between
     * sessions without going through SAF.
     */
    fun loadRom(romPath: String, homePath: String): Boolean
    {
        val ok = nativeMesenLoadRom(romPath, homePath)
        if (ok)
        {
            Log.i(TAG, "loaded $romPath (game code ${nativeMesenGameCode()})")
        }
        else
        {
            Log.e(TAG, "load failed for $romPath: ${nativeMesenLastError()}")
        }
        return ok
    }

    /** The loaded game's code (a Mesen database id, or "crc:<hex>" for SNES), or "" before a load. */
    fun gameCode(): String = nativeMesenGameCode()

    /** The core's own words for the last failure, or "". */
    fun lastError(): String = nativeMesenLastError()

    /**
     * The running backend's registry id — "nes" or "snes" — or ""
     * before a load. The session shapes its pad and its state group
     * from this (the console the core resolved), never from the
     * picked file's extension.
     */
    fun backendId(): String = nativeMesenBackendId()

    /**
     * The bundled reader set the loaded NES ROM needs, named by the
     * core from the ROM's own CRC32, or "" — the NORMAL answer for a
     * game no reader covers, and for every SNES game (their reader is
     * a native adapter, not a set). Never a guess: a reader narrating
     * the wrong game is worse than silence.
     */
    fun readerSet(): String = nativeMesenReaderSet()

    /**
     * Hands the core the staged (writable) copy of the reader set.
     * Call after [loadRom] and before [startCore] — the start is what
     * loads the reader.
     */
    fun setScriptDir(dir: String)
    {
        nativeMesenSetScriptDir(dir)
    }

    fun startCore(): Boolean = nativeMesenStart()

    /** Stops AND destroys the core. Safe to call with nothing loaded. */
    fun stopCore()
    {
        nativeMesenStop()
    }

    val isRunning: Boolean get() = nativeMesenRunning()

    /** One emulated frame. False when the core stopped answering. */
    fun runFrame(): Boolean = nativeMesenFrame()

    val framesCompleted: Long get() = nativeMesenFramesCompleted()

    /** Latch one button (a POKE_BTN_* id from pokecore.h). */
    fun setButton(button: Int, down: Boolean)
    {
        nativeMesenSetButton(button, down)
    }

    /**
     * Tap one button for [frames] emulated frames (poke_tap_button):
     * the core itself holds the press across frame boundaries and
     * releases it, so the tap is sampled by the game whatever the
     * host's frame rate is doing.
     */
    fun tapButton(button: Int, frames: Int)
    {
        nativeMesenTapButton(button, frames)
    }

    /**
     * Drains the console's audio into [out] (interleaved stereo s16 —
     * the host mixes at 32768 Hz, the rate its ABI documents). Returns
     * the number of FRAMES written; 0 when the core has nothing to
     * give.
     *
     * ⛔ TODAY THE MESEN BACKENDS HAND OUT SILENCE, IN THE SHARED CORE
     * ITSELF: their read_audio slots are unwired by design
     * (Core/oga_core.cpp — Mesen mixes into a device the glue has not
     * wired), so this returns 0 on iOS too. The drain exists so the
     * session's one output path (game audio plus the cue synth) is
     * already in place the day the core's audio is wired; the
     * readers' speech does not travel this path and is unaffected.
     */
    fun readAudio(out: ShortArray, maxFrames: Int): Int = nativeMesenReadAudio(out, maxFrames)

    /** The game-sound switch (Settings): the host gates its own output. */
    fun setAudioEnabled(enabled: Boolean)
    {
        nativeMesenSetAudioEnabled(enabled)
    }

    fun saveState(path: String): Boolean = nativeMesenSaveState(path)

    fun loadState(path: String): Boolean = nativeMesenLoadState(path)

    /**
     * Installs the app's shared speech bridge with the Mesen host (or
     * clears it, with null, at session end). Reader speech lands on
     * the same AccessibilitySpeech instance the other hosts use.
     */
    fun setSpeechBridge(bridge: Any?)
    {
        setMesenSpeechBridge(bridge)
    }

    /** The loaded game's adapter id ("alttp"; the NES seam's "nes"), or "" when the game has none. */
    fun adapterId(): String = nativeMesenAdapterId()

    /** The selected adapter's display name, or "" when the game has none. */
    fun adapterName(): String = nativeMesenAdapterName()

    /**
     * Whether the selected adapter has attached and reports ready.
     * The native side attaches lazily on this call, so the session
     * polls it once a frame rather than expecting a one-shot answer.
     */
    fun adapterReady(): Boolean = nativeMesenAdapterReady()

    /**
     * Sends one oga::Command (its raw value — see MesenAdapterCommands
     * in MesenGameSession) to the selected adapter. False is a
     * refusal: no adapter, out of range, or not ready yet; the caller
     * decides what a refusal says.
     */
    fun adapterCommand(command: Int): Boolean = nativeMesenAdapterCommand(command)

    /**
     * One frame of battle-cue data from the host's single shared
     * snapshot, packed field-for-field like the DS and PSP polls. No
     * NES or SNES reader raises these cues today, so a null (silence)
     * answer is the expected one in play; see MesenSessionJNI's note.
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
     * nothing to cue — null is silence, never an error, exactly the
     * fail-closed shape of the DS/PSP polls. Call once per frame,
     * after the frame has run.
     */
    fun cuePoll(): CuePoll?
    {
        if (!nativeMesenCuePoll(cueInts, cueFloats)) return null
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

    // The picture's scratch, sized once for the largest frame either
    // console produces (SNES high resolution, 512x478) and reused
    // across polls: the GamePicture poll runs at ~30 Hz for as long
    // as a game is on screen, and a fresh array per poll is garbage
    // the PSP twin learned not to make. Main-thread confined, like
    // the PSP surface's frame.
    private val framePixels = IntArray(512 * 478)
    private val frameSize = IntArray(2)

    /**
     * The running game's current frame as (pixels, width, height) —
     * RGBA byte-order words, the contract PspAccessibilityScript's
     * currentFrame established — or null when no frame is available.
     * The picture is decorative (see GamePicture); a blind player
     * never depends on it.
     */
    fun currentFrame(): Triple<IntArray, Int, Int>?
    {
        val count = try
        {
            nativeMesenCopyFrame(framePixels, frameSize)
        }
        catch (t: Throwable)
        {
            return null
        }
        if (count <= 0) return null
        val width = frameSize[0]
        val height = frameSize[1]
        if (width <= 0 || height <= 0 || count > framePixels.size) return null
        return Triple(framePixels, width, height)
    }
}
