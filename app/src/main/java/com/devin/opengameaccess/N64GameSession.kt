package com.devin.opengameaccess

import android.app.Activity
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.View
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import me.magnum.melonds.accessibility.AccessibilityScript
import me.magnum.melonds.accessibility.N64AccessibilityScript
import me.magnum.melonds.accessibility.N64RomResolver
import java.io.File

/**
 * The Nintendo 64 session, driven directly from Compose.
 *
 * ⛔ A SIBLING OF [Ps1GameSession], NOT A VARIANT. Same shape — an object
 * the screen calls, a Compose-observable running flag, a last error, held
 * input, an audio drain thread, an asynchronous start — over the shared
 * Core host (Core/pokecore.cpp) and its Mupen64Plus-Next backend
 * (Core/n64_core.cpp). The launcher keeps the sessions apart by the picked
 * file's extension.
 *
 * ⛔ FRAMES RUN ON THEIR OWN THREAD, NOT THE COMPOSE FRAME CLOCK. The
 * other sessions run one emulated frame per screen frame on the main
 * thread, which is fine for consoles a phone emulates in a millisecond or
 * two. The N64 core runs on its software path (cached interpreter,
 * angrylion video, no JIT, the same as iOS), where one frame can take
 * longer than a screen frame. On the main thread that would stall the
 * whole screen, and TalkBack with it. So [frameLoop] paces frames to the
 * console on a worker; when the phone cannot keep up the game runs slow
 * and the screen stays responsive. Everything that touches the core's
 * whole state between frames (the picture copy, save and load state)
 * takes [frameLock]; the pad and the stick only latch values, which the
 * native contract allows from any thread.
 *
 * ⛔ NO READER YET, SAID PLAINLY. The registry's Nintendo 64 row refuses to
 * attach: no N64 reader exists. So this session is pad, stick, save
 * states, the battery save, audio and the game picture — an N64 game in
 * this build is playable, but not accessible. Where Am I and the
 * controller layer's reader chords answer with the honest line the other
 * reader-less cases use.
 *
 * ⛔ THE PAD RIDES THE SHARED SLOTS (see [N64Keys]): A, B, Start and the
 * d-pad as themselves, Z on L, R on R, C-left on X, C-right on Y and
 * C-down on Select — the iOS mapping. C-up and the N64's own L button have
 * no slot yet. The analog stick is separate ([setStick]): most N64 games
 * move with it, not the d-pad.
 *
 * ⛔ THE BATTERY SAVE ROUND-TRIPS THROUGH ITS FILE. The save path handed
 * to the host is the cartridge's battery file (N64RomResolver keeps it in
 * internal storage): restored at load, written back on stop and on a state
 * save when the game changed it.
 */
object N64GameSession {

    private const val TAG = "N64Session"

    /** The rate the wrapper hands audio out at (n64_core.h: 32768 Hz). */
    private const val AUDIO_RATE = 32768

    /** NTSC N64 frame interval: 1 s / 60 Hz (the VI rate the core paces to). */
    private const val FRAME_NS = 1_000_000_000L / 60L

    /** The pause between frames when the phone cannot keep up (2 ms). */
    private const val BEHIND_GAP_NS = 2_000_000L

    /** How long one tap of an on-screen stick button pushes the stick, in frames. */
    private const val STICK_TAP_FRAMES = 15

    @Volatile
    private var appContext: Context? = null

    /**
     * True once a cartridge is loaded and the core is running.
     * Compose-observable, like the other sessions' flags, and written only
     * on the main thread.
     */
    private var running by androidx.compose.runtime.mutableStateOf(false)

    /** The worker's own view of [running]: plain, volatile, any thread. */
    @Volatile
    private var frameLoopActive = false

    @Volatile
    private var lastError: String = ""

    /** Bumped by every start and every stop; a stale load must not land. */
    @Volatile
    private var generation = 0

    /** Serialises the native half of a load against [stop]'s teardown. */
    private val loadLock = Any()

    /** Held around every frame, and every whole-core call between frames. */
    private val frameLock = Any()

    @Volatile
    private var coreGeneration = -1

    private const val LOAD_SUPERSEDED = "\u0001load-superseded"

    /** "n64:" and the cartridge ID ("n64:NPOE"), once a load lands. */
    @Volatile
    private var gameCode: String = ""

    @Volatile
    private var selectedAdapterId: String = ""

    @Volatile
    private var selectedAdapterName: String = ""

    @Volatile
    private var audioEnabledApplied: Boolean? = null

    fun romExtensions(): Set<String> = N64AccessibilityScript.ROM_EXTENSIONS

    fun handlesExtension(extension: String?): Boolean =
        N64AccessibilityScript.handlesExtension(extension)

    /**
     * True when this build can actually run Nintendo 64 games.
     *
     * ⛔ ASK THE CORE, NOT THE PACKAGE — the PlayStation probe's reasoning:
     * the only honest witness is the JNI probe that exists exactly when the
     * N64 surface was compiled. On armeabi-v7a the call throws
     * UnsatisfiedLinkError; that throw is the "no", caught here.
     */
    fun coreAvailable(context: Context): Boolean {
        return try {
            N64AccessibilityScript.isCoreLinked()
        } catch (e: UnsatisfiedLinkError) {
            Log.w(TAG, "Nintendo 64 path unavailable: ${e.message}")
            false
        } catch (t: Throwable) {
            Log.w(TAG, "Nintendo 64 path unavailable: ${t.message}")
            false
        }
    }

    /**
     * Point announcements at the live-region view: the same two-step as
     * the other sessions (stored now, re-forwarded once [start] has built
     * the speech bridge).
     */
    fun attachAnnouncementView(view: View) {
        storedAnnouncementView = view
        AccessibilityScript.setAnnouncementView(view)
    }

    @Volatile
    private var storedAnnouncementView: View? = null

    /**
     * Load a Nintendo 64 cartridge and start the core. [onResult] is called
     * on the main thread with null on success, or a player-facing message
     * on failure. Asynchronous, like the other sessions' starts: the ROM
     * may need copying out of the document provider first.
     */
    fun start(activity: Activity, uri: Uri, onResult: (String?) -> Unit) {
        val gen = ++generation
        val app = activity.applicationContext
        appContext = app
        val main = Handler(Looper.getMainLooper())
        val worker = Thread {
            val error = loadOnWorker(app, uri, gen)
            main.post {
                if (gen != generation) {
                    if (error == null && coreGeneration == gen) {
                        teardownNative()
                        coreGeneration = -1
                    }
                    return@post
                }
                if (error != null) {
                    lastError = error
                    onResult(error)
                    return@post
                }
                running = true
                lastError = ""
                val bridge = AccessibilityScript.speechBridge(app)
                try {
                    N64AccessibilityScript.setSpeechBridge(bridge)
                } catch (t: Throwable) {
                    Log.w(TAG, "Nintendo 64 speech bridge registration threw: ${t.message}")
                }
                selectedAdapterId = try {
                    N64AccessibilityScript.adapterId()
                } catch (t: Throwable) {
                    ""
                }
                selectedAdapterName = try {
                    N64AccessibilityScript.adapterName()
                } catch (t: Throwable) {
                    ""
                }
                storedAnnouncementView?.let { AccessibilityScript.setAnnouncementView(it) }
                startAudio()
                startFrameLoop(gen)
                Log.i(TAG, "Nintendo 64 session running: $gameCode (adapter $selectedAdapterId)")
                onResult(null)
            }
        }
        worker.name = "n64-load"
        worker.start()
    }

    /** The worker-thread half of [start]: resolve, load, start. */
    private fun loadOnWorker(context: Context, uri: Uri, gen: Int): String? {
        val romPath = N64RomResolver.resolve(context, uri)
            ?: return "Could not read that game file."
        val savePath = N64RomResolver.savePathFor(context, romPath)
            ?: return "The save file for that game could not be prepared."
        // Under the load lock, re-checking the generation first: the
        // PlayStation session's rule, for its reason (one native core).
        synchronized(loadLock) {
            if (gen != generation) return LOAD_SUPERSEDED
            val loaded = try {
                N64AccessibilityScript.loadRom(romPath, savePath)
            } catch (t: Throwable) {
                Log.e(TAG, "Nintendo 64 load threw", t)
                return "The Nintendo 64 core is not in this build."
            }
            if (!loaded) {
                val why = N64AccessibilityScript.lastError()
                return if (why.isNotEmpty()) {
                    "That Nintendo 64 game could not be loaded. $why"
                } else {
                    "That Nintendo 64 game could not be loaded."
                }
            }
            gameCode = N64AccessibilityScript.gameCode()
            if (!N64AccessibilityScript.startCore()) {
                val why = N64AccessibilityScript.lastError()
                teardownNative()
                return if (why.isNotEmpty()) {
                    "That Nintendo 64 game could not be started. $why"
                } else {
                    "That Nintendo 64 game could not be started."
                }
            }
            coreGeneration = gen
            return null
        }
    }

    /**
     * Stop the session: the frame thread first (joined, so no frame is
     * mid-flight), then audio, then the core — whose stop writes the
     * battery save back to its file. Safe with nothing running, including
     * on builds with no N64 core. MainActivity.onDestroy calls this
     * unconditionally, next to the other sessions' stops.
     */
    fun stop() {
        generation++
        running = false
        stopFrameLoop()
        heldButtons.clear()
        stickTapFramesLeft = 0
        selectedAdapterId = ""
        selectedAdapterName = ""
        audioEnabledApplied = null
        stopAudio()
        synchronized(loadLock) {
            teardownNative()
            coreGeneration = -1
        }
    }

    private fun teardownNative() {
        try {
            N64AccessibilityScript.setSpeechBridge(null)
        } catch (t: Throwable) {
            // No N64 surface in this build; nothing to clear.
        }
        try {
            N64AccessibilityScript.stopCore()
        } catch (t: Throwable) {
            Log.w(TAG, "Nintendo 64 stop threw: ${t.message}")
        }
    }

    // ---- the frame thread -----------------------------------------------------

    private var frameThread: Thread? = null

    private fun startFrameLoop(gen: Int) {
        stopFrameLoop()
        frameLoopActive = true
        val thread = Thread { frameLoop(gen) }
        thread.name = "n64-frames"
        frameThread = thread
        thread.start()
    }

    private fun stopFrameLoop() {
        frameLoopActive = false
        val thread = frameThread
        frameThread = null
        if (thread != null && thread !== Thread.currentThread()) {
            try {
                // A software N64 frame is far shorter than this; the join
                // only waits out the one frame in flight.
                thread.join(2000)
            } catch (e: InterruptedException) {
                // The core is torn down under the load lock either way.
            }
        }
    }

    /**
     * Paced to the console: a frame is due every [FRAME_NS]. When the
     * phone falls behind, the schedule resets instead of racing to catch
     * up, so the game runs slow rather than in bursts.
     */
    private fun frameLoop(gen: Int) {
        var nextAt = SystemClock.elapsedRealtimeNanos()
        var lastHeartbeat = 0L
        while (frameLoopActive && gen == generation) {
            val now = SystemClock.elapsedRealtimeNanos()
            if (now < nextAt) {
                val waitMs = (nextAt - now) / 1_000_000L
                if (waitMs > 0) {
                    try {
                        Thread.sleep(waitMs)
                    } catch (e: InterruptedException) {
                        break
                    }
                }
                continue
            }
            val ok = synchronized(frameLock) {
                try {
                    N64AccessibilityScript.runFrame()
                } catch (t: Throwable) {
                    false
                }
            }
            if (!ok) {
                // The core stopped answering mid-session: end it the way
                // Quit would, on the main thread (stop() joins this one).
                val why = try {
                    N64AccessibilityScript.lastError()
                } catch (t: Throwable) {
                    ""
                }
                Log.e(TAG, "Nintendo 64 frame failed, ending the session: $why")
                frameLoopActive = false
                Handler(Looper.getMainLooper()).post {
                    if (gen == generation) stop()
                }
                return
            }
            afterFrame()
            nextAt += FRAME_NS
            val after = SystemClock.elapsedRealtimeNanos()
            // Behind (the usual case on the software path): do not try to
            // catch up, and leave the lock free for a moment between
            // frames, so a picture copy or a save state waiting on the
            // main thread gets in after at most one frame.
            if (nextAt <= after) nextAt = after + BEHIND_GAP_NS
            val completed = try {
                N64AccessibilityScript.framesCompleted
            } catch (t: Throwable) {
                0L
            }
            if (completed >= lastHeartbeat + 600L) {
                lastHeartbeat = completed
                Log.i(TAG, "Nintendo 64 running: frames=$completed audioFrames=$audioFramesWritten")
            }
        }
    }

    /** Per-frame bookkeeping, on the frame thread: sound switch, stick taps. */
    private fun afterFrame() {
        appContext?.let { ctx ->
            val on = GameSoundSettings.gameSoundEnabled(ctx)
            if (audioEnabledApplied != on) {
                audioEnabledApplied = on
                try {
                    N64AccessibilityScript.setAudioEnabled(on)
                } catch (t: Throwable) {
                    // No surface in this build; nothing to gate.
                }
            }
        }
        if (stickTapFramesLeft > 0) {
            stickTapFramesLeft--
            if (stickTapFramesLeft == 0) applyStick(0f, 0f)
        }
    }

    // ---- the emulated pad -----------------------------------------------------

    private val heldButtons = java.util.Collections.synchronizedSet(mutableSetOf<Int>())

    fun pressButton(button: Int) {
        if (!running) return
        heldButtons.add(button)
        N64AccessibilityScript.setButton(button, true)
    }

    fun releaseButton(button: Int) {
        heldButtons.remove(button)
        if (running) N64AccessibilityScript.setButton(button, false)
    }

    /**
     * Tap a pad button for the screen-reader click path: the core itself
     * holds the press for two emulated frames (poke_tap_button), so the
     * game samples it however slowly the frames are running.
     */
    fun tapButton(button: Int) {
        if (!running) return
        try {
            N64AccessibilityScript.tapButton(button, 2)
        } catch (t: Throwable) {
            Log.w(TAG, "Nintendo 64 tap failed: ${t.message}")
        }
    }

    // ---- the analog stick -----------------------------------------------------

    /** Frames left on an on-screen stick tap; counted down by the frame thread. */
    @Volatile
    private var stickTapFramesLeft = 0

    private fun applyStick(x: Float, y: Float) {
        try {
            N64AccessibilityScript.setStick(x.coerceIn(-1f, 1f), y.coerceIn(-1f, 1f))
        } catch (t: Throwable) {
            // No surface in this build.
        }
    }

    /**
     * Set the stick directly, each axis -1..1, +y down (the controller's
     * left stick, already dead-zoned by the caller, and the on-screen
     * stick buttons while held). Cancels a pending tap.
     */
    fun setStick(x: Float, y: Float) {
        if (!running) return
        stickTapFramesLeft = 0
        applyStick(x, y)
    }

    /**
     * Push the stick fully one way for a quarter of a second of game time
     * ([STICK_TAP_FRAMES] emulated frames) and let it go: the screen-reader
     * click on an on-screen stick button. Counted in frames, not
     * milliseconds, so a slow phone still gets the whole push.
     */
    fun tapStick(x: Float, y: Float) {
        if (!running) return
        applyStick(x, y)
        stickTapFramesLeft = STICK_TAP_FRAMES
    }

    // ---- savestates -----------------------------------------------------------
    //
    // Whole-machine states, one slot per game, named by the cartridge ID,
    // under filesDir. Taken between frames, under the frame lock.

    private fun stateFile(context: Context): File {
        val dir = File(context.filesDir, "n64-states")
        if (!dir.isDirectory) dir.mkdirs()
        val name = if (gameCode.isNotEmpty()) {
            gameCode.replace(Regex("[^A-Za-z0-9_-]"), "_")
        } else {
            "n64"
        }
        return File(dir, "$name.state")
    }

    fun saveState(context: Context): Boolean {
        if (!running) return false
        val ok = synchronized(frameLock) {
            try {
                N64AccessibilityScript.saveState(stateFile(context).absolutePath)
            } catch (t: Throwable) {
                false
            }
        }
        if (!ok) Log.w(TAG, "Nintendo 64 save state failed")
        return ok
    }

    fun loadState(context: Context): Boolean {
        if (!running) return false
        val file = stateFile(context)
        if (!file.isFile) return false
        return synchronized(frameLock) {
            try {
                N64AccessibilityScript.loadState(file.absolutePath)
            } catch (t: Throwable) {
                false
            }
        }
    }

    fun hasSavedState(context: Context): Boolean = stateFile(context).isFile

    // ---- the picture ----------------------------------------------------------

    /**
     * The current frame for the game picture, or null. Copied between
     * frames, under the frame lock: the wrapper rebuilds its frame buffer
     * as a frame runs, so a copy taken mid-frame could read freed memory.
     */
    fun currentFrame(): Triple<IntArray, Int, Int>? {
        if (!running) return null
        return synchronized(frameLock) {
            try {
                N64AccessibilityScript.currentFrame()
            } catch (t: Throwable) {
                null
            }
        }
    }

    // ---- speech ---------------------------------------------------------------

    /**
     * Stop speech: the platform voice, then pokecore's StopSpeech
     * command (37), which clears the core's announcement queue so a
     * queued line can't start again a moment later, as iOS and every
     * other Android session do.
     *
     * ⛔ STOP SPEECH NEVER STOPS THE GAME: no [stop], nothing that tears
     * the core down. scripts/android-stop-speech-test.py holds every
     * session to this.
     */
    fun stopSpeech(context: Context) {
        try {
            AccessibilityScript.speechBridge(context.applicationContext).stop()
        } catch (t: Throwable) {
            // No bridge to stop; the command below is a no-op then too.
        }
        try {
            N64AccessibilityScript.adapterCommand(N64AdapterCommands.STOP_SPEECH)
        } catch (t: Throwable) {
            // No Nintendo 64 surface in this build; nothing queued to stop.
        }
    }

    fun repeatLastSpeech(): Boolean = AccessibilityScript.repeatLastSpeech()

    fun repeatOlderSpeech(): Boolean = AccessibilityScript.repeatOlderSpeech()

    /** The selected adapter's registry id ("n64"), or "" before a load lands. */
    val adapterId: String get() = selectedAdapterId

    /** The selected adapter's display name ("Nintendo 64"), or "" before a load lands. */
    val adapterName: String get() = selectedAdapterName

    // ---- audio ----------------------------------------------------------------

    @Volatile
    private var audioRunning = false

    private var audioThread: Thread? = null
    private var audioTrack: AudioTrack? = null

    @Volatile
    private var audioFramesWritten = 0L

    private fun startAudio() {
        val minBuffer = AudioTrack.getMinBufferSize(
            AUDIO_RATE,
            AudioFormat.CHANNEL_OUT_STEREO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        if (minBuffer <= 0) {
            Log.w(TAG, "Nintendo 64 audio unavailable: min buffer size $minBuffer")
            return
        }
        val track = try {
            AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(AUDIO_RATE)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                        .build()
                )
                .setBufferSizeInBytes(maxOf(minBuffer, AUDIO_RATE * 2))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
        } catch (t: Throwable) {
            Log.w(TAG, "Nintendo 64 audio unavailable: ${t.message}")
            return
        }
        audioTrack = track
        audioFramesWritten = 0L
        audioRunning = true
        track.play()
        val thread = Thread { audioLoop(track) }
        thread.name = "n64-audio"
        audioThread = thread
        thread.start()
        Log.i(TAG, "Nintendo 64 audio started: AudioTrack at $AUDIO_RATE Hz, stereo")
    }

    private fun audioLoop(track: AudioTrack) {
        val buffer = ShortArray(2048 * 2)
        while (audioRunning) {
            val frames = try {
                N64AccessibilityScript.readAudio(buffer, 2048)
            } catch (t: Throwable) {
                0
            }
            if (frames > 0) {
                track.write(buffer, 0, frames * 2, AudioTrack.WRITE_BLOCKING)
                audioFramesWritten += frames
            } else {
                try {
                    Thread.sleep(4)
                } catch (e: InterruptedException) {
                    break
                }
            }
        }
    }

    private fun stopAudio() {
        audioRunning = false
        val thread = audioThread
        audioThread = null
        if (thread != null) {
            try {
                thread.join(500)
            } catch (e: InterruptedException) {
                // The track is released below either way.
            }
        }
        val track = audioTrack
        audioTrack = null
        if (track != null) {
            try {
                track.stop()
            } catch (t: Throwable) {
                // Already stopped.
            }
            try {
                track.release()
            } catch (t: Throwable) {
                // Nothing useful to do with a failed release.
            }
        }
    }

    /** True once a Nintendo 64 game is running, so the pad and states can appear. */
    val isRunning: Boolean get() = running

    /** The loaded game's cartridge code, or "" before a load lands. */
    val loadedGameCode: String get() = gameCode
}

/**
 * The Nintendo 64 button ids: the shared pad's POKE_BTN_* slots
 * (pokecore.h), named for the N64 pad the way the core's own translation
 * table (Core/oga_core.cpp's kN64Button) lands them: A on A, B on B,
 * C-down on Select, Start, the d-pad, R on R, Z on L, C-left on X and
 * C-right on Y. The launcher contract test checks this object against
 * pokecore.h AND against that table.
 */
object N64Keys {
    const val A = 0
    const val B = 1
    const val C_DOWN = 2
    const val START = 3
    const val RIGHT = 4
    const val LEFT = 5
    const val UP = 6
    const val DOWN = 7
    const val R = 8
    const val Z = 9
    const val C_LEFT = 10
    const val C_RIGHT = 11
}

/**
 * The one pokecore command this session sends itself, mirrored from
 * Core/adapter.h's Command enum (scripts/android-stop-speech-test.py
 * checks the value against the enum).
 */
object N64AdapterCommands {
    const val STOP_SPEECH = 37
}
