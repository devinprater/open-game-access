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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import me.magnum.melonds.accessibility.AccessibilityScript
import me.magnum.melonds.accessibility.GenesisAccessibilityScript
import me.magnum.melonds.accessibility.GenesisRomResolver
import java.io.File

/**
 * The Sega Genesis session, driven directly from Compose.
 *
 * ⛔ A SIBLING OF [DsGameSession], NOT A VARIANT. Same shape — an
 * object the screen calls, a Compose-observable running flag, a last
 * error, held input, a Compose frame clock, an audio drain thread, an
 * asynchronous start — over the shared Core host (Core/pokecore.cpp)
 * and its Genesis Plus GX backend (Core/genesis_core.cpp), where DS
 * drives the same host's melonDS backend. The other sessions'
 * behavior is untouched by this file's existence; the launcher keeps
 * them apart by the picked file's extension.
 *
 * ⛔ NO READER, SAID PLAINLY. Genesis has no native adapter and no
 * reader script: the registry's Genesis row refuses to attach, and
 * the backend's ops table names no reader set. So this session is
 * pad, save states, battery saves, audio and the game picture — a
 * Genesis game in this build is playable, but not accessible: nothing
 * in it speaks, and no reader buttons exist to draw. The adapter
 * forwards on the native surface exist so the session can ask and be
 * told that truth (the id answers "genesis"; ready never answers
 * true), not because a reader is hiding behind them. Where Am I and
 * the controller layer's reader chords answer with the honest line
 * the other reader-less cases use ("No reader command there for this
 * game.").
 *
 * ⛔ THE PAD CARRIES BOTH CONTROLLERS. The core auto-selects the
 * 3-button or 6-button pad per game (the cartridge's own peripheral
 * flags), so the session exposes the full six-button set all the
 * time — A/B/C plus X/Y/Z and Mode — on the shared pad slots the
 * registry prescribes (see [GenesisKeys]): a 3-button game simply
 * never reads the extra buttons. The names are the iOS side's.
 *
 * ⛔ THE BATTERY SAVE ROUND-TRIPS THROUGH THE .sav. The save path
 * handed to the host IS the cartridge's battery file
 * (GenesisRomResolver places it where cache eviction cannot take
 * it): the wrapper restores it into SRAM at load and flushes it back
 * on stop — which is where [stop] and the quit flow land — when the
 * bytes changed, and a state save flushes first so the two never
 * disagree about which is newer. The carve-out, stated in
 * genesis_core.h as well: EEPROM/flash-save cartridges keep their
 * saves inside the chip emulation, not in SRAM, so they persist
 * through save states only in this version.
 *
 * ⛔ START IS ASYNCHRONOUS, LIKE THE OTHER SESSIONS'. The resolver
 * may have to copy the ROM before the load can parse it; doing that
 * on the button's thread would freeze the launcher — and TalkBack
 * with it — so the load runs on a worker thread and the result
 * arrives through the caller's callback on the main thread. The
 * generation counter is the DS/PSP sessions': a stop that lands
 * mid-load tears the landed core down instead of resurrecting a
 * session nobody is waiting for.
 *
 * ⛔ AUDIO IS THE SESSION'S OWN THREAD, THE DS SHAPE. The wrapper
 * mixes at 32768 Hz; this session drains it into an AudioTrack from
 * a thread that lives exactly as long as the session. Unlike the DS
 * drain there is NO cue mix here — no Genesis reader exists to arm
 * the cue synth, so the drain carries the game alone — and the
 * Settings game-sound switch IS wired, the PSP way: resolved per
 * frame in [runFrame] and applied through the host's own audio gate
 * (poke_set_audio_enabled), so flipping it mid-session lands on the
 * next frame.
 */
object GenesisGameSession {

    private const val TAG = "GenesisSession"

    /** The rate the core hands audio out at (genesis_core.h's contract). */
    private const val AUDIO_RATE = 32768

    /**
     * The application context, kept from [start] so [runFrame] can
     * read the game-sound switch.
     */
    @Volatile
    private var appContext: Context? = null

    /**
     * True once a cartridge is loaded and the core is running.
     *
     * ⛔ COMPOSE-OBSERVABLE, LIKE THE DS FLAG, FOR THE DS REASON: the
     * launcher reads [isRunning] directly when it decides whether
     * the Genesis controls exist, so a session that ends ITSELF (a
     * frame failure in [runFrame] calls [stop]) takes its controls
     * down with it.
     */
    private var running by androidx.compose.runtime.mutableStateOf(false)

    /** The last failure, for the screen to speak instead of guessing. */
    @Volatile
    private var lastError: String = ""

    /** Bumped by every start and every stop; a stale load must not land. */
    @Volatile
    private var generation = 0

    /**
     * Serialises the native half of a load (load ROM, start core)
     * against [stop]'s teardown, so the two can never interleave
     * calls into the one native core.
     */
    private val loadLock = Any()

    /**
     * The generation whose load built the live core, or -1 when no
     * load owns one. A stale load's completion tears the core down
     * ONLY when the core is still its own generation's.
     */
    @Volatile
    private var coreGeneration = -1

    /** [loadOnWorker]'s answer when a newer start/stop superseded it. */
    private const val LOAD_SUPERSEDED = "\u0001load-superseded"

    /**
     * The loaded game's identity: "crc:<hex>" of the cartridge image
     * the core runs (Genesis ROMs carry no 4-character code — the
     * wrapper's content identity, the SNES rule), once a load lands.
     */
    @Volatile
    private var gameCode: String = ""

    /** The adapter the host selected for the loaded game ("genesis"), or "". */
    @Volatile
    private var selectedAdapterId: String = ""

    /** The selected adapter's display name ("Sega Genesis"), or "" when none. */
    @Volatile
    private var selectedAdapterName: String = ""

    /**
     * The game-sound gate's last applied value, so [runFrame] calls
     * the native setter only when the switch actually moved.
     */
    @Volatile
    private var audioEnabledApplied: Boolean? = null

    fun romExtensions(): Set<String> = GenesisAccessibilityScript.ROM_EXTENSIONS

    fun handlesExtension(extension: String?): Boolean =
        GenesisAccessibilityScript.handlesExtension(extension)

    /**
     * True when this build can actually run Genesis games.
     *
     * ⛔ ASK THE CORE, NOT THE PACKAGE — the DS probe's reasoning: the
     * cores SHARE the frontend .so, so its presence says nothing
     * about Genesis; the question is whether the Genesis surface was
     * compiled into it, and the only honest witness is the JNI probe
     * that exists exactly when it was (see
     * GenesisAccessibilityScript). On armeabi-v7a the call throws
     * UnsatisfiedLinkError; that throw is the "no", caught here.
     */
    fun coreAvailable(context: Context): Boolean {
        return try {
            GenesisAccessibilityScript.isCoreLinked()
        } catch (e: UnsatisfiedLinkError) {
            // The armeabi-v7a answer: the surface is not in the .so.
            Log.w(TAG, "Genesis path unavailable: ${e.message}")
            false
        } catch (t: Throwable) {
            Log.w(TAG, "Genesis path unavailable: ${t.message}")
            false
        }
    }

    /**
     * Point announcements at the live-region view.
     *
     * ⛔ THE SAME TWO-STEP AS THE OTHER SESSIONS, FOR THE SAME
     * REASON. The attach that happens during composition forwards to
     * a speech bridge that does not exist yet (it is built when a
     * game starts) and is silently a no-op; without the re-forward in
     * [start] the view stays null for the whole session and the
     * bridge speaks through TextToSpeech on top of the screen
     * reader. MainActivity calls this from the same SideEffect as the
     * other sessions' attaches; all store the same view and forward
     * to the same AccessibilityScript.
     */
    fun attachAnnouncementView(view: View) {
        storedAnnouncementView = view
        AccessibilityScript.setAnnouncementView(view)
    }

    @Volatile
    private var storedAnnouncementView: View? = null

    /**
     * Load a Genesis cartridge image and start the core. [onResult]
     * is called on the main thread with null on success, or a
     * player-facing message on failure. See the class doc for why
     * this is asynchronous.
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
                    // Stopped (or superseded) while loading: tear
                    // down ONLY a core this load itself built and no
                    // newer load has claimed.
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
                // The bridge exists now, so the stored view can
                // actually be attached — the re-forward described in
                // attachAnnouncementView. The same bridge is
                // registered with the Genesis host, like every other
                // system's speech in the app.
                val bridge = AccessibilityScript.speechBridge(app)
                try {
                    GenesisAccessibilityScript.setSpeechBridge(bridge)
                } catch (t: Throwable) {
                    Log.w(TAG, "Genesis speech bridge registration threw: ${t.message}")
                }
                selectedAdapterId = try {
                    GenesisAccessibilityScript.adapterId()
                } catch (t: Throwable) {
                    ""
                }
                selectedAdapterName = try {
                    GenesisAccessibilityScript.adapterName()
                } catch (t: Throwable) {
                    ""
                }
                storedAnnouncementView?.let { AccessibilityScript.setAnnouncementView(it) }
                startAudio()
                Log.i(TAG, "Genesis session running: $gameCode (adapter $selectedAdapterId)")
                onResult(null)
            }
        }
        worker.name = "genesis-load"
        worker.start()
    }

    /** The worker-thread half of [start]: resolve, load, start. */
    private fun loadOnWorker(context: Context, uri: Uri, gen: Int): String? {
        val romPath = GenesisRomResolver.resolve(context, uri)
            ?: return "Could not read that game file."
        val savePath = GenesisRomResolver.savePathFor(context, romPath)
            ?: return "The save file for that game could not be prepared."
        // ⛔ THE NATIVE HALF RUNS UNDER THE LOAD LOCK, RE-CHECKING
        // THE GENERATION FIRST. The core is a singleton: two loads
        // racing it, or a load racing stop()'s teardown, would
        // interleave create/load/start/destroy calls on one core. The
        // lock serialises the native calls; the generation check
        // keeps a superseded load from touching the core at all (its
        // ROM copy above is its own file and needs no lock); and a
        // load that completes records its generation as the core's
        // owner, which is what the completion's teardown rule in
        // start() reads. (No script step: the Genesis backend has no
        // reader set to hand over — the DS load's middle step has no
        // counterpart here.)
        synchronized(loadLock) {
            if (gen != generation) return LOAD_SUPERSEDED
            val loaded = try {
                GenesisAccessibilityScript.loadRom(romPath, savePath)
            } catch (t: Throwable) {
                Log.e(TAG, "Genesis load threw", t)
                return "The Genesis core is not in this build."
            }
            if (!loaded) {
                val why = GenesisAccessibilityScript.lastError()
                return if (why.isNotEmpty()) {
                    "That Genesis game could not be loaded. $why"
                } else {
                    "That Genesis game could not be loaded."
                }
            }
            gameCode = GenesisAccessibilityScript.gameCode()
            if (!GenesisAccessibilityScript.startCore()) {
                val why = GenesisAccessibilityScript.lastError()
                teardownNative()
                return if (why.isNotEmpty()) {
                    "That Genesis game could not be started. $why"
                } else {
                    "That Genesis game could not be started."
                }
            }
            coreGeneration = gen
            return null
        }
    }

    /**
     * Stop the session: audio first (joined, so nothing is still
     * draining), then the core — whose stop flushes the battery save
     * back to its .sav (see the class doc). Safe with nothing
     * running — including on builds with no Genesis core, where the
     * native stop's absence is swallowed by [teardownNative].
     * MainActivity.onDestroy calls this unconditionally, next to the
     * other sessions' stops.
     */
    fun stop() {
        generation++
        running = false
        heldButtons.clear()
        pendingReleases.clear()
        selectedAdapterId = ""
        selectedAdapterName = ""
        audioEnabledApplied = null
        stopAudio()
        // Under the load lock: a load in its native half finishes (or
        // is superseded by the generation bump above) before the
        // teardown touches the core, never during it.
        synchronized(loadLock) {
            teardownNative()
            coreGeneration = -1
        }
    }

    private fun teardownNative() {
        try {
            GenesisAccessibilityScript.setSpeechBridge(null)
        } catch (t: Throwable) {
            // No Genesis surface in this build; nothing to clear.
        }
        try {
            GenesisAccessibilityScript.stopCore()
        } catch (t: Throwable) {
            Log.w(TAG, "Genesis stop threw: ${t.message}")
        }
    }

    /** One emulated frame. Driven from the Compose frame clock, see [rememberGenesisFrameClock]. */
    fun runFrame() {
        if (!running) return
        val ok = try {
            GenesisAccessibilityScript.runFrame()
        } catch (t: Throwable) {
            false
        }
        if (!ok) {
            // The core stopped answering mid-session: end it the way
            // Quit would, rather than leaving a "running" screen over
            // a dead core.
            val why = try {
                GenesisAccessibilityScript.lastError()
            } catch (t: Throwable) {
                ""
            }
            Log.e(TAG, "Genesis frame failed, ending the session: $why")
            stop()
            return
        }
        // The game-sound gate, resolved per frame like the PSP
        // session's (a flip in Settings lands on the next frame),
        // applied through the host's own gate only when it moved.
        appContext?.let { ctx ->
            val on = GameSoundSettings.gameSoundEnabled(ctx)
            if (audioEnabledApplied != on) {
                audioEnabledApplied = on
                try {
                    GenesisAccessibilityScript.setAudioEnabled(on)
                } catch (t: Throwable) {
                    // No surface in this build; nothing to gate.
                }
            }
        }
        // A frame has now sampled whatever is held: count the tap
        // releases against it (see the frame-counted releases note).
        tickPendingReleases()
        val completed = try {
            GenesisAccessibilityScript.framesCompleted
        } catch (t: Throwable) {
            0L
        }
        if (completed >= lastHeartbeatFrame + 600L) {
            lastHeartbeatFrame = completed
            Log.i(TAG, "Genesis running: frames=$completed audioFrames=$audioFramesWritten")
        }
    }

    private var lastHeartbeatFrame = 0L

    // ---- the emulated pad ---------------------------------------------------
    //
    // ⛔ PER-BUTTON, NOT A MASK — the PSP/DS shape, for their reason:
    // poke_set_button latches one button per call. Press and release
    // forward as they happen, and the held set is remembered only so
    // stop() leaves nothing stuck. No pad echoes are forwarded: the
    // echo contract belongs to readers, and Genesis has none.

    private val heldButtons = mutableSetOf<Int>()

    fun pressButton(button: Int) {
        if (!running) return
        heldButtons.add(button)
        GenesisAccessibilityScript.setButton(button, true)
    }

    fun releaseButton(button: Int) {
        heldButtons.remove(button)
        if (running) GenesisAccessibilityScript.setButton(button, false)
    }

    // ---- frame-counted tap releases ------------------------------------
    //
    // ⛔ RELEASES ARE COUNTED IN FRAMES, NOT MILLISECONDS — the DS
    // note, for the DS reason: a press exists for the game only if a
    // frame SAMPLES it, and a wall-clock release can fire before any
    // frame has run, losing the tap silently. Pad buttons have the
    // stronger native form (poke_tap_button, see [tapButton]); this
    // counting serves [releaseButtonSoon]. Touched only from the main
    // thread: taps originate in UI/controller paths and the counting
    // happens in [runFrame].
    private class PendingRelease(var framesLeft: Int, val fire: () -> Unit)

    private val pendingReleases = mutableListOf<PendingRelease>()

    private fun releaseAfterFrames(frames: Int, fire: () -> Unit) {
        pendingReleases.add(PendingRelease(frames, fire))
    }

    private fun tickPendingReleases() {
        if (pendingReleases.isEmpty()) return
        val due = mutableListOf<PendingRelease>()
        val iterator = pendingReleases.iterator()
        while (iterator.hasNext()) {
            val pending = iterator.next()
            if (--pending.framesLeft <= 0) {
                due.add(pending)
                iterator.remove()
            }
        }
        due.forEach { it.fire() }
    }

    /**
     * Tap a pad button for the screen-reader click path: the core
     * itself holds the press for two emulated frames
     * (poke_tap_button), so the game samples it whatever the host's
     * timing is doing — no timer anywhere in the path.
     */
    fun tapButton(button: Int) {
        if (!running) return
        try {
            GenesisAccessibilityScript.tapButton(button, 2)
        } catch (t: Throwable) {
            Log.w(TAG, "Genesis tap failed: ${t.message}")
        }
    }

    /** Release a held button once two frames have sampled it. */
    fun releaseButtonSoon(button: Int) {
        releaseAfterFrames(2) { releaseButton(button) }
    }

    // ---- savestates ---------------------------------------------------------
    //
    // Whole-core states (poke_save_state's contract, the same one iOS
    // exposes): one slot per game, named by the game's crc identity,
    // under filesDir — states must survive cache eviction, like the
    // saves do. A state save also flushes the battery SRAM first
    // (the wrapper's own rule), so a state and its .sav never
    // disagree about which is newer.

    private fun stateFile(context: Context): File {
        val dir = File(context.filesDir, "genesis-states")
        if (!dir.isDirectory) dir.mkdirs()
        // The identity is "crc:<hex>" — the colon is not a filename
        // character worth gambling on, so it is folded to a dash.
        val name = if (gameCode.isNotEmpty()) {
            gameCode.replace(':', '-')
        } else {
            "genesis"
        }
        return File(dir, "$name.state")
    }

    fun saveState(context: Context): Boolean {
        if (!running) return false
        val ok = try {
            GenesisAccessibilityScript.saveState(stateFile(context).absolutePath)
        } catch (t: Throwable) {
            false
        }
        if (!ok) Log.w(TAG, "Genesis save state failed")
        return ok
    }

    fun loadState(context: Context): Boolean {
        if (!running) return false
        val file = stateFile(context)
        if (!file.isFile) return false
        return try {
            GenesisAccessibilityScript.loadState(file.absolutePath)
        } catch (t: Throwable) {
            false
        }
    }

    /** True when a state slot exists for the loaded game (the PSP shape). */
    fun hasSavedState(context: Context): Boolean = stateFile(context).isFile

    // ---- the picture ----------------------------------------------------------

    /**
     * The current frame for the game picture, or null — the PSP
     * session's shape: guarded by the running flag (GamePicture polls
     * on its own cadence and can ask after the session ended) and by
     * a catch (a build without the surface answers with an exception,
     * not a crash).
     */
    fun currentFrame(): Triple<IntArray, Int, Int>? {
        if (!running) return null
        return try {
            GenesisAccessibilityScript.currentFrame()
        } catch (t: Throwable) {
            null
        }
    }

    // ---- speech -----------------------------------------------------------------

    /**
     * Stop speech: the platform voice only. The DS twin also sends
     * its host a StopSpeech command for the reader queue's sake;
     * Genesis has no reader and no queue, so the bridge stop is the
     * whole of it.
     */
    fun stopSpeech(context: Context) {
        try {
            AccessibilityScript.speechBridge(context.applicationContext).stop()
        } catch (t: Throwable) {
            // No bridge to stop.
        }
    }

    /**
     * Repeat the newest line in the shared spoken history (the PSP
     * session's delegates, over the same bridge): false when there is
     * nothing to repeat, and the SCREEN speaks the refusal — a
     * refused repeat answered silently reads as a broken button.
     */
    fun repeatLastSpeech(): Boolean = AccessibilityScript.repeatLastSpeech()

    /** Repeat the line before the last-repeated one; see [repeatLastSpeech]. */
    fun repeatOlderSpeech(): Boolean = AccessibilityScript.repeatOlderSpeech()

    /** The selected adapter's registry id ("genesis"), or "" before a load lands. */
    val adapterId: String get() = selectedAdapterId

    /** The selected adapter's display name ("Sega Genesis"), or "" before a load lands. */
    val adapterName: String get() = selectedAdapterName

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
            Log.w(TAG, "Genesis audio unavailable: min buffer size $minBuffer")
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
            Log.w(TAG, "Genesis audio unavailable: ${t.message}")
            return
        }
        audioTrack = track
        audioFramesWritten = 0L
        audioRunning = true
        track.play()
        val thread = Thread { audioLoop(track) }
        thread.name = "genesis-audio"
        audioThread = thread
        thread.start()
        Log.i(TAG, "Genesis audio started: AudioTrack at $AUDIO_RATE Hz, stereo")
    }

    private fun audioLoop(track: AudioTrack) {
        val buffer = ShortArray(2048 * 2)
        while (audioRunning) {
            val frames = try {
                GenesisAccessibilityScript.readAudio(buffer, 2048)
            } catch (t: Throwable) {
                0
            }
            if (frames > 0) {
                // No cue mix, deliberately: PspCueSynth is fed by
                // reader adapters, and Genesis has none — this drain
                // carries the game alone (see the class doc).
                track.write(buffer, 0, frames * 2, AudioTrack.WRITE_BLOCKING)
                audioFramesWritten += frames
            } else {
                // Dry: the core refills as frames run. Sleep briefly
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

    /** True once a Genesis game is running, so the pad and states can appear. */
    val isRunning: Boolean get() = running

    /** The loaded game's crc identity, or "" before a load lands. */
    val loadedGameCode: String get() = gameCode
}

/**
 * The Genesis button ids: the shared pad's POKE_BTN_* slots
 * (pokecore.h), named for the Genesis pad the way the shared Core
 * registry names them (Core/systems.cpp's Genesis table and
 * Core/oga_core.cpp's Genesis translation).
 *
 * ⛔ THE SLOTS ARE THE CORE'S, THE NAMES ARE THE CONSOLE'S. The
 * Genesis pad's six face buttons do not fit the shared pad's four
 * face slots one-for-one, so the registry's prescription spreads
 * them: A and B keep their slots, C rides the X slot, X rides the L
 * (left shoulder) slot, Y keeps its slot, Z rides the R slot, and
 * Mode rides the Select slot — which is what lets the positional
 * controller layer (west = A, south = B, east = C, north = Y, L1 =
 * X, R1 = Z) drive a Genesis game exactly like every other console.
 * The launcher contract test checks this object against pokecore.h
 * AND against the core's own translation table.
 */
object GenesisKeys {
    const val A = 0
    const val B = 1
    const val MODE = 2
    const val START = 3
    const val RIGHT = 4
    const val LEFT = 5
    const val UP = 6
    const val DOWN = 7
    const val Z = 8
    const val X = 9
    const val C = 10
    const val Y = 11
}

/**
 * Drives the Genesis session's frames while a Genesis game runs —
 * PACED TO THE CONSOLE, NOT TO THE DISPLAY (the DS clock's reasoning:
 * one emulated frame per display frame runs the game fast on any
 * screen above 60 Hz).
 *
 * ⛔ THE PACE IS THE NTSC RATE, AND PAL RUNS FAST UNDER IT — said
 * plainly rather than hidden: the wrapper picks the machine's region
 * from the cartridge itself, but this clock is not told which it
 * picked, so it paces at the NTSC Genesis rate (~59.92 Hz) and a PAL
 * cartridge (50 Hz) runs about a fifth fast. The core exposes no
 * frame-rate answer to pace by; when it does, this clock should ask
 * it. Kept beside its own session, like rememberDsFrameClock beside
 * its.
 */
@Composable
fun rememberGenesisFrameClock(running: Boolean) {
    if (!running) return
    androidx.compose.runtime.LaunchedEffect(Unit) {
        // The NTSC Genesis frame interval: 1 s / 59.9227 Hz ≈ 16.688 ms.
        val frameNs = (1_000_000_000.0 / 59.9227).toLong()
        var nextFrameAt = 0L
        while (true) {
            val now = androidx.compose.runtime.withFrameNanos { it }
            if (nextFrameAt == 0L) nextFrameAt = now
            if (now >= nextFrameAt) {
                GenesisGameSession.runFrame()
                nextFrameAt += frameNs
                if (nextFrameAt <= now) nextFrameAt = now + frameNs
            }
        }
    }
}
