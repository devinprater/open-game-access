//
//  CueSynth.swift — host-side audio cues (item 5 of the adapter plan).
//
//  WHAT THIS IS FOR. Speech is discrete and slow: it can say "locked, 30 metres away" but
//  it cannot say it thirty times a second, which is what a beacon needs. So the app grows a
//  third sound, beside the game's own audio and speech: a tone it generates itself.
//
//  THE ENCODING, from the reviewed mods (docs/design/cue-synth.md):
//    * ONE voice per cue family; a family's level is its gain.
//    * IDENTITY IN TIMBRE, DISTANCE IN RATE. The enemy and the EX core are different
//      sounds; the pulse quickens as the target closes. Rate never carries identity —
//      a near core and a far enemy would pulse alike.
//    * CENTRED, NOT PANNED. The owner asked for this while locked: "since we have lock on,
//      maybe just have it centered, and have a slightly low pitched beep that gets faster
//      the closer we are to the targeted entity." A lock-on cue does not need to say which
//      way to turn; the game already turns you.
//    * SILENCE IS A CUE. No battle, or no lock, means no tone — not a resting tone.
//
//  ⛔ SYNTHESISED HOST-SIDE, NEVER IN THE CORE. The adapter contract is read-only RAM plus
//  speak/log/buttons; the cue only reads poke_cue_snapshot(). It changes feedback, never
//  aim, damage or movement.
//
//  It mixes into the emulator's own audio graph (the session's AVAudioEngine), so there is
//  one output path instead of two that can drift.
//
import Foundation
import CPokeCore

/// One cue voice: a sine at a frequency, a gain, a pan, and a pulse envelope.
///
/// Values are written from the main thread on each poll and read by the render block on
/// the audio thread. Every field is a plain Float/Bool read a single time per sample, so
/// there is no torn read that matters — a one-frame-stale frequency is inaudible, and the
/// alternative (a lock on the audio thread) is how a render callback comes to glitch.
final class CueVoice {
    var frequency: Float = 900
    var gain: Float = 0            // 0 = silent
    /// How many times per second the pulse repeats. 0 = continuous tone.
    var pulseHz: Float = 0
    /// Fraction of each pulse period that sounds. 1 = continuous.
    var dutyCycle: Float = 0.5
    /// Stereo position, -1 hard left to +1 hard right. Used ONLY by a cue that must carry a
    /// direction -- the lock-on beacon leaves it at 0, because the game already aims you.
    var pan: Float = 0

    // ---- one-shot state (the health / gauge-full event family) --------------
    //
    // A one-shot is a triggered moment, not a state: the trigger (main thread)
    // loads frequency/pan/gain and a sample count, and the render block counts
    // the shot down and shapes it. The beacon voices never touch these fields.
    /// Samples left in a triggered one-shot; counted down ONLY by the render
    /// block, and hitting 0 silences the voice (it sets gain back to 0 there).
    fileprivate var shotLeft: Double = 0
    /// The one-shot's full length in samples: the decay envelope's denominator.
    fileprivate var shotTotal: Double = 1
    /// Decay exponent of the one-shot envelope. Higher sheds energy faster --
    /// the foe's bright tick decays harder than the player's duller thud.
    var shotCurve: Float = 2
    /// Second-harmonic level at the one-shot's start, decaying FASTER than the
    /// fundamental (the envelope multiplies it by an extra decay factor), so a
    /// voice can start slightly rich and settle pure -- the thud's dullness.
    var partial: Float = 0

    // Render-block state (touched only from the audio thread).
    fileprivate var phase: Double = 0
    fileprivate var pulsePhase: Double = 0
    fileprivate var partialPhase: Double = 0
}

/// The cue mixer. Owns a fixed pool of voices — no allocation on the audio path, which is
/// the rule the spec inherited from CFC2.
///
/// POOL SIZING: one voice per cue FAMILY, plus the beacon. Today that is the beacon alone;
/// the pool is sized for the families the spec names (beacon, health, meter, tick) so
/// adding one costs no re-architecture.
final class CueSynth {
    /// ⛔ NOT Sendable, and deliberately so: the synth owns the audio buffer and is touched
    /// only from the render callback plus the main-actor setters, which never overlap. Swift 6
    /// rejects the plain static because it cannot see that ownership, so the sharing is marked
    /// unsafe explicitly here rather than restructured -- the alternative (a lock or an actor
    /// hop on the audio path) would put synchronisation INSIDE the render callback, which is
    /// exactly what a realtime path must not do.
    nonisolated(unsafe) static let shared = CueSynth()

    /// Family order is fixed and is the order of the voices array.
    enum Family: Int, CaseIterable {
        case beacon
        case health
        case meter
        case tick
    }

    private(set) var voices: [CueVoice] = Family.allCases.map { _ in CueVoice() }

    /// Per-family level, 0 to 1. ONE LEVEL PER FAMILY, NEVER PER SIDE: the lock cue shares
    /// the beacon level, so turning one thing down can never make the beacon lie.
    ///
    /// ⛔ APPLIED AS GAIN, NOT AS A SKIP. A muted family still runs its voice and envelope,
    /// so muting fades instead of clicking — the click is the discontinuity, not the
    /// silence.
    private var levels: [Float] = Family.allCases.map { _ in 1.0 }

    /// Cues duck under speech so the beacon never masks an answer the player asked for.
    private var speechDuck: Float = 1.0

    private init() {}

    func level(_ f: Family) -> Float { levels[f.rawValue] }
    func setLevel(_ f: Family, _ v: Float) { levels[f.rawValue] = max(0, min(1, v)) }

    /// Duck or restore. Called from the main thread when speech starts and stops.
    func setSpeechDucking(_ ducked: Bool) {
        speechDuck = ducked ? 0.35 : 1.0
    }

    // ---- The beacon encoding ------------------------------------------------------------

    /// Carrier frequencies. The enemy is the LOWER, duller sound; the EX core is higher and
    /// brighter. Two different timbres, so both stay identifiable when the pulse rate is
    /// the same — which is exactly the case that broke pulse-rate identity.
    private static let enemyHz: Float = 660
    private static let coreHz: Float = 1180

    /// Pulse rate from distance. Nearer is faster, with a floor and a ceiling so the cue
    /// stays usable at both ends: a 40 Hz buzz is a screech and a 0.5 Hz tick is a mystery.
    /// 45 world units is roughly a Dissidia arena's far corner, 4 units is melee.
    private static let farUnits: Float = 45
    private static let nearUnits: Float = 4
    private static let slowHz: Float = 1.6
    private static let fastHz: Float = 11

    /// The FIELD mode's own ranges. This map is far larger than a Dissidia arena — cities
    /// span a few thousand world units — so the same 45/4 band would sit at the fast end for
    /// the whole stage and tell the player nothing.
    private static let fieldFarUnits: Float = 2500
    private static let fieldNearUnits: Float = 60
    private static let fieldSlowHz: Float = 1.2
    private static let fieldFastHz: Float = 9

    /// The three field targets as three TIMBRES. The enemy is the dullest and the lowest,
    /// the ally the brightest: the thing you must kill should not sound like the thing you
    /// must protect.
    private static let fieldEnemyHz: Float = 520
    private static let fieldCityHz: Float = 780
    private static let fieldAllyHz: Float = 1120

    /// The FIELD beacon: a mode with no lock mechanic, where the player must STEER.
    ///
    /// ⛔ PAN CARRIES THE BEARING HERE, AND THAT IS THE ONE PLACE THE CENTRED RULE IS
    /// SUSPENDED. The centred rule exists because in Dissidia the game turns the player
    /// toward the lock, so panning would ask them to steer something already being steered.
    /// In field mode nothing steers for you: the player flies the map themselves, so the
    /// cue's whole job is to say WHICH WAY. Front/back cannot be panned, so it is carried by
    /// the rate: the pulse quickens as the target comes round to the front and slows as it
    /// goes behind, while pan says left or right across the full circle.
    ///
    /// `kind`: 1 enemy, 2 city, 3 ally — IDENTITY IN TIMBRE, so the three stay distinct at
    /// the same pulse rate. `headingLive` false means the bearing came from a stale heading:
    /// the cue keeps its distance information but drops the pan to centre, so it never sends
    /// the player confidently the wrong way.
    func updateFieldBeacon(kind: Int32, bearingDegrees: Float, distance: Float,
                           headingLive: Bool, enabled: Bool) {
        let v = voices[Family.beacon.rawValue]
        guard enabled, kind != 0 else {
            v.gain = 0
            v.pan = 0
            return
        }
        switch kind {
        case 1:  v.frequency = Self.fieldEnemyHz
        case 2:  v.frequency = Self.fieldCityHz
        default: v.frequency = Self.fieldAllyHz
        }

        // Pan: the bearing mapped onto the full circle. +90 degrees (right) is +1.
        // A stale heading centres the cue rather than lying about the direction.
        let bearing = headingLive ? bearingDegrees : 180
        let radians = bearing * Float.pi / 180
        v.pan = sin(radians)

        // Rate: distance AND front/back. Front is faster; behind is slower, with the same
        // floor and ceiling discipline as the lock cue so neither end becomes a screech or a
        // mystery.
        let d = max(Self.fieldNearUnits, min(Self.fieldFarUnits, distance))
        let t = (d - Self.fieldNearUnits) / (Self.fieldFarUnits - Self.fieldNearUnits)
        // ⛔ BOTH ARMS ANNOTATED: an unannotated `0.35 : 1.0` infers Double and promotes the
        // whole expression, which swiftc rejects against the Float property. The gate caught
        // this one; it would not have been visible by reading.
        let behind: Float = abs(normalizedBearing(bearingDegrees)) > 90 ? 0.35 : 1.0
        v.pulseHz = (Self.fieldFastHz + (Self.fieldSlowHz - Self.fieldFastHz) * t) * behind
        v.dutyCycle = 0.5

        // A gain floor, so a distant target is never silent — silence has to mean "no
        // target", or the player cannot tell the two apart.
        v.gain = 0.18 + 0.12 * (1 - t)
    }

    /// Signed bearing in -180..180, so "behind" is expressible.
    private func normalizedBearing(_ deg: Float) -> Float {
        var d = deg
        while d > 180 { d -= 360 }
        while d < -180 { d += 360 }
        return d
    }

    /// Update the beacon from one snapshot poll.
    ///
    /// `state` is poke_cue_snapshot()'s return: 0 nothing, 1 battle with no lock, 2 enemy,
    /// 3 EX core. Silence for 0 and 1 — a battle with no lock has nothing to point at.
    func updateBeacon(state: Int32, distance: Float, enabled: Bool) {
        let v = voices[Family.beacon.rawValue]
        guard enabled, state == 2 || state == 3 || state == 4 else {
            v.gain = 0
            return
        }
        // State 4 is the EX-gain PULSE: the core's own reachable signal (decomp proves the
        // lock field can never hold a core, so the old `state == 3` branch was silent
        // forever). Same high timbre, but it marks a pickup, which happens once.
        v.frequency = (state == 4) ? Self.coreHz : Self.enemyHz

        // Clamp the distance into the usable band before mapping it.
        let d = max(Self.nearUnits, min(Self.farUnits, distance))
        let t = (d - Self.nearUnits) / (Self.farUnits - Self.nearUnits)   // 0 near, 1 far
        v.pulseHz = Self.fastHz + (Self.slowHz - Self.fastHz) * t
        v.dutyCycle = 0.5

        // Gain rises slightly as the target closes, but never to zero at the far end —
        // silence there would be indistinguishable from "no lock".
        v.gain = 0.18 + 0.12 * (1 - t)
    }

    // ---- The one-shot family: health beeps and gauge-full earcons --------------
    //
    // The second cue shape beside the beacon (docs/design/cue-synth.md,
    // "One-shot event fields"). The beacon is a STATE the poll re-asserts every
    // frame; these are MOMENTS the adapter detected from memory edges and
    // delivered exactly once through poke_cue_health(). Four dedicated voices
    // sit OUTSIDE the family pool because two of them must sound at once -- a
    // traded blow fires the foe's and the player's beeps in the same frame,
    // and one voice can only hold one frequency. Their LEVELS still come from
    // the families: both hit beeps ride Family.health (one level, never per
    // side), and the gauge-full earcons ride Family.beacon, the readiness
    // family they belong to perceptually.

    private let foeHitVoice = CueVoice()
    private let ownHitVoice = CueVoice()
    private let focusCueVoice = CueVoice()
    private let dlinkCueVoice = CueVoice()
    private let guardBeatVoice = CueVoice()

    /// The recipes, from the design doc. Durations are the audible shape: a
    /// 35 ms bright tick for the foe, a 55 ms duller thud for the player, and
    /// ~60 ms centred blips for the gauges, in the beacon family's carrier
    /// register (the D-Link earcon reuses the EX core's 1180 Hz on purpose:
    /// "something is ready" sounds related wherever it comes from).
    private static let foeHitSeconds: Double = 0.035
    private static let ownHitSeconds: Double = 0.055
    private static let earconSeconds: Double = 0.060
    private static let focusHz: Float = 1560
    private static let dlinkHz: Float = 1180

    /// Load one voice with a one-shot. Phase restarts at zero so the sine
    /// starts at a zero crossing; the render envelope adds a ~1 ms attack
    /// ramp on top, so a trigger can never click.
    private func trigger(_ v: CueVoice, hz: Float, pan: Float, seconds: Double,
                         gain: Float, curve: Float, partial: Float) {
        v.frequency = hz
        v.pan = max(-1, min(1, pan))
        v.pulseHz = 0
        v.phase = 0
        v.partialPhase = 0
        v.shotCurve = curve
        v.partial = partial
        v.shotTotal = max(1, seconds * Self.sampleRate)
        v.shotLeft = v.shotTotal
        v.gain = gain
    }

    /// Voice one frame of one-shot events. `bits` is poke_cue_health()'s
    /// return: 1 foe beep, 2 own beep, 4 Focus full, 8 D-Link full.
    ///
    /// ⛔ THE PITCH IS PLAYED AS GIVEN, NEVER RE-MAPPED. The adapter already
    /// mapped the new health fraction to a frequency logarithmically (foe
    /// 1200→300 Hz, own 800→200 Hz); a second mapping here would compress
    /// the scale the player is learning. The pan likewise arrives with the
    /// event (+0.8 foe right, −0.8 own left).
    ///
    /// `enabled` is the battle-cues switch, the same one that gates the
    /// beacon. Off drops the events unheard: they are one-frame moments,
    /// and holding them for a later un-mute would voice a hit that is
    /// already over.
    func updateOneShots(bits: Int32, foeHz: Float, foePan: Float,
                        ownHz: Float, ownPan: Float, enabled: Bool) {
        guard enabled else { return }
        if bits & 1 != 0 {
            // Bright tick: pure sine, hard decay.
            trigger(foeHitVoice, hz: foeHz, pan: foePan, seconds: Self.foeHitSeconds,
                    gain: 0.5, curve: 2.2, partial: 0)
        }
        if bits & 2 != 0 {
            // Duller thud: a slower decay plus a second harmonic that
            // decays faster than the fundamental (see CueVoice.partial).
            trigger(ownHitVoice, hz: ownHz, pan: ownPan, seconds: Self.ownHitSeconds,
                    gain: 0.55, curve: 1.6, partial: 0.35)
        }
        if bits & 4 != 0 {
            trigger(focusCueVoice, hz: Self.focusHz, pan: 0, seconds: Self.earconSeconds,
                    gain: 0.4, curve: 1.4, partial: 0)
        }
        if bits & 8 != 0 {
            trigger(dlinkCueVoice, hz: Self.dlinkHz, pan: 0, seconds: Self.earconSeconds,
                    gain: 0.4, curve: 1.4, partial: 0)
        }
    }

    // ---- The guard window: a continuous rising sweep (Attack of the Saiyans) ----
    //
    // The third cue shape (docs/design/cue-synth.md's timing family, and
    // the Budokai Access model the design settled on): not a state to
    // point at, not a moment that already happened, but a window running
    // toward a moment — an enemy attack is inbound and there is an
    // instant at which a button press guards it. poke_cue_guard() is
    // the continuous counterpart to the one-shots: it returns 1 on
    // every frame the window is open, with `frac` running 0 (just
    // opened) to 1 (press NOW). The adapter has already normalized
    // each attack family's own timing into that fraction — a ranged
    // countdown, a heavy strike countdown, a light attack's fixed
    // flag lead all peak at frac 1 — so the synth's job is only to
    // make frac audible: a tone whose pitch climbs as frac runs, so
    // the PEAK IS the guard moment and the player times the press to
    // the top of the sweep. A soft low beat sounds on the single frame
    // the window (re)opens — the restart beat — so a new window never
    // has to be noticed mid-sweep. CENTRED, always: the window names
    // no target and no side (the descriptor's target byte is a
    // formation row, not a party slot — the adapter proved it cannot
    // be named honestly), so the cue must not pretend to one either.
    //
    // The sweep rides the pool's TICK voice (the timing family), so
    // its level is Family.tick's, one level like every family. The
    // beat is a dedicated one-shot voice for the same reason the hit
    // beeps are: the sweep is already sounding on its own voice when
    // the beat must land.

    /// Sweep endpoints. An exponential rise (300 → 900 Hz) so the
    /// climb is perceptually even, in a register below the beacon's
    /// carriers and above the health thud: a window must not be
    /// mistaken for either.
    private static let guardBaseHz: Float = 300
    private static let guardRiseRatio: Float = 3
    /// Gain rises slightly with frac but never starts at zero — a
    /// window whose opening is inaudible is a window half missed, and
    /// silence has to keep meaning "no window".
    private static let guardGainBase: Float = 0.30
    private static let guardGainRise: Float = 0.15
    /// The restart beat: a soft, low, short blip under the sweep's start.
    private static let guardBeatHz: Float = 160
    private static let guardBeatSeconds: Double = 0.045

    /// Voice one frame of the guard window. `frac` is
    /// poke_cue_guard()'s: 0 as the window opens, 1 at the guard
    /// moment; played as a pure function of frac, never re-shaped —
    /// the adapter owns the timing truth, the synth only renders it.
    /// `beat` is the window's opening frame. `enabled` is the same
    /// battle-cues switch as every cue here: off silences the sweep
    /// at once and drops the beat unheard — a window is a live moment,
    /// and voicing its opening late would mark a moment already gone.
    func updateGuard(active: Bool, frac: Float, beat: Bool, enabled: Bool) {
        let v = voices[Family.tick.rawValue]
        guard enabled, active else {
            v.gain = 0
            return
        }
        let f = max(0, min(1, frac))
        v.frequency = Self.guardBaseHz * powf(Self.guardRiseRatio, f)
        v.pulseHz = 0
        v.pan = 0
        v.gain = Self.guardGainBase + Self.guardGainRise * f
        if beat {
            trigger(guardBeatVoice, hz: Self.guardBeatHz, pan: 0,
                    seconds: Self.guardBeatSeconds, gain: 0.35, curve: 1.4, partial: 0)
        }
    }

    /// Clear the beacon when a LOCK-ON cue takes over from a field cue. WHY: both cues share
    /// the one beacon voice (one voice per FAMILY), so a field cue's pan would otherwise
    /// persist into a battle cue and pan a sound that is supposed to be centred.
    func silenceLockBeacon() { voices[Family.beacon.rawValue].pan = 0 }

    /// And the reverse: clear the pan when a field cue takes over from a lock cue.
    func silenceFieldBeacon() { voices[Family.beacon.rawValue].pan = 0 }

    /// Silence every family. Used when the game stops.
    func silenceAll() {
        for v in voices { v.gain = 0 }
        for v in [foeHitVoice, ownHitVoice, focusCueVoice, dlinkCueVoice, guardBeatVoice] {
            v.gain = 0
            v.shotLeft = 0
        }
    }

    // ---- The render side ----------------------------------------------------------------

    private static let sampleRate: Double = 32768

    /// One sample of one voice (before pan), advancing the voice's render state.
    ///
    /// Two envelope shapes share this path. A STATE voice (the beacon) pulses:
    /// the raised-cosine pulse envelope the beacon has always used. A ONE-SHOT
    /// voice (shotLeft > 0) decays: a ~1 ms attack ramp so the start cannot
    /// click, then a power curve down to zero, at which point the voice
    /// silences itself -- a triggered moment must end even if no later poll
    /// ever touches the voice again.
    private func voiceSample(_ v: CueVoice, gain g: Float, sr: Double) -> Float {
        var env: Float = 1
        if v.shotLeft > 0 {
            let frac = Float(v.shotLeft / v.shotTotal)            // 1 at start, 0 at end
            let attack = Float(min(1.0, (v.shotTotal - v.shotLeft) / (sr * 0.001)))
            env = attack * powf(frac, v.shotCurve)
            v.shotLeft -= 1
            if v.shotLeft <= 0 { v.shotLeft = 0; v.gain = 0 }
        } else if v.pulseHz > 0 {
            let period = Double(sr) / Double(v.pulseHz)
            let pos = v.pulsePhase.truncatingRemainder(dividingBy: period) / period
            let duty = Double(max(0.05, min(1.0, v.dutyCycle)))
            let ramp = 0.06                      // fraction of the period
            if pos < duty {
                let into = pos, out = duty - pos
                env = Float(min(1.0, min(into / ramp, out / ramp)))
            } else {
                env = 0
            }
            v.pulsePhase += 1
            if v.pulsePhase >= period { v.pulsePhase -= period }
        }

        var s = sinf(Float(v.phase) * 2 * .pi) * g * env
        if v.partial > 0 {
            // The harmonic gets the SAME envelope times one more decay factor,
            // so it is present at the hit and gone before the fundamental is --
            // that asymmetry is what makes the thud duller than the tick.
            let frac = Float(max(0, v.shotLeft / v.shotTotal))
            s += sinf(Float(v.partialPhase) * 2 * .pi) * g * env * v.partial * frac
            v.partialPhase += Double(v.frequency) * 2 / sr
            if v.partialPhase >= 1 { v.partialPhase -= 1 }
        }
        v.phase += Double(v.frequency) / sr
        if v.phase >= 1 { v.phase -= 1 }
        return s
    }

    /// Mix every voice into an interleaved stereo s16 buffer, ADDING to what is already
    /// there (the game's own audio). Called from the render block.
    ///
    /// `frames` is the frame count; the buffer is (L,R) pairs, matching the core's format.
    func render(into ptr: UnsafeMutablePointer<Int16>, frames: Int) {
        // The one-shot voices and the family level each rides (see the
        // one-shot section above for why these pairings).
        let oneShots: [(CueVoice, Family)] = [
            (foeHitVoice, .health), (ownHitVoice, .health),
            (focusCueVoice, .beacon), (dlinkCueVoice, .beacon),
            (guardBeatVoice, .tick),
        ]
        var anyAudible = false
        for v in voices where v.gain > 0 { anyAudible = true }
        for (v, _) in oneShots where v.gain > 0 { anyAudible = true }
        guard anyAudible else { return }

        // ⛔ EQUAL-POWER PAN, NOT A LINEAR CROSSFADE. A linear pan dips ~3 dB in the
        // middle, so a target directly ahead would sound quieter than one to the side
        // and read as "farther". The sqrt law holds the power constant across the arc.
        // A centred voice (pan 0) gets 0.707 on BOTH sides, which is the same loudness
        // as the old mono write once the 0.5 headroom below is applied.
        func addPanned(_ s: Float, pan: Float, _ mixL: inout Float, _ mixR: inout Float) {
            let p = max(-1, min(1, pan))
            let angle = (p + 1) * Float.pi / 4          // 0 .. pi/2
            mixL += s * cosf(angle)
            mixR += s * sinf(angle)
        }

        let sr = Self.sampleRate
        for f in 0..<frames {
            var mixL: Float = 0
            var mixR: Float = 0
            for (i, v) in voices.enumerated() {
                let g = v.gain * levels[i] * speechDuck
                guard g > 0 else { continue }
                addPanned(voiceSample(v, gain: g, sr: sr), pan: v.pan, &mixL, &mixR)
            }
            for (v, fam) in oneShots {
                let g = v.gain * levels[fam.rawValue] * speechDuck
                // Not `guard g > 0`: a shot in flight must count down and
                // self-silence even while its family is ducked or levelled
                // to zero, or the voice would hang armed (and keep render
                // out of its early exit) until the next trigger.
                guard v.shotLeft > 0 || g > 0 else { continue }
                addPanned(voiceSample(v, gain: g, sr: sr), pan: v.pan, &mixL, &mixR)
            }

            // Keep headroom: four voices at full gain would clip.
            // The 1/sqrt(2) here is the equal-power centre gain, so a centred voice is
            // neither louder nor quieter than it was before the pan was added.
            let k: Float = 0.3535534                          // 0.5 * 0.7071068
            let l = Int16(max(-1.0, min(1.0, mixL * k)) * 32767)
            let r = Int16(max(-1.0, min(1.0, mixR * k)) * 32767)
            ptr[f * 2]     = Int16(clamping: Int(ptr[f * 2])     + Int(l))
            ptr[f * 2 + 1] = Int16(clamping: Int(ptr[f * 2 + 1]) + Int(r))
        }
    }
}
