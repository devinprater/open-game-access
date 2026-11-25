package com.devin.opengameaccess

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.util.Log
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * ⛔ IN-APP PROOF HARNESS (PlayStation 2 boot proof): inert unless the
 * launch intent carries [EXTRA_ROM], which only an
 * `adb shell am start --es oga_ps2_probe_rom <path>` sets — with no
 * extra the launcher is unchanged.
 *
 * Unlike [PspProbe], which drives its core's ABI directly, this drives
 * the REAL session: [Ps2GameSession.start] with a file Uri for the
 * disc, the same call the launcher's Start button makes after a pick.
 * The load therefore runs the whole shipping path — Ps2RomResolver,
 * Ps2SessionJNI (which hands the process's JavaVM to the wrapper, the
 * fix Play!'s Android build needs in order to start its VM thread),
 * Core/ps2_core.cpp and the dlopen()ed core — and the launcher's own
 * frame clock drives the frames, because the session's running state
 * is Compose state the launcher observes. The probe only waits,
 * samples the session's frame picture, and stops the session at the
 * end. Its lines land under the logcat tag "Ps2Proof"; the session's
 * own lines (the serial at start, the frame heartbeat) land under the
 * session's tag beside them, and the game's RAM is read through the
 * session's debug channel (oga-debug/cmd.txt) while it runs.
 */
object Ps2Probe {

    private const val TAG = "Ps2Proof"

    const val EXTRA_ROM = "oga_ps2_probe_rom"
    const val EXTRA_SECONDS = "oga_ps2_probe_seconds"

    /** Called from MainActivity.onCreate with the launch intent. */
    fun maybeStart(activity: Activity, intent: Intent?) {
        val rom = intent?.getStringExtra(EXTRA_ROM)?.takeIf { it.isNotBlank() } ?: return
        val seconds = intent.getIntExtra(EXTRA_SECONDS, 90)
        val thread = Thread { runProof(activity, rom, seconds) }
        thread.name = "ps2-probe"
        thread.start()
    }

    private fun runProof(activity: Activity, rom: String, seconds: Int) {
        Log.i(TAG, "PROOF: start rom=$rom seconds=$seconds")
        val done = CountDownLatch(1)
        val error = arrayOfNulls<String>(1)
        Ps2GameSession.start(activity, Uri.fromFile(File(rom))) { e ->
            error[0] = e
            done.countDown()
        }
        // The resolver copies the disc into the cache before the load,
        // which for a whole DVD takes a while; the load itself is fast
        // after that. Five minutes is a ceiling, not an expectation.
        if (!done.await(300, TimeUnit.SECONDS)) {
            Log.e(TAG, "PROOF: FAILED start: timed out waiting for the session")
            return
        }
        val startError = error[0]
        if (startError != null) {
            Log.e(TAG, "PROOF: FAILED start: $startError")
            return
        }
        Log.i(TAG, "PROOF: session running code=${Ps2GameSession.loadedGameCode}")

        // The launcher's frame clock is driving the session now. Sample
        // the frame picture every five seconds and say what it holds:
        // a running game changes and is not uniformly black for long.
        val deadline = System.nanoTime() + seconds.toLong() * 1_000_000_000L
        var samples = 0
        var lastSignature = ""
        while (System.nanoTime() < deadline) {
            Thread.sleep(5_000)
            val frame = try {
                Ps2GameSession.currentFrame()
            } catch (t: Throwable) {
                null
            }
            if (frame == null) {
                Log.i(TAG, "PROOF: frame sample: none yet")
                continue
            }
            val (pixels, w, h) = frame
            var lit = 0
            var sumR = 0L
            var sumG = 0L
            var sumB = 0L
            for (p in pixels) {
                val r = (p shr 16) and 0xFF
                val g = (p shr 8) and 0xFF
                val b = p and 0xFF
                if (r or g or b != 0) lit++
                sumR += r
                sumG += g
                sumB += b
            }
            val n = if (pixels.isEmpty()) 1 else pixels.size
            val signature = "${w}x${h} lit=$lit mean=${sumR / n}/${sumG / n}/${sumB / n}"
            samples++
            Log.i(TAG, "PROOF: frame sample $samples: $signature")
            lastSignature = signature
        }
        Log.i(TAG, "PROOF: done code=${Ps2GameSession.loadedGameCode} samples=$samples last=$lastSignature")
        activity.runOnUiThread { Ps2GameSession.stop() }
    }
}
