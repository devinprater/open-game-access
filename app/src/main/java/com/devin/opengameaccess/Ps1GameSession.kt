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
import me.magnum.melonds.accessibility.Ps1AccessibilityScript
import me.magnum.melonds.accessibility.Ps1RomResolver
import java.io.File

/**
 * The Sony PlayStation session, driven directly from Compose.
 *
 * ⛔ A SIBLING OF [GenesisGameSession], NOT A VARIANT. Same shape — an
 * object the screen calls, a Compose-observable running flag, a last
 * error, held input, a Compose frame clock, an audio drain thread, an
 * asynchronous start — over the shared Core host (Core/pokecore.cpp)
 * and its PCSX ReARMed backend (Core/ps1_core.cpp). The launcher keeps
 * the sessions apart by the picked file's extension.
 *
 * ⛔ NO READER YET, SAID PLAINLY. The registry's PlayStation row
 * refuses to attach: no PlayStation reader exists. So this session is
 * pad, save states, the memory card, audio and the game picture — a
 * PlayStation game in this build is playable, but not accessible.
 * Where Am I and the controller layer's reader chords answer with the
 * honest line the other reader-less cases use.
 *
 * ⛔ THE PAD IS THE DIGITAL PAD, BY POSITION. Cross, Circle, Triangle
 * and Square, L1/R1, Select/Start and the d-pad ride the shared pad
 * slots the core's translation table names (see [Ps1Keys]). L2/R2 are
 * not on the pad: on Android those triggers are the reader's ask and
 * browse modifiers, and the analog sticks are not wired yet.
 *
 * ⛔ THE MEMORY CARD ROUND-TRIPS THROUGH ITS FILE. The save path handed
 * to the host is the slot-1 card (Ps1RomResolver keeps it in internal
 * storage): restored at load, written back on stop and before a state
 * save when it changed.
 *
 * ⛔ START IS ASYNCHRONOUS, LIKE THE OTHER SESSIONS'. A disc image is
 * hundreds of megabytes; copying it on the button's thread would
 * freeze the launcher and TalkBack with it.
 */
object Ps1GameSession {

    private const val TAG = "Ps1Session"

    /** The rate the wrapper hands audio out at (ps1_core.h's PS1_APP_AUDIO_RATE). */
    private const val AUDIO_RATE = 32768

    /**
     * The application context, kept from [start] so [runFrame] can
     * read the game-sound switch.
     */
    @Volatile
    private var appContext: Context? = null

    /**
     * True once a disc is loaded and the core is running.
     *
     * ⛔ COMPOSE-OBSERVABLE, LIKE THE DS FLAG, FOR THE DS REASON: the
     * launcher reads [isRunning] directly when it decides whether
     * the PlayStation controls exist, so a session that ends ITSELF (a
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
     * The loaded game's identity: the disc's boot serial ("SLUS00594"),
     * or "" for a disc without one, once a load lands.
     */
    @Volatile
    private var gameCode: String = ""

    /** The adapter the host selected for the loaded game ("ps1"), or "". */
    @Volatile
    private var selectedAdapterId: String = ""

    /** The selected adapter's display name ("PlayStation"), or "" when none. */
    @Volatile
    private var selectedAdapterName: String = ""

    /**
     * The game-sound gate's last applied value, so [runFrame] calls
     * the native setter only when the switch actually moved.
     */
    @Volatile
    private var audioEnabledApplied: Boolean? = null

    fun romExtensions(): Set<String> = Ps1AccessibilityScript.ROM_EXTENSIONS

    /** True when a picked file name is a PlayStation BIOS to import, not a game. */
    fun isBiosName(name: String): Boolean = Ps1RomResolver.isBiosName(name)

    /**
     * Keep a picked BIOS for every later disc. Returns the line to
     * speak: what happened, in words.
     */
    fun importBios(context: Context, uri: Uri, name: String): String {
        val stored = Ps1RomResolver.importBios(context.applicationContext, uri, name)
        return if (stored != null) {
            "PlayStation BIOS $stored saved. PlayStation games will use it from now on."
        } else {
            "$name is not a PlayStation BIOS this app can use. A BIOS file is exactly 512 kilobytes."
        }
    }

    fun handlesExtension(extension: String?): Boolean =
        Ps1AccessibilityScript.handlesExtension(extension)

    /**
     * True when a picked .iso, .pbp or .bin is a PlayStation disc,
     * decided by its first bytes (the core resolver's own test, so the
     * launcher and the core agree). Any other extension, an unreadable
     * file, or a build without the PlayStation core answers false, and
     * the file keeps its by-name console (PSP or Genesis).
     */
    fun isPs1Disc(context: Context, uri: Uri, extension: String?): Boolean {
        if (extension?.lowercase() !in Ps1AccessibilityScript.SNIFFED_EXTENSIONS) return false
        return try {
            val header = ByteArray(Ps1AccessibilityScript.HEADER_BYTES)
            var length = 0
            context.contentResolver.openInputStream(uri)?.use { input ->
                while (length < header.size) {
                    val n = input.read(header, length, header.size - length)
                    if (n <= 0) break
                    length += n
                }
            } ?: return false
            Ps1AccessibilityScript.headerIsPs1(header, length)
        } catch (e: UnsatisfiedLinkError) {
            false
        } catch (t: Throwable) {
            Log.w(TAG, "could not read the header of $uri: ${t.message}")
            false
        }
    }

    /**
     * True when the last start refused because the disc lists files
     * (a .cue's tracks, an .m3u's discs) that no granted folder holds.
     * The launcher answers it with the folder picker, [rememberFolder],
     * and a fresh start.
     */
    val needsFolder: Boolean get() = Ps1RomResolver.needsFolder

    /** Keep read access to the folder the player chose for a disc's tracks. */
    fun rememberFolder(context: Context, treeUri: Uri): Boolean =
        Ps1RomResolver.rememberFolder(context.applicationContext, treeUri)

    // The loaded disc's frame rate, read once when the load lands (the
    // core's own timing: 50 for PAL, about 59.94 for NTSC); 0 = unknown.
    @Volatile
    private var discFrameRate: Double = 0.0

    /** The rate the frame clock paces this disc at, in frames per second. */
    val frameRate: Double
        get() = discFrameRate.takeIf { it in 40.0..80.0 } ?: NTSC_RATE

    /** The NTSC PlayStation rate, used before a load reports its own. */
    const val NTSC_RATE = 59.94

    /**
     * True when this build can actually run PlayStation games.
     *
     * ⛔ ASK THE CORE, NOT THE PACKAGE — the DS probe's reasoning: the
     * cores SHARE the frontend .so, so its presence says nothing
     * about PlayStation; the question is whether the PlayStation surface was
     * compiled into it, and the only honest witness is the JNI probe
     * that exists exactly when it was (see
     * Ps1AccessibilityScript). On armeabi-v7a the call throws
     * UnsatisfiedLinkError; that throw is the "no", caught here.
     */
    fun coreAvailable(context: Context): Boolean {
        return try {
            Ps1AccessibilityScript.isCoreLinked()
        } catch (e: UnsatisfiedLinkError) {
            // The armeabi-v7a answer: the surface is not in the .so.
            Log.w(TAG, "PlayStation path unavailable: ${e.message}")
            false
        } catch (t: Throwable) {
            Log.w(TAG, "PlayStation path unavailable: ${t.message}")
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
     * Load a PlayStation disc image and start the core. [onResult]
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
                // registered with the PlayStation host, like every other
                // system's speech in the app.
                val bridge = AccessibilityScript.speechBridge(app)
                try {
                    Ps1AccessibilityScript.setSpeechBridge(bridge)
                } catch (t: Throwable) {
                    Log.w(TAG, "PlayStation speech bridge registration threw: ${t.message}")
                }
                selectedAdapterId = try {
                    Ps1AccessibilityScript.adapterId()
                } catch (t: Throwable) {
                    ""
                }
                selectedAdapterName = try {
                    Ps1AccessibilityScript.adapterName()
                } catch (t: Throwable) {
                    ""
                }
                storedAnnouncementView?.let { AccessibilityScript.setAnnouncementView(it) }
                startAudio()
                Log.i(TAG, "PlayStation session running: $gameCode (adapter $selectedAdapterId)")
                onResult(null)
            }
        }
        worker.name = "ps1-load"
        worker.start()
    }

    /**
     * One frame of reader cues, driven into [PspCueSynth] the way the PSP session does: the beacon
     * (the closeness sound) and the one-shot hit beeps, gated by the game's effective battle-cues
     * value (its Per Game Settings override, else the Settings switch). A null poll (no reader, or
     * not attached yet) silences everything: a cue must never outlive the state that fed it. The
     * poll is made here and nowhere else, because the adapter raises each beep exactly once.
     */
    private fun pollCues() {
        val poll = try {
            Ps1AccessibilityScript.cuePoll()
        } catch (t: Throwable) {
            null
        }
        if (poll == null) {
            PspCueSynth.silenceAll()
            return
        }
        val ctx = appContext
        val on = ctx != null && GameSettings.effectiveBattleCues(ctx, selectedAdapterId)
        PspCueSynth.silenceFieldBeacon()
        PspCueSynth.updateBeacon(poll.state, poll.dist, on)
        PspCueSynth.updateOneShots(poll.healthBits, poll.foeHz, poll.foePan, poll.ownHz, poll.ownPan, on)
    }

    /** The worker-thread half of [start]: resolve, load, start. */
    private fun loadOnWorker(context: Context, uri: Uri, gen: Int): String? {
        val romPath = Ps1RomResolver.resolve(context, uri)
            ?: return Ps1RomResolver.lastProblem.ifEmpty { "Could not read that game file." }
        val savePath = Ps1RomResolver.savePathFor(context, romPath)
            ?: return "The memory card file for that game could not be prepared."
        // ⛔ THE NATIVE HALF RUNS UNDER THE LOAD LOCK, RE-CHECKING
        // THE GENERATION FIRST. The core is a singleton: two loads
        // racing it, or a load racing stop()'s teardown, would
        // interleave create/load/start/destroy calls on one core. The
        // lock serialises the native calls; the generation check
        // keeps a superseded load from touching the core at all (its
        // ROM copy above is its own file and needs no lock); and a
        // load that completes records its generation as the core's
        // owner, which is what the completion's teardown rule in
        // start() reads. (No script step: the PlayStation backend has no
        // reader set to hand over — the DS load's middle step has no
        // counterpart here.)
        synchronized(loadLock) {
            if (gen != generation) return LOAD_SUPERSEDED
            // The debug channel's folder goes in before the load, so the new core reads
            // cmd.txt from its first frame. Never fatal: a missing folder only means no
            // file-driven commands.
            try {
                Ps1AccessibilityScript.setDebugDir(DebugChannel.dir(context))
            } catch (t: Throwable) {
                Log.w(TAG, "debug channel folder not set", t)
            }
            val loaded = try {
                Ps1AccessibilityScript.loadRom(romPath, savePath)
            } catch (t: Throwable) {
                Log.e(TAG, "PlayStation load threw", t)
                return "The PlayStation core is not in this build."
            }
            if (!loaded) {
                val why = Ps1AccessibilityScript.lastError()
                return if (why.isNotEmpty()) {
                    "That PlayStation game could not be loaded. $why"
                } else {
                    "That PlayStation game could not be loaded."
                }
            }
            gameCode = Ps1AccessibilityScript.gameCode()
            discFrameRate = try {
                Ps1AccessibilityScript.frameRate()
            } catch (t: Throwable) {
                0.0
            }
            if (!Ps1AccessibilityScript.startCore()) {
                val why = Ps1AccessibilityScript.lastError()
                teardownNative()
                return if (why.isNotEmpty()) {
                    "That PlayStation game could not be started. $why"
                } else {
                    "That PlayStation game could not be started."
                }
            }
            coreGeneration = gen
            return null
        }
    }

    /**
     * Stop the session: audio first (joined, so nothing is still
     * draining), then the core — whose stop writes the memory card
     * back to its file (see the class doc). Safe with nothing
     * running — including on builds with no PlayStation core, where the
     * native stop's absence is swallowed by [teardownNative].
     * MainActivity.onDestroy calls this unconditionally, next to the
     * other sessions' stops.
     */
    fun stop() {
        generation++
        running = false
        discFrameRate = 0.0
        heldButtons.clear()
        pendingReleases.clear()
        selectedAdapterId = ""
        selectedAdapterName = ""
        audioEnabledApplied = null
        // A tone left running would outlive the game that fed it.
        PspCueSynth.silenceAll()
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
            Ps1AccessibilityScript.setSpeechBridge(null)
        } catch (t: Throwable) {
            // No PlayStation surface in this build; nothing to clear.
        }
        try {
            Ps1AccessibilityScript.stopCore()
        } catch (t: Throwable) {
            Log.w(TAG, "PlayStation stop threw: ${t.message}")
        }
    }

    /** One emulated frame. Driven from the Compose frame clock, see [rememberPs1FrameClock]. */
    fun runFrame() {
        if (!running) return
        val ok = try {
            Ps1AccessibilityScript.runFrame()
        } catch (t: Throwable) {
            false
        }
        if (!ok) {
            // The core stopped answering mid-session: end it the way
            // Quit would, rather than leaving a "running" screen over
            // a dead core.
            val why = try {
                Ps1AccessibilityScript.lastError()
            } catch (t: Throwable) {
                ""
            }
            Log.e(TAG, "PlayStation frame failed, ending the session: $why")
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
                    Ps1AccessibilityScript.setAudioEnabled(on)
                } catch (t: Throwable) {
                    // No surface in this build; nothing to gate.
                }
            }
        }
        // The reader's cues, once per frame, after the frame ran (see pollCues).
        pollCues()
        // A frame has now sampled whatever is held: count the tap
        // releases against it (see the frame-counted releases note).
        tickPendingReleases()
        val completed = try {
            Ps1AccessibilityScript.framesCompleted
        } catch (t: Throwable) {
            0L
        }
        if (completed >= lastHeartbeatFrame + 600L) {
            lastHeartbeatFrame = completed
            Log.i(TAG, "PlayStation running: frames=$completed audioFrames=$audioFramesWritten")
        }
    }

    private var lastHeartbeatFrame = 0L

    // ---- the emulated pad ---------------------------------------------------
    //
    // ⛔ PER-BUTTON, NOT A MASK — the PSP/DS shape, for their reason:
    // poke_set_button latches one button per call. Press and release
    // forward as they happen, and the held set is remembered only so
    // stop() leaves nothing stuck. No pad echoes are forwarded: the
    // echo contract belongs to readers, and PlayStation has none.

    private val heldButtons = mutableSetOf<Int>()

    fun pressButton(button: Int) {
        if (!running) return
        heldButtons.add(button)
        Ps1AccessibilityScript.setButton(button, true)
    }

    fun releaseButton(button: Int) {
        heldButtons.remove(button)
        if (running) Ps1AccessibilityScript.setButton(button, false)
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
            Ps1AccessibilityScript.tapButton(button, 2)
        } catch (t: Throwable) {
            Log.w(TAG, "PlayStation tap failed: ${t.message}")
        }
    }

    /**
     * Move one DualShock stick ([stick] 0 the left, 1 the right; each
     * axis -1..1, +x right, +y down). A digital game ignores it.
     */
    fun setStick(stick: Int, x: Float, y: Float) {
        if (!running) return
        try {
            Ps1AccessibilityScript.setAnalog(stick, x, y)
        } catch (t: Throwable) {
            Log.w(TAG, "PlayStation stick failed: ${t.message}")
        }
    }

    /** Release a held button once two frames have sampled it. */
    fun releaseButtonSoon(button: Int) {
        releaseAfterFrames(2) { releaseButton(button) }
    }

    // ---- savestates ---------------------------------------------------------
    //
    // Whole-machine states (poke_save_state's contract): one slot per
    // game, named by the disc's serial, under filesDir so they survive
    // cache eviction. A state save writes the memory card first (the
    // wrapper's own rule), so a state and its card never disagree.

    private fun stateFile(context: Context): File {
        val dir = File(context.filesDir, "ps1-states")
        if (!dir.isDirectory) dir.mkdirs()
        val name = if (gameCode.isNotEmpty()) {
            gameCode.replace(Regex("[^A-Za-z0-9_-]"), "_")
        } else {
            "ps1"
        }
        return File(dir, "$name.state")
    }

    fun saveState(context: Context): Boolean {
        if (!running) return false
        val ok = try {
            Ps1AccessibilityScript.saveState(stateFile(context).absolutePath)
        } catch (t: Throwable) {
            false
        }
        if (!ok) Log.w(TAG, "PlayStation save state failed")
        return ok
    }

    fun loadState(context: Context): Boolean {
        if (!running) return false
        val file = stateFile(context)
        if (!file.isFile) return false
        return try {
            Ps1AccessibilityScript.loadState(file.absolutePath)
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
            Ps1AccessibilityScript.currentFrame()
        } catch (t: Throwable) {
            null
        }
    }

    // ---- speech -----------------------------------------------------------------

    /**
     * Stop speech: the platform voice, then pokecore's StopSpeech
     * command (37), which clears the core's announcement queue so a
     * queued line can't start again a moment later. Both halves iOS's
     * GameSession.stopSpeech performs, and the DS, PSP and Mesen
     * sessions' shape.
     *
     * ⛔ STOP SPEECH NEVER STOPS THE GAME. It must not call [stop] or
     * anything that tears the core down: the Game Boy path once did,
     * and the game died while the screen still said it was running.
     * scripts/android-stop-speech-test.py holds every session to this.
     */
    fun stopSpeech(context: Context) {
        try {
            AccessibilityScript.speechBridge(context.applicationContext).stop()
        } catch (t: Throwable) {
            // No bridge to stop; the command below is a no-op then too.
        }
        try {
            Ps1AccessibilityScript.adapterCommand(Ps1AdapterCommands.STOP_SPEECH)
        } catch (t: Throwable) {
            // No PlayStation surface in this build; nothing queued to stop.
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

    /** The selected adapter's registry id ("ps1"), or "" before a load lands. */
    val adapterId: String get() = selectedAdapterId

    /** The selected adapter's display name ("PlayStation"), or "" before a load lands. */
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
            Log.w(TAG, "PlayStation audio unavailable: min buffer size $minBuffer")
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
            Log.w(TAG, "PlayStation audio unavailable: ${t.message}")
            return
        }
        audioTrack = track
        audioFramesWritten = 0L
        audioRunning = true
        track.play()
        val thread = Thread { audioLoop(track) }
        thread.name = "ps1-audio"
        audioThread = thread
        thread.start()
        Log.i(TAG, "PlayStation audio started: AudioTrack at $AUDIO_RATE Hz, stereo")
    }

    private fun audioLoop(track: AudioTrack) {
        val buffer = ShortArray(2048 * 2)
        while (audioRunning) {
            val frames = try {
                Ps1AccessibilityScript.readAudio(buffer, 2048)
            } catch (t: Throwable) {
                0
            }
            if (frames > 0) {
                // The cue synth mixes into the game's audio here, at the drain, the one
                // output path (see PspCueSynth). Silent unless a frame poll armed a voice.
                PspCueSynth.render(buffer, frames)
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

    /** True once a PlayStation game is running, so the pad and states can appear. */
    val isRunning: Boolean get() = running

    /** The loaded game's disc serial, or "" before a load lands. */
    val loadedGameCode: String get() = gameCode
}

/**
 * The PlayStation button ids: the shared pad's POKE_BTN_* slots
 * (pokecore.h), named for the PlayStation pad the way the core's own
 * translation table (Core/oga_core.cpp's kPs1Button) lands them:
 * Cross on A, Circle on B, Triangle on X, Square on Y, L1 on L, R1 on
 * R, and L2, R2, L3, R3 on the extended slots past the shared twelve
 * (POKE_BTN_L2..POKE_BTN_R3). The launcher contract test checks this
 * object against pokecore.h AND against that table.
 */
object Ps1Keys {
    const val CROSS = 0
    const val CIRCLE = 1
    const val SELECT = 2
    const val START = 3
    const val RIGHT = 4
    const val LEFT = 5
    const val UP = 6
    const val DOWN = 7
    const val R1 = 8
    const val L1 = 9
    const val TRIANGLE = 10
    const val SQUARE = 11
    const val L2 = 12
    const val R2 = 13
    const val L3 = 14
    const val R3 = 15
}

/**
 * Drives the PlayStation session's frames while a game runs, PACED TO
 * THE CONSOLE, NOT TO THE DISPLAY (the Genesis clock's reasoning).
 *
 * ⛔ THE PACE IS THE DISC'S OWN RATE. The core reports it after the
 * load (Ps1GameSession.frameRate: 50 for a PAL disc, about 59.94 for
 * NTSC), so a PAL game runs at its real speed instead of a fifth fast.
 * The interval is re-read every frame, so it follows the load that
 * started this clock.
 */
@Composable
fun rememberPs1FrameClock(running: Boolean) {
    if (!running) return
    androidx.compose.runtime.LaunchedEffect(Unit) {
        var nextFrameAt = 0L
        while (true) {
            val now = androidx.compose.runtime.withFrameNanos { it }
            val frameNs = (1_000_000_000.0 / Ps1GameSession.frameRate).toLong()
            if (nextFrameAt == 0L) nextFrameAt = now
            if (now >= nextFrameAt) {
                Ps1GameSession.runFrame()
                nextFrameAt += frameNs
                if (nextFrameAt <= now) nextFrameAt = now + frameNs
            }
        }
    }
}

/**
 * The one pokecore command this session sends itself, mirrored from
 * Core/adapter.h's Command enum (scripts/android-stop-speech-test.py
 * checks the value against the enum).
 */
object Ps1AdapterCommands {
    const val STOP_SPEECH = 37
}
