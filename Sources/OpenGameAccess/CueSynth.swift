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
            var mix: Float = 0
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

                mix += sinf(Float(v.phase) * 2 * .pi) * g * env
                v.phase += Double(v.frequency) / sr
                if v.phase >= 1 { v.phase -= 1 }
            }

            // Keep headroom: four voices at full gain would clip.
            let s = max(-1.0, min(1.0, mix * 0.5))
            let sample = Int16(s * 32767)
            ptr[f * 2]     = Int16(clamping: Int(ptr[f * 2])     + Int(sample))
            ptr[f * 2 + 1] = Int16(clamping: Int(ptr[f * 2 + 1]) + Int(sample))
        }
    }
}
