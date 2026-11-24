package me.magnum.melonds.accessibility

import android.net.Uri
import android.util.Log

/**
 * The Nintendo 64 side: the shared Core host (`Core/pokecore.cpp` — the
 * same translation unit the iOS app ships) driven through the poke_* C
 * ABI in `Sources/CPokeCore/include/pokecore.h`, via N64SessionJNI.cpp.
 * pokecore routes the cartridge to the Mupen64Plus-Next backend
 * (Core/n64_core.cpp) by extension. A sibling of
 * [Ps1AccessibilityScript], with the same shape and the same fail-closed
 * availability probe, plus the analog stick ([setStick]).
 *
 * ⛔ NO READER YET. The registry's Nintendo 64 row is a console row whose
 * attach refuses: no N64 reader exists. So this surface is pad, stick,
 * states, the battery save, audio and picture. The adapter forwards at
 * the bottom let the session ask and be told that truth.
 *
 * ⛔ THE SAVE PATH IS THE BATTERY FILE. The wrapper restores it (EEPROM,
 * SRAM, FlashRAM and the Controller Paks, one block) at load and writes
 * it back on stop and on a state save when the game changed it
 * (n64_core.h's contract).
 *
 * ⛔ AVAILABILITY IS A NATIVE QUESTION. [isCoreLinked] calls a JNI
 * function that exists exactly when the Nintendo 64 glue was compiled
 * (64-bit ABIs); on armeabi-v7a it throws UnsatisfiedLinkError, which
 * N64GameSession.coreAvailable turns into "not in this build".
 */
object N64AccessibilityScript
{
    private const val TAG = "N64Access"

    /**
     * The cartridge extensions this path handles — exactly the shared
     * Core resolver's N64 set (systems.cpp's kN64Ext): the three byte
     * orders a dump comes in (.z64 big-endian, .v64 byte-swapped, .n64
     * little-endian). The core normalises them; one file per game.
     */
    val ROM_EXTENSIONS = setOf("z64", "n64", "v64")

    // Native side (N64SessionJNI.cpp).
    private external fun nativeN64Available(): Boolean
    private external fun setN64SpeechBridge(bridge: Any?)
    private external fun nativeN64LoadRom(romPath: String, savePath: String): Boolean
    private external fun nativeN64GameCode(): String
    private external fun nativeN64LastError(): String
    private external fun nativeN64Start(): Boolean
    private external fun nativeN64Stop()
    private external fun nativeN64Running(): Boolean
    private external fun nativeN64Frame(): Boolean
    private external fun nativeN64FramesCompleted(): Long
    private external fun nativeN64SetButton(button: Int, down: Boolean)
    private external fun nativeN64TapButton(button: Int, frames: Int)
    private external fun nativeN64SetStick(x: Float, y: Float)
    private external fun nativeN64ReadAudio(out: ShortArray, maxFrames: Int): Int
    private external fun nativeN64SetAudioEnabled(enabled: Boolean)
    private external fun nativeN64SaveState(path: String): Boolean
    private external fun nativeN64LoadState(path: String): Boolean
    private external fun nativeN64CopyFrame(out: IntArray, sizeOut: IntArray): Int
    private external fun nativeN64AdapterId(): String
    private external fun nativeN64AdapterName(): String
    private external fun nativeN64AdapterReady(): Boolean
    private external fun nativeN64AdapterCommand(command: Int): Boolean

    /**
     * True when the Nintendo 64 glue is linked into this build. Throws
     * UnsatisfiedLinkError where it is not — that throw IS the
     * negative answer, and it belongs to the caller to catch.
     */
    fun isCoreLinked(): Boolean = nativeN64Available()

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
     * Creates the core and loads a cartridge. [savePath] is the game's
     * battery file; the caller owns choosing it — see
     * N64RomResolver.savePathFor.
     */
    fun loadRom(romPath: String, savePath: String): Boolean
    {
        val ok = nativeN64LoadRom(romPath, savePath)
        if (ok)
        {
            Log.i(TAG, "loaded $romPath (game code ${nativeN64GameCode()})")
        }
        else
        {
            Log.e(TAG, "load failed for $romPath: ${nativeN64LastError()}")
        }
        return ok
    }

    /**
     * The loaded game's identity: "n64:" and the cartridge ID from the
     * ROM header ("n64:NPOE"), or "" before a load.
     */
    fun gameCode(): String = nativeN64GameCode()

    /** The core's own words for the last failure, or "". */
    fun lastError(): String = nativeN64LastError()

    fun startCore(): Boolean = nativeN64Start()

    /** Stops AND destroys the core. Safe to call with nothing loaded. */
    fun stopCore()
    {
        nativeN64Stop()
    }

    val isRunning: Boolean get() = nativeN64Running()

    /** One emulated frame. False when the core stopped answering. */
    fun runFrame(): Boolean = nativeN64Frame()

    val framesCompleted: Long get() = nativeN64FramesCompleted()

    /** Latch one Nintendo 64 button (a POKE_BTN_* id from pokecore.h). */
    fun setButton(button: Int, down: Boolean)
    {
        nativeN64SetButton(button, down)
    }

    /**
     * Tap one Nintendo 64 button for [frames] emulated frames
     * (poke_tap_button): the core itself holds the press across frame
     * boundaries and releases it, so the tap is sampled by the game
     * whatever the host's frame rate is doing — the guarantee a
     * wall-clock press/release pair cannot make.
     */
    fun tapButton(button: Int, frames: Int)
    {
        nativeN64TapButton(button, frames)
    }

    /**
     * The analog stick, each axis -1..1, +x right, +y DOWN (the screen
     * convention n64_core.h takes). Most N64 games move with the stick,
     * so the session drives it from the controller's left stick and from
     * the on-screen stick buttons. (0, 0) is centred.
     */
    fun setStick(x: Float, y: Float)
    {
        nativeN64SetStick(x, y)
    }

    /**
     * Drains the console's audio into [out] (interleaved stereo s16,
     * drained by the session's AudioTrack thread — the wrapper resamples
     * to 32768 Hz, the rate n64_core.h documents). Returns the number of
     * FRAMES written; 0 when the core has nothing to give (or game
     * sound is off).
     */
    fun readAudio(out: ShortArray, maxFrames: Int): Int = nativeN64ReadAudio(out, maxFrames)

    /** The game-sound switch (Settings): pokecore gates its own output. */
    fun setAudioEnabled(enabled: Boolean)
    {
        nativeN64SetAudioEnabled(enabled)
    }

    fun saveState(path: String): Boolean = nativeN64SaveState(path)

    fun loadState(path: String): Boolean = nativeN64LoadState(path)

    // The frame copy's scratch, reused across polls: the picture poll
    // is confined to the GamePicture path, and the buffer covers the
    // largest frame the software renderer hands out (640x480, the
    // high-resolution modes; most games are 320x240), with room to spare.
    private val framePixels = IntArray(640 * 576)
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
        val count = nativeN64CopyFrame(framePixels, frameSize)
        if (count <= 0) return null
        return Triple(framePixels, frameSize[0], frameSize[1])
    }

    /**
     * Installs the app's shared speech bridge with the Nintendo 64 host
     * (or clears it, with null, at session end). Speech lands on the
     * same AccessibilitySpeech instance the other hosts speak through.
     */
    fun setSpeechBridge(bridge: Any?)
    {
        setN64SpeechBridge(bridge)
    }

    /** The loaded game's adapter id ("n64"), or "" before a load. */
    fun adapterId(): String = nativeN64AdapterId()

    /** The loaded game's adapter display name ("Nintendo 64"), or "" before a load. */
    fun adapterName(): String = nativeN64AdapterName()

    /**
     * Whether the selected adapter has attached and reports ready.
     * For the Nintendo 64 this never answers true yet — the console
     * row's attach refuses (there is no N64 reader) — and the
     * session draws no reader controls from the answer.
     */
    fun adapterReady(): Boolean = nativeN64AdapterReady()

    /**
     * Sends one oga::Command (its raw value) to the selected adapter.
     * For the Nintendo 64 the answer is a refusal until a reader exists —
     * kept as a forward so the session's speech plumbing
     * can ask the same question every other session asks.
     */
    fun adapterCommand(command: Int): Boolean = nativeN64AdapterCommand(command)
}
