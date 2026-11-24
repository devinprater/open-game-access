package com.devin.opengameaccess

import android.app.Activity
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.net.Uri
import android.util.Log
import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalView
import me.magnum.melonds.accessibility.AccessibilityScript
import me.magnum.melonds.accessibility.GbAccessibilityScript
import me.magnum.melonds.accessibility.GbRomResolver

/**
 * The Game Boy session, driven directly from Compose.
 *
 * ⛔ THIS REPLACES `GbBridge`. That class existed only because the launcher was a
 * WebView: it was the `@JavascriptInterface` seam between `assets/index.html` and
 * the native path, and its whole job was turning JS calls into Kotlin calls. With
 * Compose there is no seam to bridge, so this is a plain object the screen calls.
 *
 * ⛔ AND IT FIXES A REAL BUG THE BRIDGE HAD. `AccessibilitySpeech` deliberately
 * routes announcements through the running screen reader's live region when one is
 * active — two voices on one output interrupt each other, so the screen reader
 * should be the only voice. That path needs `setAnnouncementView`, and the old
 * MainActivity NEVER CALLED IT. So `announcementView` stayed null, the code fell
 * through to its "no view yet" branch and spoke through TextToSpeech anyway — the
 * exact double-voice problem the guard exists to prevent. [attachAnnouncementView]
 * below is the missing call.
 *
 * ⛔ THE HOST IS THE SHARED CORE. GbAccessibilityScript drives GbSessionJNI.cpp,
 * which runs Core/gba_core.cpp under pokecore — the iOS app's Game Boy host,
 * the same translation unit — instead of the Android-only MGBARunner host the
 * app used to carry. This object's surface did not change with the move.
 *
 * ⛔ THE GAME'S OWN SOUND. mGBA fills its audio ring every frame; the shared
 * core drains it into its queue after every frame (gba_core.cpp gba_frame ->
 * Core/gb_audio_queue.h, resampled to 32768 Hz and bounded in latency),
 * and this session plays the queue through an AudioTrack from its own thread —
 * the DS and PSP sessions' shape exactly, at the same 32768 Hz. The thread lives
 * exactly as long as a game: started when one loads, stopped and joined in
 * [stop] before the core is torn down. The reader's cue sounds (audio.play)
 * keep their own path through AccessibilitySpeech and are not mixed here.
 *
 * ⛔ THE "GAME SOUND" SWITCH GATES IT, read once a frame like the PSP and
 * Genesis sessions do, and applied at the native queue only when it moves.
 * It gates the game's audio only — never speech, never the cue sounds.
 */
object GbGameSession {

    private const val TAG = "PokemonAccess"

    /** True once a ROM is loaded and the reader is running. */
    @Volatile
    private var running = false

    /** The last failure, for the screen to speak instead of guessing. */
    @Volatile
    private var lastError: String = ""

    fun romExtensions(): Set<String> = GbAccessibilityScript.ROM_EXTENSIONS

    /**
     * True when this build can actually run Game Boy ROMs.
     *
     * ⛔ ASK THE CORE, NOT THE PACKAGE — the DS/Mesen/Genesis probe's reasoning:
     * every core shares the frontend .so, so its presence says nothing about
     * the Game Boy; the question is whether the Game Boy surface was compiled
     * into it, and the only honest witness is the JNI probe that exists exactly
     * when it was (GbAccessibilityScript.isCoreLinked). On armeabi-v7a, where
     * the shared-Core glue is not built, the call throws UnsatisfiedLinkError;
     * that throw is the "no", caught here.
     */
    fun coreAvailable(context: Context): Boolean {
        return try {
            GbAccessibilityScript.isCoreLinked()
        } catch (e: UnsatisfiedLinkError) {
            // The armeabi-v7a answer: the surface is not in the .so.
            Log.w(TAG, "Game Boy path unavailable: ${e.message}")
            false
        } catch (t: Throwable) {
            Log.w(TAG, "Game Boy path unavailable: ${t.message}")
            false
        }
    }

    /**
     * Point announcements at the live-region view. The missing call described above.
     *
     * Must be called with a view that is attached, so Compose calls it from a
     * SideEffect once composition is live — the view is what the screen reader
     * announces from, and an unattached one announces nothing.
     */
    fun attachAnnouncementView(view: View) {
        storedAnnouncementView = view
        AccessibilityScript.setAnnouncementView(view)
    }

    /**
     * The view the screen reader announces from.
     *
     * ⛔ RE-FORWARDED ON START, AND THAT IS THE POINT. `AccessibilitySpeech` is built
     * when a ROM loads, AFTER the Activity's first composition, so the attach that
     * happens during composition forwards to a bridge that does not exist yet and is
     * silently a no-op. Without this re-forward the view stays null for the whole
     * session, the speech code falls through to its "no view yet" branch, and the app
     * speaks through TextToSpeech on top of the screen reader -- the exact
     * double-voice bug this port was meant to fix, merely moved.
     */
    @Volatile
    private var storedAnnouncementView: View? = null

    /**
     * Load a Game Boy ROM and start the reader. Returns null on success, or a
     * player-facing message on failure.
     *
     * ⛔ THE URI IS NOT A PATH. mGBA opens ROMs through the platform VFS and its
     * reader walks GB banks past the 16 KiB window, so it needs a real file —
     * [GbRomResolver] materialises the SAF URI into the cache and keys the copy on
     * the document's name and length, which is what tells "the player reopened this
     * ROM" from "the player swapped in a patched hack of the same name".
     */
    fun start(activity: Activity, uri: Uri): String? {
        stopAudio()   // a second start without a stop must not leave two drains
        val romPath = GbRomResolver.resolve(activity, uri)
        if (romPath == null) {
            lastError = "Could not read that game file."
            return lastError
        }
        val savePath = GbRomResolver.savePathFor(activity, romPath)
        // The debug channel's folder goes in before the load, so the new core
        // reads cmd.txt from its first frame. Never fatal: a missing folder
        // only means no file-driven commands.
        try {
            GbAccessibilityScript.setDebugDir(DebugChannel.dir(activity))
        } catch (t: Throwable) {
            Log.w(TAG, "debug channel folder not set", t)
        }
        val started = try {
            GbAccessibilityScript.start(activity, romPath, savePath)
        } catch (t: Throwable) {
            Log.e(TAG, "Game Boy start threw", t)
            false
        }
        running = started
        if (!started) {
            lastError = "The accessibility reader could not start for that game."
            return lastError
        }
        Log.i(TAG, "reader running: $romPath")
        appContext = activity.applicationContext
        audioEnabledApplied = null
        applyGameSoundSwitch()
        startAudio()
        // The bridge exists now, so the view can actually be attached.
        storedAnnouncementView?.let { AccessibilityScript.setAnnouncementView(it) }
        lastError = ""
        return null
    }

    fun stop() {
        running = false
        heldMask = 0
        // Audio first, joined, so nothing is still reading when the core goes.
        stopAudio()
        GbAccessibilityScript.stop()
    }

    /** One emulated frame. Driven from the Compose frame clock, see [rememberFrameClock]. */
    fun runFrame() {
        if (!running) return
        applyGameSoundSwitch()
        GbAccessibilityScript.runFrame()
    }

    // ---- the game's audio ---------------------------------------------------

    @Volatile
    private var appContext: Context? = null

    /** What the native gate was last told; null forces the next apply. */
    private var audioEnabledApplied: Boolean? = null

    /** Resolved per frame (a flip in Settings lands on the next frame), applied only when it moved. */
    private fun applyGameSoundSwitch() {
        val ctx = appContext ?: return
        val on = GameSoundSettings.gameSoundEnabled(ctx)
        if (audioEnabledApplied == on) return
        audioEnabledApplied = on
        try {
            GbAccessibilityScript.setAudioEnabled(on)
        } catch (t: Throwable) {
            Log.w(TAG, "Game Boy sound switch unavailable: ${t.message}")
        }
    }

    @Volatile
    private var audioRunning = false

    private var audioThread: Thread? = null
    private var audioTrack: AudioTrack? = null

    @Volatile
    private var audioFramesWritten = 0L

    /** Frames handed to the speaker since the game started (diagnostics). */
    val audioFramesPlayed: Long get() = audioFramesWritten

    private fun startAudio() {
        val rate = GbAccessibilityScript.AUDIO_RATE
        val minBuffer = AudioTrack.getMinBufferSize(
            rate,
            AudioFormat.CHANNEL_OUT_STEREO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        if (minBuffer <= 0) {
            Log.w(TAG, "Game Boy audio unavailable: min buffer size $minBuffer")
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
                        .setSampleRate(rate)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                        .build()
                )
                // ~0.125 s of stereo s16, or the platform minimum if larger:
                // small enough that a press and its sound stay together.
                .setBufferSizeInBytes(maxOf(minBuffer, rate / 2))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
        } catch (t: Throwable) {
            Log.w(TAG, "Game Boy audio unavailable: ${t.message}")
            return
        }
        audioTrack = track
        audioFramesWritten = 0L
        audioRunning = true
        track.play()
        val thread = Thread { audioLoop(track) }
        thread.name = "gb-audio"
        audioThread = thread
        thread.start()
        Log.i(TAG, "Game Boy audio started: AudioTrack at $rate Hz, stereo")
    }

    private fun audioLoop(track: AudioTrack) {
        val buffer = ShortArray(1024 * 2)
        while (audioRunning) {
            val frames = try {
                GbAccessibilityScript.readAudio(buffer, 1024)
            } catch (t: Throwable) {
                0
            }
            if (frames > 0) {
                track.write(buffer, 0, frames * 2, AudioTrack.WRITE_BLOCKING)
                audioFramesWritten += frames
            } else {
                // Dry: the frame clock refills it ~60 times a second. Sleep
                // briefly rather than spin a processor on an empty queue.
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
            track.release()
        }
    }

    /**
     * The current frame (pixels, width, height) as RGBA8888 ints, or
     * null when nothing is loaded — the session screen's game picture
     * (the iOS app's FrameImage). The native copy has existed since
     * the reader landed (GbAccessibilityScript.currentFrame); this is
     * its session surface, the twin of PspGameSession.currentFrame.
     */
    fun currentFrame(): Triple<IntArray, Int, Int>? {
        if (!running) return null
        return GbAccessibilityScript.currentFrame()
    }

    // ---- the emulated pad ---------------------------------------------------
    //
    // ⛔ HOLD, NOT TAP. Walking in these games means holding a direction, so the pad
    // press and release are separate calls. The mask is OR'd here and pushed whole;
    // GbSessionJNI turns the changed bits into the shared core's per-button calls.

    fun pressKey(bit: Int) {
        if (!running) return
        heldMask = heldMask or bit
        GbAccessibilityScript.setKeys(heldMask)
    }

    fun releaseKey(bit: Int) {
        heldMask = heldMask and bit.inv()
        if (running) GbAccessibilityScript.setKeys(heldMask)
    }

    /** Cleared on stop, so a button released after the game ends cannot stick. */
    private var heldMask = 0

    // ---- script hotkeys -----------------------------------------------------
    //
    // The reader's own keys (P pathfind, M map name, E tiles, K read item, ...).
    // These go to the script, not the game, so they use onKeyEvent rather than the
    // key mask. Edge-triggered: press now, release on the next frame, because the
    // script's own edge detector wants a single transition to fire on.

    fun pressHotkey(key: Char) {
        if (running) GbAccessibilityScript.onKeyEvent(key, true)
    }

    fun releaseHotkey(key: Char) {
        if (running) GbAccessibilityScript.onKeyEvent(key, false)
    }

    fun setKeys(mask: Int) {
        heldMask = mask
        if (running) GbAccessibilityScript.setKeys(mask)
    }

    /**
     * A script hotkey as a single tap: press now, release shortly after.
     *
     * ⛔ EDGE-TRIGGERED ON PURPOSE. The script's own edge detector wants ONE
     * transition, and the release has to follow rather than never -- the WebView page
     * used an 80 ms timeout for the same reason.
     */
    fun tapHotkey(key: Char) {
        if (!running) return
        GbAccessibilityScript.onKeyEvent(key, true)
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(
            { if (running) GbAccessibilityScript.onKeyEvent(key, false) },
            80L,
        )
    }

    /** Release a held key a moment later, for the screen-reader click path. */
    fun releaseKeySoon(bit: Int) {
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(
            { releaseKey(bit) },
            120L,
        )
    }

    /**
     * Stop the reader talking, and stay quiet until the player asks for something.
     *
     * ⛔ THIS IS NOT THE SCRIPT'S OWN STOP KEY. On Game Boy the script's R key moves
     * the camera, so there is no reader key that stops speech -- iOS distinguishes
     * exactly this case (`hasDirectSpeechStop`) and stops at the engine instead,
     * because a screen reader's own "stop" gesture only ends the current utterance
     * while the script keeps producing lines.
     *
     * ⛔ AND THE GAME KEEPS RUNNING. The old Android host had no way to stop just
     * the speech, so this used to tear the whole core down while the screen still
     * showed the game (and `running` stayed true over a dead core). The shared
     * core's StopSpeech command clears its announcement queue and stops the voice
     * — iOS's behaviour — and the game goes on.
     */
    fun stopSpeech() {
        AccessibilityScript.speak("", true)
        if (running) {
            try {
                GbAccessibilityScript.stopSpeech()
            } catch (t: Throwable) {
                Log.w(TAG, "Game Boy stop speech unavailable: ${t.message}")
            }
        }
    }

    /**
     * Re-read the newest spoken line, or step one line further back
     * through what was spoken — the shared bridge's spoken-history
     * walk (see AccessibilitySpeech), iOS's RepeatNewest /
     * RepeatOlder pair. False when there is nothing (older) to
     * repeat; speaking the refusal is the screen's job, as it is for
     * iOS's callers.
     */
    fun repeatLastSpeech(): Boolean = AccessibilityScript.repeatLastSpeech()

    fun repeatOlderSpeech(): Boolean = AccessibilityScript.repeatOlderSpeech()

    /** True once a game is running, so the pad and commands can appear. */
    val isRunning: Boolean get() = running
}

/**
 * The mGBA key bits, in the core's own order.
 *
 * ⛔ THESE ARE THE CORE'S VALUES, NOT A UI CONVENTION. mGBA's GBAKey order is
 * A B Select Start Right Left Up Down R L, and a wrong bit is a wrong button with
 * no error anywhere — the player presses Up and the game reads Right.
 */
object GbKeys {
    const val A = 1
    const val B = 2
    const val SELECT = 4
    const val START = 8
    const val RIGHT = 16
    const val LEFT = 32
    const val UP = 64
    const val DOWN = 128
    const val R = 256
    const val L = 512
}

/**
 * Drives the Game Boy session's frames while a game runs — PACED TO THE
 * CONSOLE, NOT TO THE DISPLAY.
 *
 * ⛔ THE CLOCK IS THE APP'S, AND IT IS THE SAME CADENCE THE READER ASSUMES. The page
 * used requestAnimationFrame; the iOS app uses a CADisplayLink. Driving frames from
 * a frame clock (rather than a free-running native thread) keeps the reader's
 * speech callbacks on the same thread that produced them. `withFrameNanos` is
 * Compose's frame signal and suspends between frames instead of spinning.
 *
 * ⛔ BUT ONE EMULATED FRAME PER DISPLAY FRAME RUNS THE GAME FAST on any screen
 * above 60 Hz (90 and 120 Hz phones are common). That was only a speed problem
 * while the game was silent; with its sound playing, a 120 Hz screen would make
 * twice the audio the speaker can play, and the overflow would be cut out as
 * gaps. So, like the DS, PSP, Mesen and Genesis clocks, a frame only RUNS when
 * its slot has arrived: the Game Boy and the GBA both render 59.7275 frames a
 * second (4194304 / 70224 and 16777216 / 280896), which is exactly the rate
 * their audio plays out in real time. A stretch that falls behind resyncs to
 * the next slot instead of bursting to catch up.
 */
@Composable
fun rememberFrameClock(running: Boolean) {
    if (!running) return
    androidx.compose.runtime.LaunchedEffect(Unit) {
        // 1 s / 59.7275 Hz ≈ 16.743 ms.
        val frameNs = (1_000_000_000.0 / GB_FRAMES_PER_SECOND).toLong()
        var nextFrameAt = 0L
        while (true) {
            val now = androidx.compose.runtime.withFrameNanos { it }
            if (nextFrameAt == 0L) nextFrameAt = now
            if (now >= nextFrameAt) {
                GbGameSession.runFrame()
                nextFrameAt += frameNs
                if (nextFrameAt <= now) nextFrameAt = now + frameNs
            }
        }
    }
}

/** The Game Boy's (and the GBA's) frame rate: 4194304 Hz / 70224 cycles per frame. */
const val GB_FRAMES_PER_SECOND = 4194304.0 / 70224.0
