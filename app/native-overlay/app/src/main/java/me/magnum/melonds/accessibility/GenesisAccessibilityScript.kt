package me.magnum.melonds.accessibility

import android.net.Uri
import android.util.Log

/**
 * The Sega Genesis side: the shared Core host (`Core/pokecore.cpp` —
 * the same translation unit the iOS app ships) driven through the
 * poke_* C ABI in `Sources/CPokeCore/include/pokecore.h`, via
 * GenesisSessionJNI.cpp. pokecore routes the cartridge to the Genesis
 * Plus GX backend (Core/genesis_core.cpp) by extension, the way it
 * routes a DS cartridge to melonDS.
 *
 * ⛔ A SESSION WITHOUT A READER, ON PURPOSE. The registry's Genesis
 * row is a console row whose attach refuses: no Genesis reader exists
 * (no native adapter, no reader script — the backend's ops table
 * names no reader set). So this surface is pad, states, audio and
 * picture only. The adapter forwards at the bottom exist so the
 * session can ask and be told the truth — the id answers "genesis",
 * ready never answers true, commands are refused — not because a
 * reader is hiding behind them. Nothing here invents one.
 *
 * ⛔ NO RUNTIME ASSETS TO EXTRACT, AND NO SCRIPT TO SET. Where the
 * DS path hands its host a reader script before every load, the
 * Genesis backend takes two file paths (the ROM and its battery
 * .sav) and nothing else — Genesis Plus GX needs no BIOS for
 * cartridge games, and none ships.
 *
 * ⛔ THE SAVE PATH IS THE BATTERY FILE ITSELF. The wrapper restores
 * the .sav into the core's SRAM at load and flushes it back on stop
 * when the bytes changed (see GenesisRomResolver for where the file
 * lives, and genesis_core.h for the contract — including the
 * EEPROM/flash carve-out: those cartridges keep saves inside chip
 * emulation and persist through save states only).
 *
 * ⛔ AVAILABILITY IS A NATIVE QUESTION, NOT A PACKAGE QUESTION — the
 * DS probe's reasoning, verbatim: the Genesis glue shares the SAME
 * frontend .so as everything else, so the package cannot tell the
 * cores apart; instead [isCoreLinked] calls a JNI function that
 * exists exactly when the glue does. On armeabi-v7a the glue is not
 * compiled (the overlay's section 10 mirrors sections 8/9), this
 * whole native surface is absent, and the call throws
 * UnsatisfiedLinkError, which the caller
 * (GenesisGameSession.coreAvailable) turns into a plain "not in this
 * build". Fail-closed, like every other probe in this app.
 */
object GenesisAccessibilityScript
{
    private const val TAG = "GenesisAccess"

    /**
     * The cartridge-image extensions this path handles — the shared
     * Core resolver's Genesis set (systems.cpp's kGenesisExt).
     * ".bin" is claimed ON PURPOSE: Genesis is the only .bin claimant
     * with a backend, and the wrapper refuses a .bin that does not
     * identify as a Genesis/Mega Drive cartridge, in words.
     */
    val ROM_EXTENSIONS = setOf("md", "gen", "smd", "bin")

    // Native side (GenesisSessionJNI.cpp).
    private external fun nativeGenesisAvailable(): Boolean
    private external fun setGenesisSpeechBridge(bridge: Any?)
    private external fun nativeGenesisLoadRom(romPath: String, savePath: String): Boolean
    private external fun nativeGenesisGameCode(): String
    private external fun nativeGenesisLastError(): String
    private external fun nativeGenesisStart(): Boolean
    private external fun nativeGenesisStop()
    private external fun nativeGenesisRunning(): Boolean
    private external fun nativeGenesisFrame(): Boolean
    private external fun nativeGenesisFramesCompleted(): Long
    private external fun nativeGenesisSetButton(button: Int, down: Boolean)
    private external fun nativeGenesisTapButton(button: Int, frames: Int)
    private external fun nativeGenesisReadAudio(out: ShortArray, maxFrames: Int): Int
    private external fun nativeGenesisSetAudioEnabled(enabled: Boolean)
    private external fun nativeGenesisSaveState(path: String): Boolean
    private external fun nativeGenesisLoadState(path: String): Boolean
    private external fun nativeGenesisCopyFrame(out: IntArray, sizeOut: IntArray): Int
    private external fun nativeGenesisAdapterId(): String
    private external fun nativeGenesisAdapterName(): String
    private external fun nativeGenesisAdapterReady(): Boolean
    private external fun nativeGenesisAdapterCommand(command: Int): Boolean

    /**
     * True when the Genesis glue is linked into this build. Throws
     * UnsatisfiedLinkError where it is not — that throw IS the
     * negative answer, and it belongs to the caller to catch.
     */
    fun isCoreLinked(): Boolean = nativeGenesisAvailable()

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
     * Creates the core and loads a cartridge image. [savePath] is the
     * game's battery .sav file itself; the caller owns choosing it —
     * GenesisGameSession puts it beside the ROM's cached copy
     * (GenesisRomResolver) so saves survive between sessions without
     * going through SAF.
     */
    fun loadRom(romPath: String, savePath: String): Boolean
    {
        val ok = nativeGenesisLoadRom(romPath, savePath)
        if (ok)
        {
            Log.i(TAG, "loaded $romPath (game code ${nativeGenesisGameCode()})")
        }
        else
        {
            Log.e(TAG, "load failed for $romPath: ${nativeGenesisLastError()}")
        }
        return ok
    }

    /**
     * The loaded game's identity: "crc:<hex>" of the cartridge image
     * the core actually runs (the wrapper's content identity — Genesis
     * ROMs carry no 4-character code), or "" before a load.
     */
    fun gameCode(): String = nativeGenesisGameCode()

    /** The core's own words for the last failure, or "". */
    fun lastError(): String = nativeGenesisLastError()

    fun startCore(): Boolean = nativeGenesisStart()

    /** Stops AND destroys the core. Safe to call with nothing loaded. */
    fun stopCore()
    {
        nativeGenesisStop()
    }

    val isRunning: Boolean get() = nativeGenesisRunning()

    /** One emulated frame. False when the core stopped answering. */
    fun runFrame(): Boolean = nativeGenesisFrame()

    val framesCompleted: Long get() = nativeGenesisFramesCompleted()

    /** Latch one Genesis button (a POKE_BTN_* id from pokecore.h). */
    fun setButton(button: Int, down: Boolean)
    {
        nativeGenesisSetButton(button, down)
    }

    /**
     * Tap one Genesis button for [frames] emulated frames
     * (poke_tap_button): the core itself holds the press across frame
     * boundaries and releases it, so the tap is sampled by the game
     * whatever the host's frame rate is doing — the guarantee a
     * wall-clock press/release pair cannot make.
     */
    fun tapButton(button: Int, frames: Int)
    {
        nativeGenesisTapButton(button, frames)
    }

    /**
     * Drains the console's audio into [out] (interleaved stereo s16,
     * drained by the session's AudioTrack thread — the wrapper mixes
     * at 32768 Hz, the rate its ABI documents). Returns the number of
     * FRAMES written; 0 when the core has nothing to give (or game
     * sound is off).
     */
    fun readAudio(out: ShortArray, maxFrames: Int): Int = nativeGenesisReadAudio(out, maxFrames)

    /** The game-sound switch (Settings): pokecore gates its own output. */
    fun setAudioEnabled(enabled: Boolean)
    {
        nativeGenesisSetAudioEnabled(enabled)
    }

    fun saveState(path: String): Boolean = nativeGenesisSaveState(path)

    fun loadState(path: String): Boolean = nativeGenesisLoadState(path)

    // The frame copy's scratch, reused across polls: the picture poll
    // is confined to the GamePicture path, and the buffer is the
    // core's own maximum viewport (720x576, its bitmap allocation) so
    // no Genesis frame can outgrow it.
    private val framePixels = IntArray(720 * 576)
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
        val count = nativeGenesisCopyFrame(framePixels, frameSize)
        if (count <= 0) return null
        return Triple(framePixels, frameSize[0], frameSize[1])
    }

    /**
     * Installs the app's shared speech bridge with the Genesis host
     * (or clears it, with null, at session end). Speech lands on the
     * same AccessibilitySpeech instance the other hosts speak through.
     */
    fun setSpeechBridge(bridge: Any?)
    {
        setGenesisSpeechBridge(bridge)
    }

    /** The loaded game's adapter id ("genesis"), or "" before a load. */
    fun adapterId(): String = nativeGenesisAdapterId()

    /** The loaded game's adapter display name ("Sega Genesis"), or "" before a load. */
    fun adapterName(): String = nativeGenesisAdapterName()

    /**
     * Whether the selected adapter has attached and reports ready.
     * For Genesis this never answers true — the console row's attach
     * refuses by design (there is no Genesis reader) — and the
     * session draws no reader controls from the answer.
     */
    fun adapterReady(): Boolean = nativeGenesisAdapterReady()

    /**
     * Sends one oga::Command (its raw value) to the selected adapter.
     * For Genesis the answer is always a refusal — no adapter is ever
     * attached — kept as a forward so the session's speech plumbing
     * can ask the same question every other session asks.
     */
    fun adapterCommand(command: Int): Boolean = nativeGenesisAdapterCommand(command)
}
