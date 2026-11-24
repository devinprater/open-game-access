package me.magnum.melonds.accessibility

import android.net.Uri
import android.util.Log

/**
 * The Sony PlayStation side: the shared Core host (`Core/pokecore.cpp`
 * — the same translation unit the iOS app ships) driven through the
 * poke_* C ABI in `Sources/CPokeCore/include/pokecore.h`, via
 * Ps1SessionJNI.cpp. pokecore routes the disc to the PCSX ReARMed
 * backend (Core/ps1_core.cpp) by extension. A sibling of
 * [GenesisAccessibilityScript], with the same shape and the same
 * fail-closed availability probe.
 *
 * ⛔ NO READER YET. The registry's PlayStation row is a console row
 * whose attach refuses: no PlayStation reader exists. So this surface
 * is pad, states, memory card, audio and picture. The adapter forwards
 * at the bottom let the session ask and be told that truth.
 *
 * ⛔ THE BIOS IS OPTIONAL. With no BIOS file the core runs its own HLE
 * BIOS, which boots most games. The host looks for a real one in the
 * disc's own folder (see Ps1RomResolver).
 *
 * ⛔ THE SAVE PATH IS THE MEMORY CARD. The wrapper restores the raw
 * 128 KiB card at load and writes it back on stop and before a state
 * save when it changed (ps1_core.h's contract).
 *
 * ⛔ AVAILABILITY IS A NATIVE QUESTION. [isCoreLinked] calls a JNI
 * function that exists exactly when the PlayStation glue was compiled
 * (64-bit ABIs); on armeabi-v7a it throws UnsatisfiedLinkError, which
 * Ps1GameSession.coreAvailable turns into "not in this build".
 */
object Ps1AccessibilityScript
{
    private const val TAG = "Ps1Access"

    /**
     * The disc-image extensions this path handles — exactly the shared
     * Core resolver's reachable PlayStation set (systems.cpp's kPs1Ext
     * minus .iso/.pbp/.bin, which the PSP and Genesis rows claim
     * first). A .cue and an .m3u name other files beside them, so they
     * need those files in the same folder; .chd is one file per disc.
     */
    val ROM_EXTENSIONS = setOf("cue", "chd", "m3u", "ccd")

    /** The extensions that only list other files (tracks or discs). */
    val INDEX_EXTENSIONS = setOf("cue", "m3u", "ccd")

    // Native side (Ps1SessionJNI.cpp).
    private external fun nativePs1Available(): Boolean
    private external fun setPs1SpeechBridge(bridge: Any?)
    private external fun nativePs1LoadRom(romPath: String, savePath: String): Boolean
    private external fun nativePs1GameCode(): String
    private external fun nativePs1LastError(): String
    private external fun nativePs1Start(): Boolean
    private external fun nativePs1Stop()
    private external fun nativePs1Running(): Boolean
    private external fun nativePs1Frame(): Boolean
    private external fun nativePs1FramesCompleted(): Long
    private external fun nativePs1SetButton(button: Int, down: Boolean)
    private external fun nativePs1TapButton(button: Int, frames: Int)
    private external fun nativePs1ReadAudio(out: ShortArray, maxFrames: Int): Int
    private external fun nativePs1SetAudioEnabled(enabled: Boolean)
    private external fun nativePs1SaveState(path: String): Boolean
    private external fun nativePs1LoadState(path: String): Boolean
    private external fun nativePs1CopyFrame(out: IntArray, sizeOut: IntArray): Int
    private external fun nativePs1AdapterId(): String
    private external fun nativePs1AdapterName(): String
    private external fun nativePs1AdapterReady(): Boolean
    private external fun nativePs1AdapterCommand(command: Int): Boolean

    /**
     * True when the PlayStation glue is linked into this build. Throws
     * UnsatisfiedLinkError where it is not — that throw IS the
     * negative answer, and it belongs to the caller to catch.
     */
    fun isCoreLinked(): Boolean = nativePs1Available()

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
     * game's memory card file; the caller owns choosing it — see
     * Ps1RomResolver.savePathFor.
     */
    fun loadRom(romPath: String, savePath: String): Boolean
    {
        val ok = nativePs1LoadRom(romPath, savePath)
        if (ok)
        {
            Log.i(TAG, "loaded $romPath (game code ${nativePs1GameCode()})")
        }
        else
        {
            Log.e(TAG, "load failed for $romPath: ${nativePs1LastError()}")
        }
        return ok
    }

    /**
     * The loaded game's identity: the disc's boot serial ("SLUS00594",
     * as the core stores it), or "" for a disc with no serial and
     * before a load.
     */
    fun gameCode(): String = nativePs1GameCode()

    /** The core's own words for the last failure, or "". */
    fun lastError(): String = nativePs1LastError()

    fun startCore(): Boolean = nativePs1Start()

    /** Stops AND destroys the core. Safe to call with nothing loaded. */
    fun stopCore()
    {
        nativePs1Stop()
    }

    val isRunning: Boolean get() = nativePs1Running()

    /** One emulated frame. False when the core stopped answering. */
    fun runFrame(): Boolean = nativePs1Frame()

    val framesCompleted: Long get() = nativePs1FramesCompleted()

    /** Latch one PlayStation button (a POKE_BTN_* id from pokecore.h). */
    fun setButton(button: Int, down: Boolean)
    {
        nativePs1SetButton(button, down)
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
        nativePs1TapButton(button, frames)
    }

    /**
     * Drains the console's audio into [out] (interleaved stereo s16,
     * drained by the session's AudioTrack thread — the wrapper resamples
     * to 32768 Hz, the rate its ABI documents). Returns the number of
     * FRAMES written; 0 when the core has nothing to give (or game
     * sound is off).
     */
    fun readAudio(out: ShortArray, maxFrames: Int): Int = nativePs1ReadAudio(out, maxFrames)

    /** The game-sound switch (Settings): pokecore gates its own output. */
    fun setAudioEnabled(enabled: Boolean)
    {
        nativePs1SetAudioEnabled(enabled)
    }

    fun saveState(path: String): Boolean = nativePs1SaveState(path)

    fun loadState(path: String): Boolean = nativePs1LoadState(path)

    // The frame copy's scratch, reused across polls: the picture poll
    // is confined to the GamePicture path, and the buffer covers the
    // largest frame ps1_core.h allows (1024x512), so no PlayStation
    // frame can outgrow it.
    private val framePixels = IntArray(1024 * 512)
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
        val count = nativePs1CopyFrame(framePixels, frameSize)
        if (count <= 0) return null
        return Triple(framePixels, frameSize[0], frameSize[1])
    }

    /**
     * Installs the app's shared speech bridge with the PlayStation host
     * (or clears it, with null, at session end). Speech lands on the
     * same AccessibilitySpeech instance the other hosts speak through.
     */
    fun setSpeechBridge(bridge: Any?)
    {
        setPs1SpeechBridge(bridge)
    }

    /** The loaded game's adapter id ("ps1"), or "" before a load. */
    fun adapterId(): String = nativePs1AdapterId()

    /** The loaded game's adapter display name ("PlayStation"), or "" before a load. */
    fun adapterName(): String = nativePs1AdapterName()

    /**
     * Whether the selected adapter has attached and reports ready.
     * For the PlayStation this never answers true yet — the console
     * row's attach refuses (there is no PlayStation reader) — and the
     * session draws no reader controls from the answer.
     */
    fun adapterReady(): Boolean = nativePs1AdapterReady()

    /**
     * Sends one oga::Command (its raw value) to the selected adapter.
     * For the PlayStation the answer is a refusal until a reader exists —
     * kept as a forward so the session's speech plumbing
     * can ask the same question every other session asks.
     */
    fun adapterCommand(command: Int): Boolean = nativePs1AdapterCommand(command)
}
