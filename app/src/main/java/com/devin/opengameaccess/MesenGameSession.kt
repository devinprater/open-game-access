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
import me.magnum.melonds.accessibility.MesenAccessibilityScript
import me.magnum.melonds.accessibility.MesenRomResolver
import java.io.File

/**
 * The NES and Super Nintendo session, driven directly from Compose.
 *
 * ⛔ A SIBLING OF [DsGameSession], NOT A VARIANT. Same shape — an object
 * the screen calls, a running flag, a last error, held input, a Compose
 * frame clock, an audio drain thread, an asynchronous start — over a
 * different pair of backends of the SAME shared Core host
 * (Core/pokecore.cpp, the translation unit the iOS app ships): its NES
 * and SNES branches drive Core/mesen_core.cpp and Core/snes_core.cpp,
 * the MesenCE glue, compiled into this build by the overlay's section
 * 10 with the ABIs' former stubs removed. The other sessions' behavior
 * is untouched by this file's existence; the launcher keeps the
 * sessions apart by the picked file's extension.
 *
 * ⛔ THE READER STORY IS THE HOST'S, IN TWO KINDS. The SNES reader is a
 * native adapter (A Link to the Past), selected by the shared registry
 * from the ROM's CRC when the host attaches it — the DS shape. The NES
 * readers are Lua mods (Dragon Warrior Access, Zelda 1 Access) running
 * in mesen_core's own Lua host; this session's part is what iOS's
 * GameSession does around them: after the load, the core names the set
 * the ROM needs by its own CRC32 (never by filename — "" is the normal
 * answer for a game no reader covers); the set is STAGED — a writable
 * copy under filesDir, because the readers write into their own tree
 * (speech text, settings, waypoint progress) and the bundled assets are
 * read-only — and its directory is handed to the core before the start,
 * which is what loads the reader. The readers' speech reaches the
 * shared bridge through the host's speech callback, which pokecore
 * registers at NES load (the NesSayForward there — the GBA forwards'
 * twin). Readiness is polled once a frame in [runFrame], pad presses
 * are forwarded as adapter echoes (see [pressButton]), and the reader
 * buttons on the launcher screen call [sendAdapterCommand].
 *
 * ⛔ THE LOAD'S SECOND PATH IS A FOLDER, NOT A SAVE FILE. pokecore's
 * NES/SNES branches hand it to Mesen as its HOME FOLDER
 * (FolderUtilities::SetHomeFolder): without one Mesen refuses every
 * ROM, and the game's battery save lives inside it. MesenRomResolver
 * owns choosing it — one folder per game under internal storage, so a
 * re-pick keeps its save.
 *
 * ⛔ START IS ASYNCHRONOUS, LIKE THE OTHER SESSIONS'. The resolver may
 * copy a large ROM, and the host's load parses it; both would freeze
 * the launcher — and TalkBack with it — on the button's thread. The
 * generation counter and the load lock are the DS session's, for the
 * DS session's reasons: a stop that lands mid-load tears down only the
 * core that load itself built.
 *
 * ⛔ AUDIO IS THE SESSION'S OWN THREAD, THE SHARED SHAPE. pokecore
 * mixes at 32768 Hz; this session drains it into an AudioTrack from a
 * thread that lives exactly as long as the session, with PspCueSynth
 * mixing in at the drain (the one output path) and the Game sound
 * switch honoured the PSP way — the game's samples silenced before the
 * cue mix, never instead of draining. TODAY the Mesen backends hand
 * out silence (their read_audio slots are unwired in the shared core
 * itself — see MesenAccessibilityScript), so the drain mostly carries
 * the cue synth; the readers' speech does not travel this path.
 */
object MesenGameSession {

    private const val TAG = "MesenSession"

    /** The rate the core hands audio out at (pokecore.h's contract). */
    private const val AUDIO_RATE = 32768

    /**
     * The application context, kept from [start] so the per-frame cue
     * poll can read the battle-cues switch (see [pollCues]).
     */
    @Volatile
    private var appContext: Context? = null

    /**
     * True once a cartridge is loaded and the core is running.
     *
     * ⛔ COMPOSE-OBSERVABLE, LIKE THE DS SESSION'S FLAG — the launcher
     * reads [isRunning] directly when it decides whether the Mesen
     * controls exist, so a session that ends ITSELF (a frame failure
     * in [runFrame] calls [stop]) takes its controls down with it.
     */
    private var running by androidx.compose.runtime.mutableStateOf(false)

    /** The last failure, for the screen to speak instead of guessing. */
    @Volatile
    private var lastError: String = ""

    /** Bumped by every start and every stop; a stale load must not land. */
    @Volatile
    private var generation = 0

    /**
     * Serialises the native half of a load (resolve the reader set,
     * load ROM, start core) against [stop]'s teardown, so the two can
     * never interleave calls into the one native core.
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

    /** The loaded game's code (a Mesen database id, or "crc:<hex>" for SNES), once a load lands. */
    @Volatile
    private var gameCode: String = ""

    /**
     * The backend the host resolved for the loaded game — "nes" or
     * "snes" — captured at load. The launcher shapes its pad and its
     * state group from this (the console the core chose), never from
     * the picked file's extension.
     */
    @Volatile
    private var loadedBackendId: String = ""

    /** The adapter the host selected for the loaded game, or "". */
    @Volatile
    private var selectedAdapterId: String = ""

    /** The selected adapter's display name, or "" when there is none. */
    @Volatile
    private var selectedAdapterName: String = ""

    /**
     * The adapter's latest readiness answer, refreshed once a frame by
     * [runFrame]. The command buttons and the pad echoes gate on this.
     */
    @Volatile
    private var adapterReadyNow: Boolean = false

    fun romExtensions(): Set<String> = MesenAccessibilityScript.ROM_EXTENSIONS

    fun handlesExtension(extension: String?): Boolean =
        MesenAccessibilityScript.handlesExtension(extension)

    /**
     * True when this build can actually run NES and SNES games.
     *
     * ⛔ ASK THE CORE, NOT THE PACKAGE — the DS/PSP probe's reasoning:
     * the cores SHARE the frontend .so, so its presence says nothing
     * about Mesen; the question is whether the Mesen surface was
     * compiled into it, and the only honest witness is the JNI probe
     * that exists exactly when it was (see MesenAccessibilityScript).
     * On armeabi-v7a the call throws UnsatisfiedLinkError; that throw
     * is the "no", caught here.
     */
    fun coreAvailable(context: Context): Boolean {
        return try {
            MesenAccessibilityScript.isCoreLinked()
        } catch (e: UnsatisfiedLinkError) {
            // The armeabi-v7a answer: the surface is not in the .so.
            Log.w(TAG, "Mesen path unavailable: ${e.message}")
            false
        } catch (t: Throwable) {
            Log.w(TAG, "Mesen path unavailable: ${t.message}")
            false
        }
    }

    /**
     * Point announcements at the live-region view.
     *
     * ⛔ THE SAME TWO-STEP AS THE OTHER SESSIONS, FOR THE SAME REASON.
     * The attach that happens during composition forwards to a speech
     * bridge that does not exist yet (it is built when a game starts)
     * and is silently a no-op; without the re-forward in [start] the
     * view stays null for the whole session and the bridge speaks
     * through TextToSpeech on top of the screen reader. MainActivity
     * calls this from the same SideEffect as the other sessions'
     * attaches; all store the same view and forward to the same
     * AccessibilityScript.
     */
    fun attachAnnouncementView(view: View) {
        storedAnnouncementView = view
        AccessibilityScript.setAnnouncementView(view)
    }

    @Volatile
    private var storedAnnouncementView: View? = null

    /**
     * Load a NES or SNES cartridge image and start the core.
     * [onResult] is called on the main thread with null on success, or
     * a player-facing message on failure. See the class doc for why
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
                    // newer load has claimed — if a newer start has
                    // since loaded its own core, that core is the new
                    // session's, and destroying it here would kill a
                    // game the player is looking at.
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
                // registered with the Mesen host: reader speech lands
                // on this one instance, like every other system's
                // speech in the app.
                val bridge = AccessibilityScript.speechBridge(app)
                try {
                    MesenAccessibilityScript.setSpeechBridge(bridge)
                } catch (t: Throwable) {
                    Log.w(TAG, "Mesen speech bridge registration threw: ${t.message}")
                }
                selectedAdapterId = try {
                    MesenAccessibilityScript.adapterId()
                } catch (t: Throwable) {
                    ""
                }
                selectedAdapterName = try {
                    MesenAccessibilityScript.adapterName()
                } catch (t: Throwable) {
                    ""
                }
                storedAnnouncementView?.let { AccessibilityScript.setAnnouncementView(it) }
                startAudio()
                Log.i(TAG, "Mesen session running: $loadedBackendId $gameCode")
                onResult(null)
            }
        }
        worker.name = "mesen-load"
        worker.start()
    }

    /** The worker-thread half of [start]: resolve, load, stage the reader, start. */
    private fun loadOnWorker(context: Context, uri: Uri, gen: Int): String? {
        val romFile = try {
            MesenRomResolver.resolveRom(context, uri)
        } catch (t: Throwable) {
            Log.e(TAG, "Mesen ROM could not be resolved", t)
            return t.message ?: "Could not read that game file."
        }
        val homePath = MesenRomResolver.homePathFor(context, romFile)
        // ⛔ THE NATIVE HALF RUNS UNDER THE LOAD LOCK, RE-CHECKING THE
        // GENERATION FIRST — the DS session's rule, for the DS
        // session's reasons: the core is a singleton, and a load
        // racing stop()'s teardown would interleave
        // create/load/start/destroy calls on one core.
        synchronized(loadLock) {
            if (gen != generation) return LOAD_SUPERSEDED
            val loaded = try {
                MesenAccessibilityScript.loadRom(romFile.absolutePath, homePath)
            } catch (t: Throwable) {
                Log.e(TAG, "Mesen load threw", t)
                return "The NES and Super Nintendo core is not in this build."
            }
            if (!loaded) {
                val why = MesenAccessibilityScript.lastError()
                return if (why.isNotEmpty()) {
                    "That game could not be loaded. $why"
                } else {
                    "That game could not be loaded."
                }
            }
            gameCode = MesenAccessibilityScript.gameCode()
            loadedBackendId = try {
                MesenAccessibilityScript.backendId()
            } catch (t: Throwable) {
                ""
            }
            // ⛔ THE READER SET IS STAGED BETWEEN THE LOAD AND THE
            // START — iOS's GameSession order: the set is a property
            // of the loaded cartridge (the core names it by CRC32),
            // and the start is what loads the reader. A staging
            // failure is not fatal to the load — most games have no
            // reader at all — but it is logged loudly: Dragon Warrior
            // without its reader is a silent game, and the workflow's
            // gates verify the staged assets exist in the shipped APK.
            val setName = try {
                MesenAccessibilityScript.readerSet()
            } catch (t: Throwable) {
                ""
            }
            if (setName.isNotEmpty()) {
                val staged = stageReaderSet(context, setName)
                if (staged != null) {
                    try {
                        MesenAccessibilityScript.setScriptDir(staged.absolutePath)
                    } catch (t: Throwable) {
                        Log.e(TAG, "reader set dir could not be handed to the core", t)
                    }
                } else {
                    Log.e(TAG, "reader set $setName could not be staged; the game runs without its reader")
                }
            }
            if (!MesenAccessibilityScript.startCore()) {
                val why = MesenAccessibilityScript.lastError()
                teardownNative()
                return if (why.isNotEmpty()) {
                    "That game could not be started. $why"
                } else {
                    "That game could not be started."
                }
            }
            coreGeneration = gen
            return null
        }
    }

    /**
     * Stage a writable copy of the bundled reader set [setName] under
     * filesDir/nes-readers and return its directory, or null on
     * failure.
     *
     * ⛔ THE COPY IS THE POINT — iOS's ReaderStore note, verbatim in
     * spirit: the bundled assets are READ-ONLY and must never be
     * handed to the core, because the readers WRITE into their own
     * tree (speech text, settings, waypoint and visited-room
     * progress). The stage overwrites the shipped files on every
     * start — the app always runs its own code — while files the app
     * did not ship, the reader's own progress and settings, survive.
     */
    private fun stageReaderSet(context: Context, setName: String): File? {
        val dest = File(File(context.filesDir, "nes-readers"), setName)
        return try {
            copyAssetTree(context, "nes-lua/$setName", dest)
            dest
        } catch (t: Throwable) {
            Log.e(TAG, "reader set $setName could not be staged", t)
            null
        }
    }

    private fun copyAssetTree(context: Context, assetPath: String, dest: File) {
        val children = context.assets.list(assetPath) ?: emptyArray()
        if (children.isEmpty()) {
            // A leaf: copy the file itself.
            dest.parentFile?.mkdirs()
            context.assets.open(assetPath).use { input ->
                dest.outputStream().use { output -> input.copyTo(output) }
            }
            return
        }
        dest.mkdirs()
        for (child in children) {
            copyAssetTree(context, "$assetPath/$child", File(dest, child))
        }
    }

    /**
     * Stop the session: audio first (joined, so nothing is still
     * draining), then the core. Safe with nothing running — including
     * on builds with no Mesen core, where the native stop's absence is
     * swallowed by [teardownNative]. MainActivity.onDestroy calls this
     * unconditionally, next to the other sessions' stops.
     */
    fun stop() {
        generation++
        running = false
        heldButtons.clear()
        pendingReleases.clear()
        selectedAdapterId = ""
        selectedAdapterName = ""
        loadedBackendId = ""
        adapterReadyNow = false
        // Silence the cue as well as the game: a tone left running
        // after the session ends would outlive the game it described.
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
            MesenAccessibilityScript.setSpeechBridge(null)
        } catch (t: Throwable) {
            // No Mesen surface in this build; nothing to clear.
        }
        try {
            MesenAccessibilityScript.stopCore()
        } catch (t: Throwable) {
            Log.w(TAG, "Mesen stop threw: ${t.message}")
        }
    }

    /** One emulated frame. Driven from the Compose frame clock, see [rememberMesenFrameClock]. */
    fun runFrame() {
        if (!running) return
        val ok = try {
            MesenAccessibilityScript.runFrame()
        } catch (t: Throwable) {
            false
        }
        if (!ok) {
            // The core stopped answering mid-session: end it the way
            // Quit would, rather than leaving a "running" screen over
            // a dead core.
            val why = try {
                MesenAccessibilityScript.lastError()
            } catch (t: Throwable) {
                ""
            }
            Log.e(TAG, "Mesen frame failed, ending the session: $why")
            stop()
            return
        }
        // The adapter's readiness is polled, not pushed: the native
        // host attaches lazily inside this call (its memory anchors
        // only exist once the game has booted), and the launcher's
        // reader buttons and pad echoes key off the answer.
        adapterReadyNow = try {
            MesenAccessibilityScript.adapterReady()
        } catch (t: Throwable) {
            false
        }
        // The Game sound gate, resolved per frame like the PSP
        // session's: a flip in Settings lands on the next frame.
        appContext?.let { gameSoundOn = GameSoundSettings.gameSoundEnabled(it) }
        // A frame has now sampled whatever is held: count the tap
        // releases against it (see the frame-counted releases note).
        tickPendingReleases()
        pollCues()
        val completed = try {
            MesenAccessibilityScript.framesCompleted
        } catch (t: Throwable) {
            0L
        }
        if (completed >= lastHeartbeatFrame + 600L) {
            lastHeartbeatFrame = completed
            Log.i(TAG, "Mesen running: frames=$completed audioFrames=$audioFramesWritten")
        }
    }

    private var lastHeartbeatFrame = 0L

    /**
     * One frame of battle cues: the host's single shared snapshot,
     * driven into [PspCueSynth] exactly the way the other sessions'
     * polls drive it — the field answer wins over the lock answer, the
     * one-shots are voiced either way, and everything is gated by the
     * game's effective battle-cues value. No NES or SNES reader raises
     * these cues today, so the expected answer in play is null and the
     * poll simply keeps the synth silent; it is wired anyway so a
     * future Mesen-console adapter's cues land on the synth that
     * already mixes into this session's drain. A null poll silences
     * the beacon: the cue must never outlive the state that fed it.
     * Once per frame, here and nowhere else — the poll consumes the
     * adapter's one-frame events.
     */
    private fun pollCues() {
        val poll = try {
            MesenAccessibilityScript.cuePoll()
        } catch (t: Throwable) {
            null
        }
        if (poll == null) {
            PspCueSynth.silenceAll()
            return
        }
        val ctx = appContext
        val on = ctx != null && GameSettings.effectiveBattleCues(ctx, selectedAdapterId)
        if (poll.field) {
            PspCueSynth.silenceLockBeacon()
            PspCueSynth.updateFieldBeacon(
                poll.kind, poll.bearing, poll.fieldDist, poll.headingLive, on,
            )
        } else {
            PspCueSynth.silenceFieldBeacon()
            PspCueSynth.updateBeacon(poll.state, poll.dist, on)
        }
        PspCueSynth.updateOneShots(
            poll.healthBits, poll.foeHz, poll.foePan, poll.ownHz, poll.ownPan, on,
        )
        PspCueSynth.updateGuard(
            poll.guardActive, poll.guardFrac, poll.guardBeat, on,
        )
    }

    // ---- the emulated pad ---------------------------------------------------
    //
    // ⛔ PER-BUTTON, NOT A MASK — the shared shape, for the shared
    // reason: poke_set_button latches one button per call. Press and
    // release forward as they happen, and the held set is remembered
    // only so stop() leaves nothing stuck.

    private val heldButtons = mutableSetOf<Int>()

    fun pressButton(button: Int) {
        if (!running) return
        heldButtons.add(button)
        MesenAccessibilityScript.setButton(button, true)
        forwardPadEcho(button)
    }

    fun releaseButton(button: Int) {
        heldButtons.remove(button)
        if (running) MesenAccessibilityScript.setButton(button, false)
    }

    // ---- frame-counted tap releases ------------------------------------
    //
    // ⛔ RELEASES ARE COUNTED IN FRAMES, NOT MILLISECONDS — the DS
    // session's note, for the DS session's reason: a press exists for
    // the game only if a frame SAMPLES it, and a wall-clock release
    // can fire before any frame has run, losing the tap silently.
    // Pad buttons have the stronger native form (poke_tap_button, see
    // [tapButton]), so this counting serves only the release-after
    // helper below. Touched only from the main thread.
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
            MesenAccessibilityScript.tapButton(button, 2)
        } catch (t: Throwable) {
            Log.w(TAG, "Mesen tap failed: ${t.message}")
        }
        forwardPadEcho(button)
    }

    /** Release a held button once two frames have sampled it. */
    fun releaseButtonSoon(button: Int) {
        releaseAfterFrames(2) { releaseButton(button) }
    }

    // ---- savestates ---------------------------------------------------------
    //
    // Whole-core states (poke_save_state's contract, the same one iOS
    // exposes): one slot per game, under filesDir — states must survive
    // cache eviction, like the saves do.
    //
    // ⛔ SNES ONLY, BY THE CORE'S OWN REFUSAL. The SNES backend
    // implements states (Mesen's SaveStateManager); the NES backend
    // refuses out loud ("NES save states are not implemented yet") —
    // so the launcher offers the state group only when [backendId] is
    // "snes", and these functions stay honest pass-throughs either way.

    private fun stateFile(context: Context): File {
        val dir = File(context.filesDir, "mesen-states")
        if (!dir.isDirectory) dir.mkdirs()
        val name = if (gameCode.isNotEmpty()) {
            gameCode.replace(Regex("[^A-Za-z0-9._-]+"), "_")
        } else {
            "mesen"
        }
        return File(dir, "$name.state")
    }

    fun saveState(context: Context): Boolean {
        if (!running) return false
        val ok = try {
            MesenAccessibilityScript.saveState(stateFile(context).absolutePath)
        } catch (t: Throwable) {
            false
        }
        if (!ok) Log.w(TAG, "Mesen save state failed")
        return ok
    }

    fun loadState(context: Context): Boolean {
        if (!running) return false
        val file = stateFile(context)
        if (!file.isFile) return false
        return try {
            MesenAccessibilityScript.loadState(file.absolutePath)
        } catch (t: Throwable) {
            false
        }
    }

    // ---- the reader ---------------------------------------------------------
    //
    // The command surface the launcher's Mesen screen drives, the
    // shared split: sendAdapterCommand returns the native verdict and
    // the SCREEN speaks a refusal, while the pad echoes below are
    // forwarded silently — a swallowed refusal there would make
    // directional presses repeat "not ready" forever.

    /** The selected adapter's registry id, or "" when the game has no reader. */
    val adapterId: String get() = selectedAdapterId

    /** The selected adapter's display name, or "" when the game has no reader. */
    val adapterName: String get() = selectedAdapterName

    /** The adapter's latest readiness answer (see [runFrame]'s poll). */
    val isAdapterReady: Boolean get() = adapterReadyNow

    /** The backend the host resolved for the loaded game: "nes", "snes", or "". */
    val backendId: String get() = loadedBackendId

    /**
     * The running game's current frame for the stage-2 game picture —
     * (pixels, width, height) as RGBA byte-order words — or null when
     * no frame is available. Decorative; a blind player never depends
     * on it.
     */
    fun currentFrame(): Triple<IntArray, Int, Int>? =
        MesenAccessibilityScript.currentFrame()

    /**
     * Send one reader command (a [MesenAdapterCommands] value). True
     * when the adapter took it; false is the native refusal (no
     * adapter, out of range, not ready), and speaking it is the
     * caller's job.
     */
    fun sendAdapterCommand(command: Int): Boolean {
        if (!running) return false
        return try {
            MesenAccessibilityScript.adapterCommand(command)
        } catch (t: Throwable) {
            false
        }
    }

    /**
     * Stop speech: the platform voice AND the host's queue, the two
     * halves iOS's GameSession.stopSpeech performs (its platform stop
     * plus the StopSpeech command the core intercepts).
     */
    fun stopSpeech(context: Context) {
        try {
            AccessibilityScript.speechBridge(context.applicationContext).stop()
        } catch (t: Throwable) {
            // No bridge to stop; the command below is a no-op then too.
        }
        try {
            MesenAccessibilityScript.adapterCommand(MesenAdapterCommands.STOP_SPEECH)
        } catch (t: Throwable) {
            // No Mesen surface in this build; nothing queued to stop.
        }
    }

    /**
     * Forward one pad press to the adapter as its echo command — the
     * adapter.h echo contract, mapped BY POSITION the way the other
     * sessions map it: the bottom face button (B) types on the
     * on-screen keyboard, the right one (A) erases, the left one (Y)
     * spaces, Select shifts, Start finishes, and the D-pad steps menus
     * and value lists whether or not the game is showing one (each
     * adapter decides). Only while the adapter stands ready, and only
     * on the press.
     */
    private fun forwardPadEcho(button: Int) {
        val command = when (button) {
            MesenKeys.B -> MesenAdapterCommands.OSK_TYPE
            MesenKeys.A -> MesenAdapterCommands.OSK_DELETE
            MesenKeys.Y -> MesenAdapterCommands.OSK_SPACE
            MesenKeys.SELECT -> MesenAdapterCommands.OSK_SHIFT
            MesenKeys.START -> MesenAdapterCommands.OSK_FINISH
            MesenKeys.UP -> MesenAdapterCommands.MENU_PREV
            MesenKeys.DOWN -> MesenAdapterCommands.MENU_NEXT
            MesenKeys.LEFT -> MesenAdapterCommands.MENU_LEFT
            MesenKeys.RIGHT -> MesenAdapterCommands.MENU_RIGHT
            else -> return
        }
        if (!adapterReadyNow) return
        try {
            MesenAccessibilityScript.adapterCommand(command)
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

    @Volatile
    private var gameSoundOn = true

    private fun startAudio() {
        val minBuffer = AudioTrack.getMinBufferSize(
            AUDIO_RATE,
            AudioFormat.CHANNEL_OUT_STEREO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        if (minBuffer <= 0) {
            Log.w(TAG, "Mesen audio unavailable: min buffer size $minBuffer")
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
            Log.w(TAG, "Mesen audio unavailable: ${t.message}")
            return
        }
        audioTrack = track
        audioFramesWritten = 0L
        audioRunning = true
        track.play()
        val thread = Thread { audioLoop(track) }
        thread.name = "mesen-audio"
        audioThread = thread
        thread.start()
        Log.i(TAG, "Mesen audio started: AudioTrack at $AUDIO_RATE Hz, stereo")
    }

    private fun audioLoop(track: AudioTrack) {
        val buffer = ShortArray(2048 * 2)
        while (audioRunning) {
            val frames = try {
                MesenAccessibilityScript.readAudio(buffer, 2048)
            } catch (t: Throwable) {
                0
            }
            if (frames > 0) {
                // Game sound off: the game's samples are silenced
                // BEFORE the cue mix, never instead of draining — the
                // PSP session's rule, at the one place they meet.
                if (!gameSoundOn) {
                    java.util.Arrays.fill(buffer, 0, frames * 2, 0)
                }
                // The cue synth mixes into the game's own audio here,
                // at the drain — the one output path, so cue and game
                // can never drift apart (see PspCueSynth). Silent
                // unless a frame poll armed a voice.
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

    /** True once a NES or SNES game is running, so the pad and commands can appear. */
    val isRunning: Boolean get() = running

    /** The loaded game's code, or "" before a load lands. */
    val loadedGameCode: String get() = gameCode
}

/**
 * The NES/SNES button ids, in the core's own order (pokecore.h's
 * POKE_BTN_*).
 *
 * ⛔ THESE ARE THE CORE'S VALUES, NOT A UI CONVENTION — the same warning
 * as DsKeys, for the same reason: poke_set_button indexes the shared
 * pad table, so a wrong id is a wrong button with no error anywhere.
 * pokecore translates these shared indices to each console's own
 * button order (SNES_BTN_* / NES_BTN_* in the glue); the NES simply
 * drops the values it does not have (X, Y, L, R). The launcher
 * contract test checks this object against pokecore.h's POKE_BTN_*
 * defines.
 */
object MesenKeys {
    const val A = 0
    const val B = 1
    const val SELECT = 2
    const val START = 3
    const val RIGHT = 4
    const val LEFT = 5
    const val UP = 6
    const val DOWN = 7
    const val R = 8
    const val L = 9
    const val X = 10
    const val Y = 11
}

/**
 * The oga::Command values the Mesen screen and pad can send, as raw
 * ints.
 *
 * ⛔ THESE ARE THE C ABI'S VALUES, NOT A UI CONVENTION — adapter.h's
 * Command enum is append-only because the integer IS the
 * cross-language contract. Only the commands in some Mesen-console
 * adapter's iOS-supported set (A Link to the Past's), the pad echoes,
 * and the core's own speech commands are mirrored here; the launcher
 * contract test checks every value below against Core/adapter.h
 * itself.
 */
object MesenAdapterCommands {
    const val WHERE_AM_I = 0
    const val DUMP_STATE = 6
    const val MENU_STATE = 7
    const val MENU_NEXT = 8
    const val MENU_PREV = 9
    const val MENU_LEFT = 10
    const val MENU_RIGHT = 11
    const val OSK_TOGGLE = 31
    const val OSK_TYPE = 32
    const val OSK_DELETE = 33
    const val OSK_SPACE = 34
    const val OSK_SHIFT = 35
    const val OSK_FINISH = 36
    const val STOP_SPEECH = 37
    const val REPEAT_NEWEST = 38
    const val REPEAT_OLDER = 39
    const val STATUS = 40
    const val GUIDE = 41
    const val OBJECTIVE = 42
}

/**
 * Drives the Mesen session's frames while a NES or SNES game runs —
 * PACED TO THE CONSOLE, NOT TO THE DISPLAY.
 *
 * ⛔ ONE EMULATED FRAME PER DISPLAY FRAME RUNS THE GAME FAST on any
 * screen above 60 Hz (90/120 Hz phones are the common case): both
 * consoles render ~60.0988 frames a second in NTSC, so an unpaced
 * clock would run the game at 1.5-2x speed. The clock still wakes on
 * the display's frame signal (the core's per-frame work belongs on
 * the thread that produced it, the other sessions' reasoning), but a
 * frame only RUNS when its slot has arrived; a stretch that falls
 * behind resyncs to the next slot instead of bursting to catch up.
 *
 * Kept beside its own session, like rememberDsFrameClock beside its.
 */
@Composable
fun rememberMesenFrameClock(running: Boolean) {
    if (!running) return
    androidx.compose.runtime.LaunchedEffect(Unit) {
        // The NTSC frame interval both consoles share:
        // 1 s / 60.0988 Hz ≈ 16.639 ms.
        val frameNs = (1_000_000_000.0 / 60.0988).toLong()
        var nextFrameAt = 0L
        while (true) {
            val now = androidx.compose.runtime.withFrameNanos { it }
            if (nextFrameAt == 0L) nextFrameAt = now
            if (now >= nextFrameAt) {
                MesenGameSession.runFrame()
                nextFrameAt += frameNs
                if (nextFrameAt <= now) nextFrameAt = now + frameNs
            }
        }
    }
}
