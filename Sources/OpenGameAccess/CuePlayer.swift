//
//  CuePlayer.swift — plays the reader's own WAV cues, with pan.
//
//  WHAT THIS CLOSES. Pokémon Access conveys DIRECTION with sound: 42 `audio.play`
//  sites pan a WAV to an obstacle's side (gb.lua:230 plays a boulder sound panned to
//  where the boulder is). Under mGBA that has never been audible on any platform —
//  the original `audio.dll` is a 32-bit Windows BASS binary that cannot load into
//  mGBA's Lua, so `oga_audio.lua` is a stub that records cues and drops them.
//
//  The fix is not to emulate BASS in Lua. It is to play the WAV files from the HOST
//  and apply the pan there, which is exactly what the Android bridge already does via
//  its JNI sound callback. This is the iOS counterpart.
//
//  ⛔ MIXED FORMATS IN THE READER'S OWN TREE, so nothing here may assume one layout.
//  Measured across the 33 bundled WAVs: 44100 Hz, mono 8-bit and 16-bit, and stereo
//  16-bit all occur (s_grass.wav is stereo 16-bit/0.86 s; menusel.wav is mono
//  8-bit/0.08 s). AVAudioPlayer handles all of them; the engine's own 32768 Hz format
//  does not, so these must NOT be fed into the emulator's render graph.
//
//  ⛔ SEPARATE PLAYBACK FROM THE GAME'S AUDIO PATH. The session's AVAudioEngine is a
//  realtime render graph on the audio thread; decoding a WAV into it would allocate
//  and decode there. Cues are short and infrequent, so one AVAudioPlayer per cue,
//  started from the main thread, keeps the render path clean. They mix at the OS
//  level, which is also what lets a cue play over the game without stopping it.
//
import AVFoundation
import Foundation

/// Plays the reader's positional WAV cues. Main-thread only, like the rest of the
/// app's state.
final class CuePlayer {
    /// One reused player per distinct path. The reader replays the same handful of
    /// files constantly (grass, wall, boulder, menusel), so caching by path avoids
    /// decoding on every step — `gb.lua` can fire a cue per row of a menu.
    private var players: [String: AVAudioPlayer] = [:]

    /// The directory the reader resolves its own relative paths against. Set once the
    /// reader set is located; cues are dropped until then rather than guessed at.
    private(set) var scriptDir: String?

    /// Cue level, 0 to 1. ONE LEVEL FOR ALL CUES: they carry direction, not identity,
    /// so a per-file level could make two sides of the same obstacle disagree.
    var level: Float = 0.8

    /// Paths whose play() was refused, so the warning is said once each rather than on
    /// every step of a walk.
    private var warned: Set<String> = []

    func setScriptDir(_ dir: String?) {
        guard dir != scriptDir else { return }
        scriptDir = dir
        players.removeAll()   // stale paths from the previous set are meaningless
    }

    /// Play one cue. `path` arrives with the reader's own separators (it builds them
    /// with backslashes because it was written for Windows), so normalise before use —
    /// the same normalisation the GBA host applies to reader paths.
    func play(path: String, pan: Int, volume: Int) {
        guard let fileURL = resolve(path) else { return }

        let player: AVAudioPlayer
        if let cached = players[path] {
            player = cached
        } else {
            guard let made = try? AVAudioPlayer(contentsOf: fileURL) else {
                // A cue that will not decode must not stop the reader; log once and
                // remember the failure by not caching, so a later fix is picked up.
                NSLog("[cue] could not decode \(fileURL.lastPathComponent)")
                return
            }
            // ⛔ ACTIVATE THE SESSION, DO NOT ASSUME IT. AVAudioPlayer.play() returns
            // false and plays NOTHING when the audio session is inactive. The speech
            // engine activates the shared session at attach time, so in the app this is
            // normally already true — but that ordering is invisible from here, and a
            // cue that silently does nothing is the exact bug this file exists to fix.
            // Cheap to check, and it makes this file work on its own.
            if !AVAudioSession.sharedInstance().isOtherAudioPlaying {
                try? AVAudioSession.sharedInstance().setActive(true)
            }
            made.prepareToPlay()
            players[path] = made
            player = made
        }

        player.pan = Float(max(-100, min(100, pan))) / 100.0
        player.volume = level * (Float(max(0, min(100, volume))) / 100.0)
        // ⛔ RESTART, DO NOT SKIP. A cue repeating while it is still playing is the
        // normal case (walking repeats a footstep cue); letting it play through would
        // drop every cue after the first, and `pan` is only read when playback starts.
        player.currentTime = 0
        // ⛔ CHECK THE RESULT. play() returns false instead of throwing, so a cue that
        // could not start would be indistinguishable from one that played. Say so once
        // per file rather than failing in silence, which is what took the original gap
        // so long to notice.
        if !player.play(), !warned.contains(path) {
            warned.insert(path)
            NSLog("[cue] play() refused \\(fileURL.lastPathComponent) — session inactive?")
        }
    }

    func stopAll() {
        for p in players.values where p.isPlaying { p.stop() }
        players.removeAll()
    }

    /// Resolve the reader's relative path against the reader directory. The reader
    /// joins `scriptpath` itself, so the string looks like
    /// "…/gba-lua/sounds\gba\s_grass.wav" — or on a host that rewrote the prefix, a
    /// bare "sounds\gba\s_grass.wav". Both are handled.
    private func resolve(_ path: String) -> URL? {
        let fm = FileManager.default
        // The reader uses backslashes; some of its own boot code rewrites to '/'.
        let normalised = path.replacingOccurrences(of: "\\", with: "/")

        // Case 1: an absolute path already (the reader normally builds one).
        if normalised.hasPrefix("/"), fm.fileExists(atPath: normalised) {
            return URL(fileURLWithPath: normalised)
        }
        // Case 2: relative to the reader directory.
        if let dir = scriptDir {
            let candidate = (dir as NSString).appendingPathComponent(normalised)
            if fm.fileExists(atPath: candidate) { return URL(fileURLWithPath: candidate) }
        }
        return nil
    }
}
