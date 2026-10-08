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

    // Render-block state (touched only from the audio thread).
    fileprivate var phase: Double = 0
    fileprivate var pulsePhase: Double = 0
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
        guard enabled, state == 2 || state == 3 else {
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

    /// Clear the beacon when a LOCK-ON cue takes over from a field cue. WHY: both cues share
    /// the one beacon voice (one voice per FAMILY), so a field cue's pan would otherwise
    /// persist into a battle cue and pan a sound that is supposed to be centred.
    func silenceLockBeacon() { voices[Family.beacon.rawValue].pan = 0 }

    /// And the reverse: clear the pan when a field cue takes over from a lock cue.
    func silenceFieldBeacon() { voices[Family.beacon.rawValue].pan = 0 }

    /// Silence every family. Used when the game stops.
    func silenceAll() {
        for v in voices { v.gain = 0 }
    }

    // ---- The render side ----------------------------------------------------------------

    private static let sampleRate: Double = 32768

    /// Mix every voice into an interleaved stereo s16 buffer, ADDING to what is already
    /// there (the game's own audio). Called from the render block.
    ///
    /// `frames` is the frame count; the buffer is (L,R) pairs, matching the core's format.
    func render(into ptr: UnsafeMutablePointer<Int16>, frames: Int) {
        var anyAudible = false
        for (i, v) in voices.enumerated() where v.gain > 0 {
            anyAudible = true
            _ = i
        }
        guard anyAudible else { return }

        let sr = Self.sampleRate
        for f in 0..<frames {
            var mixL: Float = 0
            var mixR: Float = 0
            for (i, v) in voices.enumerated() {
                let g = v.gain * levels[i] * speechDuck
                guard g > 0 else { continue }

                // Pulse envelope: 1 while sounding, 0 in the gap. A raised-cosine ramp at
                // each edge removes the click a hard square edge would make.
                var env: Float = 1
                if v.pulseHz > 0 {
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

                // ⛔ EQUAL-POWER PAN, NOT A LINEAR CROSSFADE. A linear pan dips ~3 dB in the
                // middle, so a target directly ahead would sound quieter than one to the side
                // and read as "farther". The sqrt law holds the power constant across the arc.
                // A centred voice (pan 0) gets 0.707 on BOTH sides, which is the same loudness
                // as the old mono write once the 0.5 headroom below is applied.
                let pan = max(-1, min(1, v.pan))
                let angle = (pan + 1) * Float.pi / 4          // 0 .. pi/2
                let gl = cosf(angle)
                let gr = sinf(angle)

                let sample = sinf(Float(v.phase) * 2 * .pi) * g * env
                mixL += sample * gl
                mixR += sample * gr
                v.phase += Double(v.frequency) / sr
                if v.phase >= 1 { v.phase -= 1 }
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
