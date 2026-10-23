package com.devin.opengameaccess

import android.app.Activity
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.View
import androidx.compose.runtime.Composable
import me.magnum.melonds.accessibility.AccessibilityScript
import me.magnum.melonds.accessibility.PspAccessibilityScript
import me.magnum.melonds.accessibility.PspRomResolver
import java.io.File

/**
 * The PlayStation Portable session, driven directly from Compose.
 *
 * ⛔ A SIBLING OF [GbGameSession], NOT A VARIANT. Same shape — an object
 * the screen calls, a running flag, a last error, held input, a Compose
 * frame clock — over a different core: PPSSPP through the psp_* ABI
 * (Core/psp_core.h), where GB drives mGBA through its Lua host. The GB
 * session's behavior is untouched by this file's existence; the launcher
 * keeps the two apart by the picked file's extension.
 *
 * ⛔ THE READER IS THE NATIVE HOST'S, DRIVEN FROM HERE (stage 4). The
 * PSP game adapters (Dissidia, Birth by Sleep, Mortal Kombat:
 * Unchained, DBZ: Another Road) attach in PspAdapterHost.cpp, which
 * selects by DISC_ID at load and owns the oga::Host; this session's
 * part is the lifecycle around it: the shared speech bridge is
 * registered with the host when a session lands, readiness is polled
 * once a frame in [runFrame], pad presses are forwarded as adapter
 * echoes (see [pressButton]), and the reader buttons on the launcher
 * screen call [sendAdapterCommand]. A game with no adapter simply
 * never answers ready and shows no reader buttons. The LAUNCHER's own
 * lines during a PSP session ("Game started.", "State saved.") are a
 * different matter: they ride the shared speech bridge, built and
 * view-wired at start exactly as the GB session does (see
 * [attachAnnouncementView]), or they would be typed into a status
 * field nobody hears.
 *
 * ⛔ START IS ASYNCHRONOUS, AND GB'S IS NOT, BECAUSE DISC IMAGES ARE NOT
 * CARTRIDGES. GbGameSession.start resolves a few megabytes on the
 * caller's thread and returns. A PSP image is 0.5–2 GB: the resolver
 * may have to copy all of it (see PspRomResolver for the storage cost,
 * which is real and recorded), and psp_load_rom then parses it. Doing
 * that on the button's thread would freeze the launcher — and TalkBack
 * with it — for the whole copy, so the load runs on a worker thread and
 * the result arrives through the caller's callback on the main thread.
 * A generation counter keeps a stop (Quit, or the Activity being
 * destroyed) that lands mid-load from resurrecting a session nobody is
 * waiting for: the load still completes natively, then is torn down
 * instead of started.
 *
 * ⛔ AUDIO IS THE SESSION'S OWN THREAD. The core mixes at 32768 Hz into
 * a ring the frame loop fills (Core/psp_core.cpp resamples to the app's
 * rate, the contract psp_read_audio documents); this session drains the
 * ring into an AudioTrack from a thread that lives exactly as long as
 * the session — started when a load lands, stopped and joined in
 * [stop], before the core is destroyed, so the drain never reads a dead
 * core. Game Boy has no audio path on Android today (a standing TODO in
 * the GB session's world); PSP ships with one because the core already
 * mixes the game's sound and leaving it unplayed would make every PSP
 * game a silent movie. There is no Android audio setting to gate the
 * drain behind — SettingsScreen says so itself — so it runs whenever a
 * PSP game does.
 */
object PspGameSession {

    private const val TAG = "PspSession"

    /** The rate the core hands audio out at (Core/psp_core.h). */
    private const val AUDIO_RATE = 32768

    /** True once a disc image is loaded and the core is running. */
    @Volatile
    private var running = false

    /** The last failure, for the screen to speak instead of guessing. */
    @Volatile
    private var lastError: String = ""

    /** Bumped by every start and every stop; a stale load must not land. */
    @Volatile
    private var generation = 0

    /** The loaded game's DISC_ID (e.g. ULUS10437), once a load lands. */
    @Volatile
    private var gameCode: String = ""

    /** The adapter the native host selected for the loaded game, or "". */
    @Volatile
    private var selectedAdapterId: String = ""

    /** The selected adapter's display name, or "" when there is none. */
    @Volatile
    private var selectedAdapterName: String = ""

    /**
     * The adapter's latest readiness answer, refreshed once a frame by
     * [runFrame]. The command buttons and the pad echoes gate on this —
     * the same shape as iOS, where GameSession forwards pad echoes only
     * while its adapterReady flag stands.
     */
    @Volatile
    private var adapterReadyNow: Boolean = false

    fun romExtensions(): Set<String> = PspAccessibilityScript.ROM_EXTENSIONS

    fun handlesExtension(extension: String?): Boolean =
        PspAccessibilityScript.handlesExtension(extension)

    /**
     * True when this build can actually run PSP games.
     *
     * ⛔ ASK THE CORE, NOT THE PACKAGE. The GB probe reads the APK's own
     * entries, which works there because the frontend .so's presence is
     * the whole question. Here the cores SHARE that .so, so its presence
     * says nothing about PSP — the question is whether the PSP surface
     * was compiled into it, and the only honest witness is the JNI probe
     * that exists exactly when it was (see PspAccessibilityScript). On
     * armeabi-v7a the call throws UnsatisfiedLinkError; that throw is
     * the "no", caught here. Still fail-closed, like the GB probe.
     */
    fun coreAvailable(context: Context): Boolean {
        return try {
            PspAccessibilityScript.isCoreLinked()
        } catch (e: UnsatisfiedLinkError) {
            // The armeabi-v7a answer: the surface is not in the .so.
            Log.w(TAG, "PSP path unavailable: ${e.message}")
            false
        } catch (t: Throwable) {
            Log.w(TAG, "PSP path unavailable: ${t.message}")
            false
        }
    }

    /**
     * Point announcements at the live-region view.
     *
     * ⛔ THE SAME TWO-STEP AS GbGameSession, FOR THE SAME REASON. The
     * attach that happens during composition forwards to a speech bridge
     * that does not exist yet (it is built when a game starts) and is
     * silently a no-op; without the re-forward in [start] the view stays
     * null for the whole session and the bridge speaks through
     * TextToSpeech on top of the screen reader — two voices on one
     * output. MainActivity calls this from the same SideEffect as the GB
     * session's attach; both store the same view and forward to the same
     * AccessibilityScript, so the duplication is two lines, not two
     * mechanisms.
     */
    fun attachAnnouncementView(view: View) {
        storedAnnouncementView = view
        AccessibilityScript.setAnnouncementView(view)
    }

    @Volatile
    private var storedAnnouncementView: View? = null

    /**
     * Load a PSP disc image and start the core. [onResult] is called on
     * the main thread with null on success, or a player-facing message
     * on failure. See the class doc for why this is not
     * GbGameSession.start's synchronous shape.
     */
    fun start(activity: Activity, uri: Uri, onResult: (String?) -> Unit) {
        val gen = ++generation
        val app = activity.applicationContext
        val main = Handler(Looper.getMainLooper())
        val worker = Thread {
            val error = loadOnWorker(app, uri)
            main.post {
                if (gen != generation) {
                    // Stopped (or superseded) while loading: the load's
                    // native core, if it got that far, is torn down
                    // rather than left running ownerless.
                    if (error == null) teardownNative()
                    return@post
                }
                if (error != null) {
                    lastError = error
                    onResult(error)
                    return@post
                }
                running = true
                lastError = ""
                // The bridge exists now (built on demand, as the GB
                // session builds it when its reader starts), so the
                // stored view can actually be attached — the re-forward
                // described in attachAnnouncementView. The same bridge
                // is registered with the PSP adapter host: adapter
                // speech lands on this one instance, like every other
                // system's speech in the app.
                val bridge = AccessibilityScript.speechBridge(app)
                try {
                    PspAccessibilityScript.setSpeechBridge(bridge)
                } catch (t: Throwable) {
                    Log.w(TAG, "PSP speech bridge registration threw: ${t.message}")
                }
                selectedAdapterId = try {
                    PspAccessibilityScript.adapterId()
                } catch (t: Throwable) {
                    ""
                }
                selectedAdapterName = try {
                    PspAccessibilityScript.adapterName()
                } catch (t: Throwable) {
                    ""
                }
                storedAnnouncementView?.let { AccessibilityScript.setAnnouncementView(it) }
                startAudio()
                Log.i(TAG, "PSP session running: $gameCode")
                onResult(null)
            }
        }
        worker.name = "psp-load"
        worker.start()
    }

    /** The worker-thread half of [start]: resolve, extract, load, start. */
    private fun loadOnWorker(context: Context, uri: Uri): String? {
        val romPath = PspRomResolver.resolve(context, uri)
            ?: return "Could not read that game file."
        val assetDir = PspAccessibilityScript.extractAssets(context)
            ?: return "The PSP core's files could not be prepared."
        // The memstick lives under filesDir, NOT beside the resolver's
        // cache copy: savedata must survive cache eviction.
        val memstick = File(context.filesDir, "ppsspp-memstick")
        if (!memstick.isDirectory && !memstick.mkdirs()) {
            return "The PSP save folder could not be created."
        }
        val loaded = try {
            PspAccessibilityScript.loadRom(romPath, memstick.absolutePath, assetDir)
        } catch (t: Throwable) {
            Log.e(TAG, "PSP load threw", t)
            return "The PSP core is not in this build."
        }
        if (!loaded) {
            val why = PspAccessibilityScript.lastError()
            return if (why.isNotEmpty()) {
                "That PSP game could not be loaded. $why"
            } else {
                "That PSP game could not be loaded."
            }
        }
        gameCode = PspAccessibilityScript.gameCode()
        if (!PspAccessibilityScript.startCore()) {
            val why = PspAccessibilityScript.lastError()
            teardownNative()
            return if (why.isNotEmpty()) {
                "That PSP game could not be started. $why"
            } else {
                "That PSP game could not be started."
            }
        }
        return null
    }

    /**
     * Stop the session: audio first (joined, so nothing is still
     * draining), then the core. Safe with nothing running — including on
     * builds with no PSP core, where the native stop's absence is
     * swallowed by [teardownNative]. MainActivity.onDestroy calls this
     * unconditionally, next to GbGameSession.stop().
     */
    fun stop() {
        generation++
        running = false
        heldButtons.clear()
        selectedAdapterId = ""
        selectedAdapterName = ""
        adapterReadyNow = false
        stopAudio()
        teardownNative()
    }

    private fun teardownNative() {
        try {
            PspAccessibilityScript.setSpeechBridge(null)
        } catch (t: Throwable) {
            // No PSP surface in this build; nothing to clear.
        }
        try {
            PspAccessibilityScript.stopCore()
        } catch (t: Throwable) {
            Log.w(TAG, "PSP stop threw: ${t.message}")
        }
    }

    /** One emulated frame. Driven from the Compose frame clock, see [rememberPspFrameClock]. */
    fun runFrame() {
        if (!running) return
        val ok = try {
            PspAccessibilityScript.runFrame()
        } catch (t: Throwable) {
            false
        }
        if (!ok) {
            // The core stopped answering mid-session: end it the way
            // Quit would, rather than leaving a "running" screen over a
            // dead core.
            val why = try {
                PspAccessibilityScript.lastError()
            } catch (t: Throwable) {
                ""
            }
            Log.e(TAG, "PSP frame failed, ending the session: $why")
            stop()
            return
        }
        // The adapter's readiness is polled, not pushed: the native
        // host attaches lazily inside this call (its memory anchors
        // only exist once the game has booted), and the launcher's
        // reader buttons and pad echoes key off the answer.
        adapterReadyNow = try {
            PspAccessibilityScript.adapterReady()
        } catch (t: Throwable) {
            false
        }
        val completed = try {
            PspAccessibilityScript.framesCompleted
        } catch (t: Throwable) {
            0L
        }
        if (completed >= lastHeartbeatFrame + 600L) {
            lastHeartbeatFrame = completed
            Log.i(TAG, "PSP running: frames=$completed audioFrames=$audioFramesWritten")
        }
    }

    private var lastHeartbeatFrame = 0L

    // ---- the emulated pad ---------------------------------------------------
    //
    // ⛔ PER-BUTTON, NOT A MASK. mGBA takes the whole pad state in one call
    // (GbGameSession pushes a mask); the psp_* ABI latches one button per
    // call (psp_set_button). So press and release forward as they happen,
    // and the held set is remembered only so stop() leaves nothing stuck —
    // the core is destroyed there anyway, this is belt and braces.

    private val heldButtons = mutableSetOf<Int>()

    fun pressButton(button: Int) {
        if (!running) return
        heldButtons.add(button)
        PspAccessibilityScript.setButton(button, true)
        forwardPadEcho(button)
    }

    fun releaseButton(button: Int) {
        heldButtons.remove(button)
        if (running) PspAccessibilityScript.setButton(button, false)
    }

    /** Release a held button a moment later, for the screen-reader click path. */
    fun releaseButtonSoon(button: Int) {
        Handler(Looper.getMainLooper()).postDelayed(
            { releaseButton(button) },
            120L,
        )
    }

    // ---- savestates ---------------------------------------------------------
    //
    // Whole-core PPSSPP states (psp_save_state's contract, the same one iOS
    // exposes): one slot per game, named by DISC_ID, under filesDir —
    // states must survive cache eviction, like the memstick does.

    private fun stateFile(context: Context): File {
        val dir = File(context.filesDir, "ppsspp-states")
        if (!dir.isDirectory) dir.mkdirs()
        val name = if (gameCode.isNotEmpty()) gameCode else "psp"
        return File(dir, "$name.state")
    }

    fun saveState(context: Context): Boolean {
        if (!running) return false
        val ok = try {
            PspAccessibilityScript.saveState(stateFile(context).absolutePath)
        } catch (t: Throwable) {
            false
        }
        if (!ok) Log.w(TAG, "PSP save state failed")
        return ok
    }

    fun loadState(context: Context): Boolean {
        if (!running) return false
        val file = stateFile(context)
        if (!file.isFile) return false
        return try {
            PspAccessibilityScript.loadState(file.absolutePath)
        } catch (t: Throwable) {
            false
        }
    }

    // ---- the reader (stage 4) ----------------------------------------------
    //
    // The command surface the launcher's PSP screen drives. It mirrors
    // iOS's GameSession split: sendAdapterCommand returns the native
    // verdict and the SCREEN speaks a refusal (the two lines are iOS's),
    // while the pad echoes below are forwarded silently — exactly like
    // iOS's forwardPadCommand, a swallowed refusal makes directional
    // presses repeat "not ready" forever, which is worse than silence.

    /** The selected adapter's registry id, or "" when the game has no reader. */
    val adapterId: String get() = selectedAdapterId

    /** The selected adapter's display name, or "" when the game has no reader. */
    val adapterName: String get() = selectedAdapterName

    /** The adapter's latest readiness answer (see [runFrame]'s poll). */
    val isAdapterReady: Boolean get() = adapterReadyNow

    /**
     * Send one reader command (a [PspAdapterCommands] value). True when
     * the adapter took it; false is the native refusal (no adapter, out
     * of range, not ready), and speaking it is the caller's job.
     */
    fun sendAdapterCommand(command: Int): Boolean {
        if (!running) return false
        return try {
            PspAccessibilityScript.adapterCommand(command)
        } catch (t: Throwable) {
            false
        }
    }

    /**
     * Stop speech: the platform voice AND the adapter's queue, the two
     * halves iOS's GameSession.stopSpeech performs (its platform stop
     * plus the StopSpeech command the core intercepts). PSP has a direct
     * speech stop — the bridge — so there is no script-side stop here.
     */
    fun stopSpeech(context: Context) {
        try {
            AccessibilityScript.speechBridge(context.applicationContext).stop()
        } catch (t: Throwable) {
            // No bridge to stop; the command below is a no-op then too.
        }
        try {
            PspAccessibilityScript.adapterCommand(PspAdapterCommands.STOP_SPEECH)
        } catch (t: Throwable) {
            // No PSP surface in this build; nothing queued to stop.
        }
    }

    /**
     * Forward one pad press to the adapter as its echo command — the
     * adapter.h echo contract, in this pad's own button names: face
     * buttons are the on-screen keyboard's keys while a text entry is
     * open (Cross types, Circle erases, Square spaces, Select shifts,
     * Start finishes), and the D-pad steps menus and value lists
     * whether or not the game is showing one (each adapter decides;
     * the echo is how DBZ: Another Road's target ring turns, for one).
     * Only while the adapter stands ready, and only on the press —
     * iOS's InputBridge.echoPadCommand is the same shape.
     */
    private fun forwardPadEcho(button: Int) {
        val command = when (button) {
            PspKeys.CROSS -> PspAdapterCommands.OSK_TYPE
            PspKeys.CIRCLE -> PspAdapterCommands.OSK_DELETE
            PspKeys.SQUARE -> PspAdapterCommands.OSK_SPACE
            PspKeys.SELECT -> PspAdapterCommands.OSK_SHIFT
            PspKeys.START -> PspAdapterCommands.OSK_FINISH
            PspKeys.UP -> PspAdapterCommands.MENU_PREV
            PspKeys.DOWN -> PspAdapterCommands.MENU_NEXT
            PspKeys.LEFT -> PspAdapterCommands.MENU_LEFT
            PspKeys.RIGHT -> PspAdapterCommands.MENU_RIGHT
            else -> return
        }
        if (!adapterReadyNow) return
        try {
            PspAccessibilityScript.adapterCommand(command)
        } catch (t: Throwable) {
            // The echo path stays silent (see the section note).
        }
    }

    // ---- audio --------------------------------------------------------------

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
            Log.w(TAG, "PSP audio unavailable: min buffer size $minBuffer")
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
                // ~0.5 s of stereo s16, or the platform minimum if larger.
                .setBufferSizeInBytes(maxOf(minBuffer, AUDIO_RATE * 2))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
        } catch (t: Throwable) {
            Log.w(TAG, "PSP audio unavailable: ${t.message}")
            return
        }
        audioTrack = track
        audioFramesWritten = 0L
        audioRunning = true
        track.play()
        val thread = Thread { audioLoop(track) }
        thread.name = "psp-audio"
        audioThread = thread
        thread.start()
        Log.i(TAG, "PSP audio started: AudioTrack at $AUDIO_RATE Hz, stereo")
    }

    private fun audioLoop(track: AudioTrack) {
        val buffer = ShortArray(2048 * 2)
        while (audioRunning) {
            val frames = try {
                PspAccessibilityScript.readAudio(buffer, 2048)
            } catch (t: Throwable) {
                0
            }
            if (frames > 0) {
                track.write(buffer, 0, frames * 2, AudioTrack.WRITE_BLOCKING)
                audioFramesWritten += frames
            } else {
                // Dry: the ring refills as frames run. Sleep briefly
                // rather than spin a processor on an empty queue.
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
                // Already stopped; releasing is what matters.
            }
            try {
                track.release()
            } catch (t: Throwable) {
                // Nothing useful to do with a failed release.
            }
        }
    }

    /** True once a PSP game is running, so the pad and commands can appear. */
    val isRunning: Boolean get() = running

    /** The loaded game's DISC_ID, or "" before a load lands. */
    val loadedGameCode: String get() = gameCode
}

/**
 * The PSP button ids, in the core's own order (Core/psp_core.h).
 *
 * ⛔ THESE ARE THE CORE'S VALUES, NOT A UI CONVENTION — the same warning
 * as GbKeys, for the same reason: psp_set_button indexes the core's own
 * table, so a wrong id is a wrong button with no error anywhere. The
 * launcher contract test checks this object against psp_core.h's
 * PSP_BTN_* defines.
 */
object PspKeys {
    const val SELECT = 0
    const val START = 1
    const val UP = 2
    const val RIGHT = 3
    const val DOWN = 4
    const val LEFT = 5
    const val TRIANGLE = 6
    const val CIRCLE = 7
    const val CROSS = 8
    const val SQUARE = 9
    const val L = 10
    const val R = 11
}

/**
 * The oga::Command values the PSP screen and pad can send, as raw ints.
 *
 * ⛔ THESE ARE THE C ABI'S VALUES, NOT A UI CONVENTION — adapter.h's
 * Command enum is append-only because the integer IS the cross-language
 * contract ("Swift mirrors these integer values exactly" is its own
 * header's rule, and Android now does too). Only the commands in some
 * PSP adapter's iOS-supported set, the pad echoes, and the core's own
 * StopSpeech are mirrored here; the launcher contract test checks every
 * value below against Core/adapter.h itself.
 */
object PspAdapterCommands {
    const val WHERE_AM_I = 0
    const val NEXT_ALLY = 1
    const val PREV_ALLY = 2
    const val NEXT_ENEMY = 3
    const val PREV_ENEMY = 4
    const val NEXT_UNACTED_ALLY = 5
    const val DUMP_STATE = 6
    const val MENU_STATE = 7
    const val MENU_NEXT = 8
    const val MENU_PREV = 9
    const val MENU_LEFT = 10
    const val MENU_RIGHT = 11
    const val CUST_TOGGLE = 12
    const val CHAR_TOGGLE = 13
    const val OSK_TOGGLE = 31
    const val OSK_TYPE = 32
    const val OSK_DELETE = 33
    const val OSK_SPACE = 34
    const val OSK_SHIFT = 35
    const val OSK_FINISH = 36
    const val STOP_SPEECH = 37
    const val STATUS = 40
    const val GUIDE = 41
}

/**
 * Drives one emulated PSP frame per Compose frame while a PSP game runs.
 *
 * The twin of GbGameSession.kt's rememberFrameClock, kept beside its own
 * session for the same reason that one is beside its: the cadence is the
 * app's frame signal, so the core's per-frame work happens on the same
 * thread that produced it.
 */
@Composable
fun rememberPspFrameClock(running: Boolean) {
    if (!running) return
    androidx.compose.runtime.LaunchedEffect(Unit) {
        while (true) {
            androidx.compose.runtime.withFrameNanos { }
            PspGameSession.runFrame()
        }
    }
}
