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
import me.magnum.melonds.accessibility.DsAccessibilityScript
import me.magnum.melonds.accessibility.DsRomResolver
import java.io.File
import java.io.InputStreamReader
import java.nio.charset.StandardCharsets

/**
 * The Nintendo DS session, driven directly from Compose.
 *
 * ⛔ A SIBLING OF [PspGameSession], NOT A VARIANT. Same shape — an object
 * the screen calls, a running flag, a last error, held input, a Compose
 * frame clock, an audio drain thread, an asynchronous start — over a
 * different core: the shared Core DS host (Core/pokecore.cpp, the same
 * translation unit the iOS app ships) through the poke_* ABI, where PSP
 * drives PPSSPP through psp_*. The GB and PSP sessions' behavior is
 * untouched by this file's existence; the launcher keeps the three
 * apart by the picked file's extension.
 *
 * ⛔ THE READER IS THE HOST'S OWN, DRIVEN FROM HERE. Unlike PSP — whose
 * adapters attach in a separate host TU — the DS host carries its
 * readers inside itself: the native adapters (Fire Emblem: Shadow
 * Dragon, Dragon Ball Z: Attack of the Saiyans, Dragon Quest IX)
 * attach through the shared Core registry when pokecore selects by
 * cartridge code, and the Pokémon Black/White reader runs in
 * pokecore's embedded Lua. This session's part is the lifecycle around
 * them: the shared speech bridge is registered with the host when a
 * session lands, the reader script is handed over before the load (see
 * [loadOnWorker]), readiness is polled once a frame in [runFrame], pad
 * presses are forwarded as adapter echoes (see [pressButton]), and the
 * reader buttons on the launcher screen call [sendAdapterCommand]. A
 * game with no adapter simply never answers ready and shows no reader
 * buttons. The launcher's own lines during a DS session ride the
 * shared speech bridge, built and view-wired at start exactly as the
 * other sessions do (see [attachAnnouncementView]).
 *
 * ⛔ START IS ASYNCHRONOUS, LIKE PSP'S, AND FOR THE SAME REASON IN
 * MINIATURE. A DS ROM is up to 512 MB: the resolver may have to copy
 * all of it (see DsRomResolver), and the load then parses it. Doing
 * that on the button's thread would freeze the launcher — and TalkBack
 * with it — for the whole copy, so the load runs on a worker thread
 * and the result arrives through the caller's callback on the main
 * thread. The generation counter is the PSP session's: a stop that
 * lands mid-load tears the landed core down instead of resurrecting a
 * session nobody is waiting for.
 *
 * ⛔ AUDIO IS THE SESSION'S OWN THREAD, PSP'S SHAPE EXACTLY. pokecore
 * mixes at 32768 Hz (the rate its ABI documents and iOS drains); this
 * session drains it into an AudioTrack from a thread that lives
 * exactly as long as the session — started when a load lands, stopped
 * and joined in [stop], before the core is destroyed. The battle cues
 * (PspCueSynth, shared with the PSP session — only one session ever
 * runs at a time) mix in at this drain, the one output path, so cue
 * and game can never drift apart. There is no Android game-sound
 * setting to gate the drain behind yet (SettingsScreen's own header
 * lists it as missing), so it runs whenever a DS game does; the native
 * side's own audio gate (poke_set_audio_enabled) stands ready for the
 * stage that adds the switch.
 */
object DsGameSession {

    private const val TAG = "DsSession"

    /** The rate the core hands audio out at (pokecore.h's contract). */
    private const val AUDIO_RATE = 32768

    /**
     * The application context, kept from [start] so the per-frame cue
     * poll can read the battle-cues switch (see [pollCues]).
     */
    @Volatile
    private var appContext: Context? = null

    /** True once a cartridge is loaded and the core is running. */
    @Volatile
    private var running = false

    /** The last failure, for the screen to speak instead of guessing. */
    @Volatile
    private var lastError: String = ""

    /** Bumped by every start and every stop; a stale load must not land. */
    @Volatile
    private var generation = 0

    /** The loaded game's cartridge code (e.g. YDQE), once a load lands. */
    @Volatile
    private var gameCode: String = ""

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

    fun romExtensions(): Set<String> = DsAccessibilityScript.ROM_EXTENSIONS

    fun handlesExtension(extension: String?): Boolean =
        DsAccessibilityScript.handlesExtension(extension)

    /**
     * True when this build can actually run DS games.
     *
     * ⛔ ASK THE CORE, NOT THE PACKAGE — the PSP probe's reasoning: the
     * cores SHARE the frontend .so, so its presence says nothing about
     * DS; the question is whether the DS surface was compiled into it,
     * and the only honest witness is the JNI probe that exists exactly
     * when it was (see DsAccessibilityScript). On armeabi-v7a the call
     * throws UnsatisfiedLinkError; that throw is the "no", caught here.
     */
    fun coreAvailable(context: Context): Boolean {
        return try {
            DsAccessibilityScript.isCoreLinked()
        } catch (e: UnsatisfiedLinkError) {
            // The armeabi-v7a answer: the surface is not in the .so.
            Log.w(TAG, "DS path unavailable: ${e.message}")
            false
        } catch (t: Throwable) {
            Log.w(TAG, "DS path unavailable: ${t.message}")
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
     * Load a DS cartridge image and start the core. [onResult] is
     * called on the main thread with null on success, or a
     * player-facing message on failure. See the class doc for why this
     * is asynchronous.
     */
    fun start(activity: Activity, uri: Uri, onResult: (String?) -> Unit) {
        val gen = ++generation
        val app = activity.applicationContext
        appContext = app
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
                // The bridge exists now, so the stored view can
                // actually be attached — the re-forward described in
                // attachAnnouncementView. The same bridge is
                // registered with the DS host: adapter and script
                // speech lands on this one instance, like every other
                // system's speech in the app.
                val bridge = AccessibilityScript.speechBridge(app)
                try {
                    DsAccessibilityScript.setSpeechBridge(bridge)
                } catch (t: Throwable) {
                    Log.w(TAG, "DS speech bridge registration threw: ${t.message}")
                }
                selectedAdapterId = try {
                    DsAccessibilityScript.adapterId()
                } catch (t: Throwable) {
                    ""
                }
                selectedAdapterName = try {
                    DsAccessibilityScript.adapterName()
                } catch (t: Throwable) {
                    ""
                }
                storedAnnouncementView?.let { AccessibilityScript.setAnnouncementView(it) }
                startAudio()
                Log.i(TAG, "DS session running: $gameCode")
                onResult(null)
            }
        }
        worker.name = "ds-load"
        worker.start()
    }

    /** The worker-thread half of [start]: resolve, set script, load, start. */
    private fun loadOnWorker(context: Context, uri: Uri): String? {
        val romPath = DsRomResolver.resolve(context, uri)
            ?: return "Could not read that game file."
        val savePath = DsRomResolver.savePathFor(context, romPath)
            ?: return "The save file for that game could not be prepared."
        // The reader script goes in BEFORE the load: pokecore's
        // contract is set-once-before-load (poke_set_script), the order
        // iOS's GameSession uses at init. It is set on EVERY start, so
        // a stale script can never leak into a later game. A failed
        // asset read is not fatal to the load — the native-adapter
        // games never touch the script — but it is logged loudly:
        // Pokémon without its reader is a silent game, and in the
        // shipped APK the workflow's gates verify these assets exist.
        try {
            DsAccessibilityScript.setScript(readScriptText(context))
        } catch (t: Throwable) {
            Log.e(TAG, "DS script could not be handed to the core", t)
            return "The DS core is not in this build."
        }
        val loaded = try {
            DsAccessibilityScript.loadRom(romPath, savePath)
        } catch (t: Throwable) {
            Log.e(TAG, "DS load threw", t)
            return "The DS core is not in this build."
        }
        if (!loaded) {
            val why = DsAccessibilityScript.lastError()
            return if (why.isNotEmpty()) {
                "That DS game could not be loaded. $why"
            } else {
                "That DS game could not be loaded."
            }
        }
        gameCode = DsAccessibilityScript.gameCode()
        if (!DsAccessibilityScript.startCore()) {
            val why = DsAccessibilityScript.lastError()
            teardownNative()
            return if (why.isNotEmpty()) {
                "That DS game could not be started. $why"
            } else {
                "That DS game could not be started."
            }
        }
        return null
    }

    /**
     * The Pokémon reader script: the BizHawk compat shim and main.lua,
     * concatenated — the exact text pokecore's embedded Lua runs, read
     * as UTF-8 the way AccessibilityScript reads it (the script's many
     * literal strings — accented Pokémon names, arrows — corrupt under
     * any other charset). "" when the assets cannot be read.
     */
    private fun readScriptText(context: Context): String {
        val shim = readAsset(context, "lua/bizhawk_compat.lua") ?: return ""
        val main = readAsset(context, "lua/main.lua") ?: return ""
        return StringBuilder(shim.length + main.length + 8)
            .append(shim)
            .append('\n')
            .append(main)
            .toString()
    }

    private fun readAsset(context: Context, name: String): String? {
        return try {
            context.assets.open(name).use { stream ->
                InputStreamReader(stream, StandardCharsets.UTF_8).use { it.readText() }
            }
        } catch (e: Exception) {
            Log.e(TAG, "[ds] could not read asset $name", e)
            null
        }
    }

    /**
     * Stop the session: audio first (joined, so nothing is still
     * draining), then the core. Safe with nothing running — including
     * on builds with no DS core, where the native stop's absence is
     * swallowed by [teardownNative]. MainActivity.onDestroy calls this
     * unconditionally, next to the other sessions' stops.
     */
    fun stop() {
        generation++
        running = false
        heldButtons.clear()
        selectedAdapterId = ""
        selectedAdapterName = ""
        adapterReadyNow = false
        // Silence the cue as well as the game: a tone left running
        // after the session ends would outlive the game it described.
        PspCueSynth.silenceAll()
        stopAudio()
        teardownNative()
    }

    private fun teardownNative() {
        try {
            DsAccessibilityScript.setSpeechBridge(null)
        } catch (t: Throwable) {
            // No DS surface in this build; nothing to clear.
        }
        try {
            DsAccessibilityScript.stopCore()
        } catch (t: Throwable) {
            Log.w(TAG, "DS stop threw: ${t.message}")
        }
    }

    /** One emulated frame. Driven from the Compose frame clock, see [rememberDsFrameClock]. */
    fun runFrame() {
        if (!running) return
        val ok = try {
            DsAccessibilityScript.runFrame()
        } catch (t: Throwable) {
            false
        }
        if (!ok) {
            // The core stopped answering mid-session: end it the way
            // Quit would, rather than leaving a "running" screen over
            // a dead core.
            val why = try {
                DsAccessibilityScript.lastError()
            } catch (t: Throwable) {
                ""
            }
            Log.e(TAG, "DS frame failed, ending the session: $why")
            stop()
            return
        }
        // The adapter's readiness is polled, not pushed: the native
        // host attaches lazily inside this call (its memory anchors
        // only exist once the game has booted), and the launcher's
        // reader buttons and pad echoes key off the answer.
        adapterReadyNow = try {
            DsAccessibilityScript.adapterReady()
        } catch (t: Throwable) {
            false
        }
        pollCues()
        val completed = try {
            DsAccessibilityScript.framesCompleted
        } catch (t: Throwable) {
            0L
        }
        if (completed >= lastHeartbeatFrame + 600L) {
            lastHeartbeatFrame = completed
            Log.i(TAG, "DS running: frames=$completed audioFrames=$audioFramesWritten")
        }
    }

    private var lastHeartbeatFrame = 0L

    /**
     * One frame of battle cues: the host's single shared snapshot,
     * driven into [PspCueSynth] exactly the way the PSP session's poll
     * drives it — the field answer wins over the lock answer, the
     * one-shots are voiced either way, and everything is gated by the
     * game's effective battle-cues value. The DS reader this serves
     * first is Attack of the Saiyans (its beacon, its health beeps,
     * its Active Guard window ride poke_cue_* exactly as Dissidia's
     * ride the PSP host's snapshot). A null poll (no adapter selected)
     * silences the beacon: the cue must never outlive the state that
     * fed it. Once per frame, here and nowhere else — the poll
     * consumes the adapter's one-frame events.
     */
    private fun pollCues() {
        val poll = try {
            DsAccessibilityScript.cuePoll()
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
    // ⛔ PER-BUTTON, NOT A MASK — the PSP shape, for the PSP reason:
    // poke_set_button latches one button per call. Press and release
    // forward as they happen, and the held set is remembered only so
    // stop() leaves nothing stuck.

    private val heldButtons = mutableSetOf<Int>()

    fun pressButton(button: Int) {
        if (!running) return
        heldButtons.add(button)
        DsAccessibilityScript.setButton(button, true)
        forwardPadEcho(button)
    }

    fun releaseButton(button: Int) {
        heldButtons.remove(button)
        if (running) DsAccessibilityScript.setButton(button, false)
    }

    /** Release a held button a moment later, for the screen-reader click path. */
    fun releaseButtonSoon(button: Int) {
        Handler(Looper.getMainLooper()).postDelayed(
            { releaseButton(button) },
            120L,
        )
    }

    // ---- the reader script's hotkeys ---------------------------------------
    //
    // The Pokémon script listens for LETTERS, not buttons (its BizHawk
    // heritage): J/K/L/I/O read lists, P finds a path, C answers Where
    // Am I, E shows tiles, R stops the script's speech, U repeats. The
    // launcher's script groups tap them — press and release a beat
    // later, because the script edge-detects the down stroke (the GB
    // session's tapHotkey, same contract).

    fun pressHotkey(key: Char) {
        if (!running) return
        DsAccessibilityScript.setHotkey(key.uppercaseChar(), true)
    }

    fun releaseHotkey(key: Char) {
        if (running) DsAccessibilityScript.setHotkey(key.uppercaseChar(), false)
    }

    fun tapHotkey(key: Char) {
        pressHotkey(key)
        Handler(Looper.getMainLooper()).postDelayed(
            { releaseHotkey(key) },
            80L,
        )
    }

    // ---- the touch screen ----------------------------------------------------
    //
    // ⛔ COARSE TAPS ONLY, HONESTLY. The DS bottom screen is a touch
    // surface and some reader games need touches to advance (the
    // Pokémon script itself taps through the core when a menu asks).
    // What the session screen offers is a 3x3 grid of tap targets in
    // the screen's own coordinates — enough for "tap the middle", "tap
    // the lower right" — NOT a drawn picture to explore by finger:
    // there is no DS picture surface in the Android UI at all in this
    // stage (the parity audit's Stage 2 owns pictures), so pretending
    // to pixel accuracy would be a lie told to a blind player. A tap is
    // a press and a release a beat later, the pad's click shape.

    fun tapTouch(x: Int, y: Int) {
        if (!running) return
        DsAccessibilityScript.setTouch(x, y, true)
        Handler(Looper.getMainLooper()).postDelayed(
            {
                if (running) DsAccessibilityScript.setTouch(x, y, false)
            },
            120L,
        )
    }

    // ---- savestates ---------------------------------------------------------
    //
    // Whole-core states (poke_save_state's contract, the same one iOS
    // exposes): one slot per game, named by cartridge code, under
    // filesDir — states must survive cache eviction, like the saves do.

    private fun stateFile(context: Context): File {
        val dir = File(context.filesDir, "ds-states")
        if (!dir.isDirectory) dir.mkdirs()
        val name = if (gameCode.isNotEmpty()) gameCode else "ds"
        return File(dir, "$name.state")
    }

    fun saveState(context: Context): Boolean {
        if (!running) return false
        val ok = try {
            DsAccessibilityScript.saveState(stateFile(context).absolutePath)
        } catch (t: Throwable) {
            false
        }
        if (!ok) Log.w(TAG, "DS save state failed")
        return ok
    }

    fun loadState(context: Context): Boolean {
        if (!running) return false
        val file = stateFile(context)
        if (!file.isFile) return false
        return try {
            DsAccessibilityScript.loadState(file.absolutePath)
        } catch (t: Throwable) {
            false
        }
    }

    // ---- the reader ---------------------------------------------------------
    //
    // The command surface the launcher's DS screen drives. It mirrors
    // the PSP session's split: sendAdapterCommand returns the native
    // verdict and the SCREEN speaks a refusal, while the pad echoes
    // below are forwarded silently — a swallowed refusal there would
    // make directional presses repeat "not ready" forever.

    /** The selected adapter's registry id, or "" when the game has no reader. */
    val adapterId: String get() = selectedAdapterId

    /** The selected adapter's display name, or "" when the game has no reader. */
    val adapterName: String get() = selectedAdapterName

    /** The adapter's latest readiness answer (see [runFrame]'s poll). */
    val isAdapterReady: Boolean get() = adapterReadyNow

    /**
     * True for Pokémon Black/White — the games the Lua reader script
     * serves (cartridge codes IRAO/IRBO, the check iOS's GameSession
     * makes). The launcher keys its script hotkey groups on this: they
     * are the script's keys, meaningless to a native-adapter game.
     */
    val isPokemonGame: Boolean get() = gameCode == "IRAO" || gameCode == "IRBO"

    /**
     * Send one reader command (a [DsAdapterCommands] value). True when
     * the adapter took it; false is the native refusal (no adapter, out
     * of range, not ready), and speaking it is the caller's job.
     */
    fun sendAdapterCommand(command: Int): Boolean {
        if (!running) return false
        return try {
            DsAccessibilityScript.adapterCommand(command)
        } catch (t: Throwable) {
            false
        }
    }

    /**
     * Stop speech: the platform voice AND the host's queue, the two
     * halves iOS's GameSession.stopSpeech performs (its platform stop
     * plus the StopSpeech command the core intercepts — for a script
     * game the core routes it into the script's own stop, so this one
     * call serves both DS reader kinds).
     */
    fun stopSpeech(context: Context) {
        try {
            AccessibilityScript.speechBridge(context.applicationContext).stop()
        } catch (t: Throwable) {
            // No bridge to stop; the command below is a no-op then too.
        }
        try {
            DsAccessibilityScript.adapterCommand(DsAdapterCommands.STOP_SPEECH)
        } catch (t: Throwable) {
            // No DS surface in this build; nothing queued to stop.
        }
    }

    /**
     * Forward one pad press to the adapter as its echo command — the
     * adapter.h echo contract, mapped BY POSITION the way iOS's
     * controller layer maps the DS pad: the bottom face button (B)
     * types on the on-screen keyboard, the right one (A) erases, the
     * left one (Y) spaces, Select shifts, Start finishes, and the
     * D-pad steps menus and value lists whether or not the game is
     * showing one (each adapter decides). Only while the adapter
     * stands ready, and only on the press.
     */
    private fun forwardPadEcho(button: Int) {
        val command = when (button) {
            DsKeys.B -> DsAdapterCommands.OSK_TYPE
            DsKeys.A -> DsAdapterCommands.OSK_DELETE
            DsKeys.Y -> DsAdapterCommands.OSK_SPACE
            DsKeys.SELECT -> DsAdapterCommands.OSK_SHIFT
            DsKeys.START -> DsAdapterCommands.OSK_FINISH
            DsKeys.UP -> DsAdapterCommands.MENU_PREV
            DsKeys.DOWN -> DsAdapterCommands.MENU_NEXT
            DsKeys.LEFT -> DsAdapterCommands.MENU_LEFT
            DsKeys.RIGHT -> DsAdapterCommands.MENU_RIGHT
            else -> return
        }
        if (!adapterReadyNow) return
        try {
            DsAccessibilityScript.adapterCommand(command)
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
            Log.w(TAG, "DS audio unavailable: min buffer size $minBuffer")
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
            Log.w(TAG, "DS audio unavailable: ${t.message}")
            return
        }
        audioTrack = track
        audioFramesWritten = 0L
        audioRunning = true
        track.play()
        val thread = Thread { audioLoop(track) }
        thread.name = "ds-audio"
        audioThread = thread
        thread.start()
        Log.i(TAG, "DS audio started: AudioTrack at $AUDIO_RATE Hz, stereo")
    }

    private fun audioLoop(track: AudioTrack) {
        val buffer = ShortArray(2048 * 2)
        while (audioRunning) {
            val frames = try {
                DsAccessibilityScript.readAudio(buffer, 2048)
            } catch (t: Throwable) {
                0
            }
            if (frames > 0) {
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

    /** True once a DS game is running, so the pad and commands can appear. */
    val isRunning: Boolean get() = running

    /** The loaded game's cartridge code, or "" before a load lands. */
    val loadedGameCode: String get() = gameCode
}

/**
 * The DS button ids, in the core's own order (pokecore.h's POKE_BTN_*).
 *
 * ⛔ THESE ARE THE CORE'S VALUES, NOT A UI CONVENTION — the same warning
 * as PspKeys, for the same reason: poke_set_button indexes the shared
 * pad table (melonDS's own key bit order), so a wrong id is a wrong
 * button with no error anywhere. The launcher contract test checks this
 * object against pokecore.h's POKE_BTN_* defines.
 */
object DsKeys {
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
 * The oga::Command values the DS screen and pad can send, as raw ints.
 *
 * ⛔ THESE ARE THE C ABI'S VALUES, NOT A UI CONVENTION — adapter.h's
 * Command enum is append-only because the integer IS the
 * cross-language contract. Only the commands in some DS adapter's
 * iOS-supported set, the pad echoes, and the core's own speech
 * commands are mirrored here; the launcher contract test checks every
 * value below against Core/adapter.h itself.
 */
object DsAdapterCommands {
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
    const val REPEAT_NEWEST = 38
    const val REPEAT_OLDER = 39
    const val STATUS = 40
    const val GUIDE = 41
    const val OBJECTIVE = 42
    const val ENEMIES = 43
}

/**
 * Drives one emulated DS frame per Compose frame while a DS game runs.
 *
 * The twin of rememberPspFrameClock, kept beside its own session for
 * the same reason that one is beside its: the cadence is the app's
 * frame signal, so the core's per-frame work happens on the same
 * thread that produced it.
 */
@Composable
fun rememberDsFrameClock(running: Boolean) {
    if (!running) return
    androidx.compose.runtime.LaunchedEffect(Unit) {
        while (true) {
            androidx.compose.runtime.withFrameNanos { }
            DsGameSession.runFrame()
        }
    }
}
