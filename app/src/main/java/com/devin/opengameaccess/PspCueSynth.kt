package com.devin.opengameaccess

import kotlin.math.cos
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.sin

/**
 * The PSP battle-cue synth: the Android half of the cue design
 * (docs/design/cue-synth.md), a port of the iOS CueSynth's encoding to
 * this session's audio path.
 *
 * WHAT IT MAKES. Beside the game's own audio, a third sound the app
 * generates itself: the lock-on beacon (identity in timbre, distance
 * in pulse rate, centred while locked; in free-flight modes the pan
 * carries the bearing), the one-shot family — the health beeps
 * (a hit that dropped a health bar, pitch mapped from the health
 * fraction by the ADAPTER and played here exactly as given, the foe
 * on the right and the player on the left) and the gauge-full
 * earcons — and the guard window's rising sweep (Attack of the
 * Saiyans: pitch climbing toward the guard moment, a soft beat at
 * the window's open).
 *
 * ⛔ THE SEAM IS THE SESSION'S OWN DRAIN. iOS mixes its synth into the
 * emulator's audio graph at the render callback; here the game audio
 * already passes through [PspGameSession]'s audio thread as PCM, so
 * [render] adds the cue samples into that same buffer before it
 * reaches the AudioTrack — one output path, so cue and game can
 * never drift apart, exactly the iOS arrangement at a different seam.
 *
 * ⛔ THREADS, THE IOS DISCIPLINE. Values are written from the frame
 * thread on each poll ([updateBeacon], [updateFieldBeacon],
 * [updateOneShots]) and read by the audio thread in [render]. Every
 * field is a plain Float/Double read a single time per sample, so
 * there is no torn read that matters — a one-frame-stale frequency
 * is inaudible, and the alternative (a lock on the audio path) is
 * how a render loop comes to glitch.
 */
object PspCueSynth {

    /** The core's audio rate (Core/psp_core.h); the session's track runs at it too. */
    private const val SAMPLE_RATE = 32768.0

    // Family indices for the level table: one level per family, never
    // per side — the foe and own beeps share the health level, so
    // turning one thing down can never make the other side lie.
    private const val FAMILY_BEACON = 0
    private const val FAMILY_HEALTH = 1
    private const val FAMILY_TICK = 3

    /** One cue voice: a sine at a frequency, a gain, a pan, and an envelope. */
    private class Voice {
        var frequency: Float = 900f
        var gain: Float = 0f // 0 = silent
        var pulseHz: Float = 0f // 0 = continuous tone
        var dutyCycle: Float = 0.5f
        var pan: Float = 0f

        // One-shot state: samples left in a triggered shot (counted
        // down only by render), its full length, its decay exponent,
        // and its second-harmonic level (see the iOS CueVoice docs —
        // the faster-decaying harmonic is the own-hit thud's dullness).
        var shotLeft: Double = 0.0
        var shotTotal: Double = 1.0
        var shotCurve: Float = 2f
        var partial: Float = 0f

        // Render-thread state.
        var phase: Double = 0.0
        var pulsePhase: Double = 0.0
        var partialPhase: Double = 0.0
    }

    private val beaconVoice = Voice()
    private val foeHitVoice = Voice()
    private val ownHitVoice = Voice()
    private val focusCueVoice = Voice()
    private val dlinkCueVoice = Voice()
    private val guardVoice = Voice()
    private val guardBeatVoice = Voice()

    /** Per-family level, 0 to 1 (beacon, health; meter/tick slots kept for parity). */
    private val levels = floatArrayOf(1f, 1f, 1f, 1f)

    /** Cues duck under speech; mirrors the iOS synth's control (nothing calls it yet, there either). */
    private var speechDuck: Float = 1f

    fun setLevel(family: Int, v: Float) {
        if (family in levels.indices) levels[family] = v.coerceIn(0f, 1f)
    }

    fun setSpeechDucking(ducked: Boolean) {
        speechDuck = if (ducked) 0.35f else 1f
    }

    // ---- the beacon encoding (the iOS numbers, unchanged) ---------------------

    private const val ENEMY_HZ = 660f
    private const val CORE_HZ = 1180f
    private const val FAR_UNITS = 45f
    private const val NEAR_UNITS = 4f
    private const val SLOW_HZ = 1.6f
    private const val FAST_HZ = 11f

    private const val FIELD_FAR_UNITS = 2500f
    private const val FIELD_NEAR_UNITS = 60f
    private const val FIELD_SLOW_HZ = 1.2f
    private const val FIELD_FAST_HZ = 9f
    private const val FIELD_ENEMY_HZ = 520f
    private const val FIELD_CITY_HZ = 780f
    private const val FIELD_ALLY_HZ = 1120f

    /**
     * The lock-on beacon from one poll. [state] is the host's beacon
     * state: 0 nothing, 1 battle with no lock, 2 enemy, 3 EX core,
     * 4 the one-frame EX-gain pulse (the core timbre, marking a
     * pickup). Silence for 0 and 1 — a battle with no lock has
     * nothing to point at.
     */
    fun updateBeacon(state: Int, distance: Float, enabled: Boolean) {
        val v = beaconVoice
        if (!enabled || (state != 2 && state != 3 && state != 4)) {
            v.gain = 0f
            return
        }
        v.frequency = if (state == 4) CORE_HZ else ENEMY_HZ
        val d = distance.coerceIn(NEAR_UNITS, FAR_UNITS)
        val t = (d - NEAR_UNITS) / (FAR_UNITS - NEAR_UNITS) // 0 near, 1 far
        v.pulseHz = FAST_HZ + (SLOW_HZ - FAST_HZ) * t
        v.dutyCycle = 0.5f
        // Gain rises slightly as the target closes, never to zero at
        // the far end — silence there would read as "no lock".
        v.gain = 0.18f + 0.12f * (1f - t)
    }

    /**
     * The free-flight beacon: no lock mechanic, the player steers, so
     * PAN CARRIES THE BEARING (the one place the centred rule is
     * suspended — see the design doc). [kind]: 1 enemy, 2 city,
     * 3 ally, distinct timbres so identity survives equal pulse
     * rates. A stale heading ([headingLive] false) centres the pan
     * rather than pointing confidently the wrong way, and the rate
     * still carries distance and front/back.
     */
    fun updateFieldBeacon(kind: Int, bearingDegrees: Float, distance: Float,
                          headingLive: Boolean, enabled: Boolean) {
        val v = beaconVoice
        if (!enabled || kind == 0) {
            v.gain = 0f
            v.pan = 0f
            return
        }
        v.frequency = when (kind) {
            1 -> FIELD_ENEMY_HZ
            2 -> FIELD_CITY_HZ
            else -> FIELD_ALLY_HZ
        }
        val bearing = if (headingLive) bearingDegrees else 180f
        val radians = bearing * Math.PI.toFloat() / 180f
        v.pan = sin(radians)

        val d = distance.coerceIn(FIELD_NEAR_UNITS, FIELD_FAR_UNITS)
        val t = (d - FIELD_NEAR_UNITS) / (FIELD_FAR_UNITS - FIELD_NEAR_UNITS)
        val behind: Float = if (kotlin.math.abs(normalizedBearing(bearingDegrees)) > 90f) 0.35f else 1.0f
        v.pulseHz = (FIELD_FAST_HZ + (FIELD_SLOW_HZ - FIELD_FAST_HZ) * t) * behind
        v.dutyCycle = 0.5f
        v.gain = 0.18f + 0.12f * (1f - t)
    }

    /** Signed bearing in -180..180, so "behind" is expressible. */
    private fun normalizedBearing(deg: Float): Float {
        var d = deg
        while (d > 180f) d -= 360f
        while (d < -180f) d += 360f
        return d
    }

    /** The two beacon modes share one voice; a takeover clears the other's pan. */
    fun silenceLockBeacon() {
        beaconVoice.pan = 0f
    }

    fun silenceFieldBeacon() {
        beaconVoice.pan = 0f
    }

    // ---- the one-shot family: health beeps and gauge-full earcons -------------

    // The recipes, from docs/design/cue-synth.md: a 35 ms bright tick
    // for the foe, a 55 ms duller thud for the player, ~60 ms centred
    // earcon blips in the beacon family's carrier register.
    private const val FOE_HIT_SECONDS = 0.035
    private const val OWN_HIT_SECONDS = 0.055
    private const val EARCON_SECONDS = 0.060
    private const val FOCUS_HZ = 1560f
    private const val DLINK_HZ = 1180f

    /** Load one voice with a one-shot; phase restarts at a zero crossing. */
    private fun trigger(v: Voice, hz: Float, pan: Float, seconds: Double,
                        gain: Float, curve: Float, partial: Float) {
        v.frequency = hz
        v.pan = pan.coerceIn(-1f, 1f)
        v.pulseHz = 0f
        v.phase = 0.0
        v.partialPhase = 0.0
        v.shotCurve = curve
        v.partial = partial
        v.shotTotal = maxOf(1.0, seconds * SAMPLE_RATE)
        v.shotLeft = v.shotTotal
        v.gain = gain
    }

    /**
     * Voice one frame of one-shot events. [bits] is the host poll's
     * health bitmask: 1 foe beep, 2 own beep, 4 Focus full, 8 D-Link
     * full. The pitch arrives WITH the event — the adapter mapped the
     * new health fraction logarithmically (foe 1200→300 Hz, own
     * 800→200 Hz) — and is played exactly as given, never re-mapped;
     * the pan likewise (+0.8 foe right, −0.8 own left). [enabled] is
     * the battle-cues switch, the same gate as the beacon: off drops
     * the events unheard, because a one-frame moment held for later
     * would voice a hit that is already over.
     */
    fun updateOneShots(bits: Int, foeHz: Float, foePan: Float,
                       ownHz: Float, ownPan: Float, enabled: Boolean) {
        if (!enabled) return
        if (bits and 1 != 0) {
            trigger(foeHitVoice, foeHz, foePan, FOE_HIT_SECONDS,
                gain = 0.5f, curve = 2.2f, partial = 0f)
        }
        if (bits and 2 != 0) {
            trigger(ownHitVoice, ownHz, ownPan, OWN_HIT_SECONDS,
                gain = 0.55f, curve = 1.6f, partial = 0.35f)
        }
        if (bits and 4 != 0) {
            trigger(focusCueVoice, FOCUS_HZ, 0f, EARCON_SECONDS,
                gain = 0.4f, curve = 1.4f, partial = 0f)
        }
        if (bits and 8 != 0) {
            trigger(dlinkCueVoice, DLINK_HZ, 0f, EARCON_SECONDS,
                gain = 0.4f, curve = 1.4f, partial = 0f)
        }
    }

    // ---- the guard window: a continuous rising sweep (Attack of the Saiyans) --
    //
    // The third cue shape, the iOS CueSynth's timing family: a window
    // running toward a moment. The poll's guard answer is continuous —
    // active on every frame the window is open, frac running 0 (just
    // opened) to 1 (press NOW) — and the adapter has already
    // normalized each attack family's own timing into that fraction,
    // so this synth only makes frac audible: a tone whose pitch
    // climbs as frac runs, its peak the guard moment itself (the
    // Budokai Access model). A soft low beat sounds on the window's
    // opening frame — the restart beat. CENTRED, always: the window
    // names no target and no side, so the cue must not pretend to
    // one either. The sweep rides the TICK family's level; the beat
    // is a dedicated one-shot voice because the sweep is already
    // sounding on its own voice when the beat must land.

    // Sweep endpoints: an exponential rise (300 → 900 Hz) so the
    // climb is perceptually even, in a register below the beacon's
    // carriers and above the health thud.
    private const val GUARD_BASE_HZ = 300f
    private const val GUARD_RISE_RATIO = 3f
    // Gain rises slightly with frac but never starts at zero — a
    // window whose opening is inaudible is a window half missed, and
    // silence has to keep meaning "no window".
    private const val GUARD_GAIN_BASE = 0.30f
    private const val GUARD_GAIN_RISE = 0.15f
    // The restart beat: a soft, low, short blip under the sweep's start.
    private const val GUARD_BEAT_HZ = 160f
    private const val GUARD_BEAT_SECONDS = 0.045

    /**
     * Voice one frame of the guard window. [frac] is played as a
     * pure function of the fraction, never re-shaped — the adapter
     * owns the timing truth, the synth only renders it. [enabled]
     * is the same battle-cues switch as every cue here: off
     * silences the sweep at once and drops the beat unheard — a
     * window is a live moment, and voicing its opening late would
     * mark a moment already gone.
     */
    fun updateGuard(active: Boolean, frac: Float, beat: Boolean, enabled: Boolean) {
        val v = guardVoice
        if (!enabled || !active) {
            v.gain = 0f
            return
        }
        val f = frac.coerceIn(0f, 1f)
        v.frequency = GUARD_BASE_HZ * GUARD_RISE_RATIO.pow(f)
        v.pulseHz = 0f
        v.pan = 0f
        v.gain = GUARD_GAIN_BASE + GUARD_GAIN_RISE * f
        if (beat) {
            trigger(guardBeatVoice, GUARD_BEAT_HZ, 0f, GUARD_BEAT_SECONDS,
                gain = 0.35f, curve = 1.4f, partial = 0f)
        }
    }

    /** Silence every voice. Used when the game stops. */
    fun silenceAll() {
        beaconVoice.gain = 0f
        guardVoice.gain = 0f
        for (v in arrayOf(foeHitVoice, ownHitVoice, focusCueVoice, dlinkCueVoice, guardBeatVoice)) {
            v.gain = 0f
            v.shotLeft = 0.0
        }
    }

    // ---- the render side --------------------------------------------------------

    /**
     * One sample of one voice (before pan), advancing its render state.
     * A state voice (the beacon) uses the raised-cosine pulse envelope;
     * a one-shot (shotLeft > 0) decays — a ~1 ms attack ramp so the
     * start cannot click, then a power curve to zero, where the voice
     * silences itself: a triggered moment must end even if no later
     * poll ever touches the voice again.
     */
    private fun voiceSample(v: Voice, g: Float): Float {
        var env = 1.0
        if (v.shotLeft > 0) {
            val frac = v.shotLeft / v.shotTotal // 1 at start, 0 at end
            val attack = min(1.0, (v.shotTotal - v.shotLeft) / (SAMPLE_RATE * 0.001))
            env = attack * frac.pow(v.shotCurve.toDouble())
            v.shotLeft -= 1
            if (v.shotLeft <= 0) {
                v.shotLeft = 0.0
                v.gain = 0f
            }
        } else if (v.pulseHz > 0) {
            val period = SAMPLE_RATE / v.pulseHz
            val pos = (v.pulsePhase % period) / period
            val duty = v.dutyCycle.coerceIn(0.05f, 1.0f).toDouble()
            val ramp = 0.06 // fraction of the period
            env = if (pos < duty) {
                val into = pos
                val out = duty - pos
                min(1.0, min(into / ramp, out / ramp))
            } else {
                0.0
            }
            v.pulsePhase += 1
            if (v.pulsePhase >= period) v.pulsePhase -= period
        }

        var s = sin(v.phase * 2 * Math.PI) * g * env
        if (v.partial > 0) {
            // The harmonic gets the same envelope times one more decay
            // factor: present at the hit, gone before the fundamental —
            // that asymmetry is what makes the thud duller than the tick.
            val frac = maxOf(0.0, v.shotLeft / v.shotTotal)
            s += sin(v.partialPhase * 2 * Math.PI) * g * env * v.partial * frac
            v.partialPhase += v.frequency * 2.0 / SAMPLE_RATE
            if (v.partialPhase >= 1) v.partialPhase -= 1
        }
        v.phase += v.frequency / SAMPLE_RATE
        if (v.phase >= 1) v.phase -= 1
        return s.toFloat()
    }

    /**
     * Mix every voice into an interleaved stereo s16 buffer, ADDING to
     * what is already there (the game's own audio drained by the
     * session). [frames] is the frame count; the buffer is (L,R)
     * pairs at the core's 32768 Hz.
     */
    fun render(buffer: ShortArray, frames: Int) {
        // The one-shot voices and the family level each rides.
        val oneShots = arrayOf(
            foeHitVoice to FAMILY_HEALTH, ownHitVoice to FAMILY_HEALTH,
            focusCueVoice to FAMILY_BEACON, dlinkCueVoice to FAMILY_BEACON,
            guardBeatVoice to FAMILY_TICK,
        )
        var anyAudible = beaconVoice.gain > 0 || guardVoice.gain > 0
        if (!anyAudible) {
            for ((v, _) in oneShots) {
                if (v.gain > 0) {
                    anyAudible = true
                    break
                }
            }
        }
        if (!anyAudible) return

        // Headroom, the iOS constant: 0.5 for the voice sum, times the
        // equal-power centre gain, so a centred voice is neither louder
        // nor quieter than a panned one and four voices cannot clip.
        val k = 0.3535534f
        for (f in 0 until frames) {
            var mixL = 0f
            var mixR = 0f

            val gb = beaconVoice.gain * levels[FAMILY_BEACON] * speechDuck
            if (gb > 0) {
                val s = voiceSample(beaconVoice, gb)
                // Equal-power pan: the sqrt law holds power constant
                // across the arc, where a linear crossfade would dip
                // ~3 dB in the middle and read as "farther".
                val angle = (beaconVoice.pan.coerceIn(-1f, 1f) + 1f) * Math.PI.toFloat() / 4f
                mixL += s * cos(angle)
                mixR += s * sin(angle)
            }
            // The guard sweep: a state voice like the beacon, centred
            // (its pan is always 0 — the window names no side), riding
            // the tick family's level.
            val gg = guardVoice.gain * levels[FAMILY_TICK] * speechDuck
            if (gg > 0) {
                val s = voiceSample(guardVoice, gg)
                val angle = (guardVoice.pan.coerceIn(-1f, 1f) + 1f) * Math.PI.toFloat() / 4f
                mixL += s * cos(angle)
                mixR += s * sin(angle)
            }
            for ((v, fam) in oneShots) {
                val g = v.gain * levels[fam] * speechDuck
                // Not `g > 0` alone: a shot in flight must count down
                // and self-silence even while ducked or levelled to
                // zero, or it would hang armed until the next trigger.
                if (v.shotLeft <= 0 && g <= 0) continue
                val s = voiceSample(v, g)
                val angle = (v.pan.coerceIn(-1f, 1f) + 1f) * Math.PI.toFloat() / 4f
                mixL += s * cos(angle)
                mixR += s * sin(angle)
            }

            val l = (mixL * k).coerceIn(-1f, 1f)
            val r = (mixR * k).coerceIn(-1f, 1f)
            val li = f * 2
            buffer[li] = (buffer[li].toInt() + (l * 32767).toInt())
                .coerceIn(-32768, 32767).toShort()
            buffer[li + 1] = (buffer[li + 1].toInt() + (r * 32767).toInt())
                .coerceIn(-32768, 32767).toShort()
        }
    }
}
