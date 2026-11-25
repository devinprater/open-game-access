package me.magnum.melonds.accessibility

import android.net.Uri
import android.util.Log

/**
 * The Sony PlayStation 2 side: the shared Core host (`Core/pokecore.cpp`
 * — the same translation unit the iOS app ships) driven through the
 * poke_* C ABI in `Sources/CPokeCore/include/pokecore.h`, via
 * Ps2SessionJNI.cpp. pokecore routes the disc to the PlayStation 2
 * backend (Core/ps2_core.cpp — the wrapper that dlopen()s a libretro
 * core, Play! or ARMSX2) by its header for the shared .iso name. A
 * sibling of [Ps2AccessibilityScript], with the same shape and the
 * same fail-closed availability probe.
 *
 * ⛔ NO READER YET. The registry's PlayStation 2 row is a console row
 * whose attach refuses: no PlayStation 2 reader exists. So this
 * surface is pad, states, memory cards, audio and picture. The adapter
 * forwards at the bottom let the session ask and be told that truth.
 *
 * ⛔ THE BIOS DECIDES THE CORE. With a BIOS staged, the wrapper
 * prefers ARMSX2; with none, Play! runs its HLE BIOS, which boots
 * most games. The session stages the player's BIOS beside the disc
 * (see Ps2RomResolver) and hands both folders over before the load
 * ([setCoreDir], [setSystemDir]).
 *
 * ⛔ THE SAVE PATH IS A FOLDER. The cores keep their own memory-card
 * files in it (the one deliberate difference from the PlayStation,
 * whose save path is the card file itself).
 *
 * ⛔ AVAILABILITY IS A NATIVE QUESTION. [isCoreLinked] calls a JNI
 * function that exists exactly when the PlayStation 2 wrapper was
 * compiled (arm64-v8a only — the pinned cores are arm64 builds); on
 * every other ABI it throws UnsatisfiedLinkError, which
 * Ps2GameSession.coreAvailable turns into "not in this build".
 */
object Ps2AccessibilityScript
{
    private const val TAG = "Ps2Access"

    /**
     * The disc-image extensions this path handles BY NAME: none. Both
     * of the PlayStation 2's formats are shared names — .iso is the
     * PSP's and .chd the PlayStation's — so a PlayStation 2 game is
     * claimed by its header ([headerIsPs2]), never by name alone. (A
     * PS2 .chd carries no header the sniff can read, so in this build
     * a .chd still routes to the PlayStation session, which refuses
     * it in words; the core itself accepts CHD — see ps2_core.h.)
     */
    val ROM_EXTENSIONS = emptySet<String>()

    /**
     * The shared name a PlayStation 2 disc may carry in this build:
     * .iso (the PSP's by name). It is decided by the file's first
     * bytes ([headerIsPs2]), the same test the core's own resolver
     * runs, so the launcher and the core never disagree.
     */
    val SNIFFED_EXTENSIONS = setOf("iso")

    /** How many leading bytes [headerIsPs2] needs (systems.h's OGA_DISC_HEADER_BYTES). */
    const val HEADER_BYTES = 65536

    // Native side (Ps2SessionJNI.cpp).
    private external fun nativePs2Available(): Boolean
    private external fun setPs2SpeechBridge(bridge: Any?)
    private external fun nativePs2LoadRom(romPath: String, savePath: String): Boolean
    private external fun nativePs2GameCode(): String
    private external fun nativePs2LastError(): String
    private external fun nativePs2Start(): Boolean
    private external fun nativePs2Stop()
    private external fun nativePs2Running(): Boolean
    private external fun nativePs2Frame(): Boolean
    private external fun nativePs2FramesCompleted(): Long
    private external fun nativePs2SetButton(button: Int, down: Boolean)
    private external fun nativePs2TapButton(button: Int, frames: Int)
    private external fun nativePs2ReadAudio(out: ShortArray, maxFrames: Int): Int
    private external fun nativePs2SetAudioEnabled(enabled: Boolean)
    private external fun nativePs2SaveState(path: String): Boolean
    private external fun nativePs2LoadState(path: String): Boolean
    private external fun nativePs2CopyFrame(out: IntArray, sizeOut: IntArray): Int
    private external fun nativePs2AdapterId(): String
    private external fun nativePs2AdapterName(): String
    private external fun nativePs2AdapterReady(): Boolean
    private external fun nativePs2AdapterCommand(command: Int): Boolean
    private external fun nativePs2CuePoll(ints: IntArray, floats: FloatArray): Boolean
    private external fun nativePs2SetDebugDir(dir: String)
    private external fun nativePs2DebugCommand(text: String)
    private external fun nativePs2SetCoreDir(dir: String)
    private external fun nativePs2SetSystemDir(dir: String)
    private external fun nativePs2SetAnalog(stick: Int, x: Float, y: Float)
    private external fun nativePs2FrameRate(): Double
    private external fun nativePs2HeaderIsPs2(header: ByteArray, length: Int): Boolean

    /**
     * True when the PlayStation 2 wrapper is linked into this build.
     * Throws UnsatisfiedLinkError where it is not — that throw IS the
     * negative answer, and it belongs to the caller to catch.
     */
    fun isCoreLinked(): Boolean = nativePs2Available()

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
     * Creates the core and loads a disc image. [savePath] is the
     * game's memory-card FOLDER; the caller owns choosing it — see
     * Ps2RomResolver.savePathFor.
     */
    fun loadRom(romPath: String, savePath: String): Boolean
    {
        val ok = nativePs2LoadRom(romPath, savePath)
        if (ok)
        {
            Log.i(TAG, "loaded $romPath (game code ${nativePs2GameCode()})")
        }
        else
        {
            Log.e(TAG, "load failed for $romPath: ${nativePs2LastError()}")
        }
        return ok
    }

    /**
     * The loaded game's identity: the disc's boot serial ("SLUS20998",
     * as the core stores it), or "" for a disc with no serial (a CHD,
     * whose SYSTEM.CNF the wrapper does not parse) and before a load.
     */
    fun gameCode(): String = nativePs2GameCode()

    /** The core's own words for the last failure, or "". */
    fun lastError(): String = nativePs2LastError()

    fun startCore(): Boolean = nativePs2Start()

    /** Stops AND destroys the core. Safe to call with nothing loaded. */
    fun stopCore()
    {
        nativePs2Stop()
    }

    val isRunning: Boolean get() = nativePs2Running()

    /** One emulated frame. False when the core stopped answering. */
    fun runFrame(): Boolean = nativePs2Frame()

    val framesCompleted: Long get() = nativePs2FramesCompleted()

    /** Latch one PlayStation button (a POKE_BTN_* id from pokecore.h). */
    fun setButton(button: Int, down: Boolean)
    {
        nativePs2SetButton(button, down)
    }

    /**
     * Tap one PlayStation button for [frames] emulated frames
     * (poke_tap_button): the core itself holds the press across frame
     * boundaries and releases it, so the tap is sampled by the game
     * whatever the host's frame rate is doing — the guarantee a
     * wall-clock press/release pair cannot make.
     */
    fun tapButton(button: Int, frames: Int)
    {
        nativePs2TapButton(button, frames)
    }

    /**
     * Drains the console's audio into [out] (interleaved stereo s16,
     * drained by the session's AudioTrack thread — the wrapper
     * resamples its core's own rate to 32768 Hz, the rate its ABI
     * documents). Returns the number of FRAMES written; 0 when the
     * core has nothing to give (or game sound is off).
     */
    fun readAudio(out: ShortArray, maxFrames: Int): Int = nativePs2ReadAudio(out, maxFrames)

    /** The game-sound switch (Settings): pokecore gates its own output. */
    fun setAudioEnabled(enabled: Boolean)
    {
        nativePs2SetAudioEnabled(enabled)
    }

    /**
     * One DualShock 2 stick: [stick] 0 the left, 1 the right; each
     * axis -1..1, +x right, +y down.
     */
    fun setAnalog(stick: Int, x: Float, y: Float)
    {
        nativePs2SetAnalog(stick, x, y)
    }

    /** The loaded disc's frame rate (50 PAL, about 59.94 NTSC), or 0 before a load. */
    fun frameRate(): Double = nativePs2FrameRate()

    /**
     * True when [header] (the first [length] bytes of a file, up to
     * [HEADER_BYTES]) is a PlayStation 2 disc: an ISO 9660 volume whose
     * system id is PLAYSTATION and whose size is a DVD's, not a CD's —
     * the line that keeps the two PlayStations' sniffs disjoint. Throws
     * UnsatisfiedLinkError on a build without the PlayStation 2
     * wrapper, like every call here.
     */
    fun headerIsPs2(header: ByteArray, length: Int): Boolean = nativePs2HeaderIsPs2(header, length)

    fun saveState(path: String): Boolean = nativePs2SaveState(path)

    fun loadState(path: String): Boolean = nativePs2LoadState(path)

    // The frame copy's scratch, reused across polls: the picture poll
    // is confined to the GamePicture path, and the buffer covers the
    // largest frame a PS2 core presents in this build (1280x1024), so
    // no PlayStation 2 frame can outgrow it.
    private val framePixels = IntArray(1280 * 1024)
    private val frameSize = IntArray(2)

    /**
     * The current frame as RGBA8888 ints, plus its size — the twin of
     * PspAccessibilityScript.currentFrame, same contract: returns
     * (pixels, width, height), or null when nothing is loaded or the
     * core cannot hand a frame back. The pixels come from the poke_*
     * ABI's own framebuffer staging (poke_framebuffer fills it,
     * poke_framebuffer_ptr hands it back), so this is a copy of a
     * frame the core already produced, never a second render.
     */
    fun currentFrame(): Triple<IntArray, Int, Int>?
    {
        val count = nativePs2CopyFrame(framePixels, frameSize)
        if (count <= 0) return null
        return Triple(framePixels, frameSize[0], frameSize[1])
    }

    /**
     * Installs the app's shared speech bridge with the PlayStation 2
     * host (or clears it, with null, at session end). Speech lands on
     * the same AccessibilitySpeech instance the other hosts speak
     * through.
     */
    fun setSpeechBridge(bridge: Any?)
    {
        setPs2SpeechBridge(bridge)
    }

    /** The loaded game's adapter id ("ps2"), or "" before a load. */
    fun adapterId(): String = nativePs2AdapterId()

    /** The loaded game's adapter display name ("PlayStation 2"), or "" before a load. */
    fun adapterName(): String = nativePs2AdapterName()

    /**
     * Whether the selected adapter has attached and reports ready.
     * For the PlayStation 2 this never answers true yet — the console
     * row's attach refuses (there is no PlayStation 2 reader) — and
     * the session draws no reader controls from the answer.
     */
    fun adapterReady(): Boolean = nativePs2AdapterReady()

    /**
     * Sends one oga::Command (its raw value) to the selected adapter.
     * For the PlayStation 2 the answer is a refusal until a reader
     * exists — kept as a forward so the session's speech plumbing can
     * ask the same question every other session asks.
     */
    fun adapterCommand(command: Int): Boolean = nativePs2AdapterCommand(command)

    /** One frame's reader cues: the beacon state and distance, and the one-shot hit beeps. */
    data class CuePoll(
        val state: Int,
        val dist: Float,
        val healthBits: Int,
        val foeHz: Float,
        val foePan: Float,
        val ownHz: Float,
        val ownPan: Float,
    )

    private val cueInts = IntArray(2)
    private val cueFloats = FloatArray(5)

    /**
     * Polls the cue snapshot for this frame, or null when there is nothing to cue (no core, no
     * reader attached yet) - null is silence, never an error. Call once per frame, after the
     * frame has run, from the frame thread only: the native side takes one snapshot per call and
     * the adapter consumes its one-frame events into it.
     */
    fun cuePoll(): CuePoll?
    {
        if (!nativePs2CuePoll(cueInts, cueFloats)) return null
        return CuePoll(
            state = cueInts[0],
            dist = cueFloats[0],
            healthBits = cueInts[1],
            foeHz = cueFloats[1],
            foePan = cueFloats[2],
            ownHz = cueFloats[3],
            ownPan = cueFloats[4],
        )
    }

    /**
     * The in-game debug channel's folder (docs/design/debug-channel.md): a cmd.txt saved
     * there runs against the live game, and the replies land in replies.log beside it.
     * Kept across loads and applied to every core created after it; "" turns the file off.
     */
    fun setDebugDir(dir: String) { nativePs2SetDebugDir(dir) }

    /** The same commands without the file; runs on the next frame. */
    fun debugCommand(text: String) { nativePs2DebugCommand(text) }

    /**
     * The folder holding the PlayStation 2 core libraries (the app's
     * native library directory, where the overlay stages the pinned
     * cores). Kept across loads and applied to every core created
     * after it; the wrapper searches it at load time.
     */
    fun setCoreDir(dir: String) { nativePs2SetCoreDir(dir) }

    /**
     * The folder the player's BIOS is staged under (as pcsx2/bios):
     * handed to the core as its system folder. With a BIOS there the
     * wrapper prefers ARMSX2; without one Play! runs its HLE BIOS.
     */
    fun setSystemDir(dir: String) { nativePs2SetSystemDir(dir) }
}
