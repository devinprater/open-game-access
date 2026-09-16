import Foundation
import AVFoundation
import UIKit
import CPokeCore

/// SpeechEngine is the app's single speech channel.
///
/// Two things speak in this app and both go through here:
///   1. The accessibility script (main.lua), via the core's speech callback.
///      Its `say(text, interrupt)` becomes queue vs interrupt exactly as the
///      script meant it, so dialogue replaces itself and status lines stack.
///   2. The app's own UI announcements (ROM loaded, errors).
///
/// The `hermes_tts` global name is kept from the Android port: the compat shim
/// maps `speech.say` onto it, so the same shim text works on both platforms.
///
/// Two channels, chosen by whether VoiceOver is running:
///
///   * VoiceOver ON  — everything is posted as a VoiceOver announcement, so the
///     reader and VoiceOver share one voice and one queue instead of interrupting
///     each other. VoiceOver owns the audio session; a second synthesizer talking
///     over it gets cut off mid-word.
///   * VoiceOver OFF — AVSpeechSynthesizer speaks directly, which also keeps
///     working with the screen off, where an announcement would go nowhere.
///
/// Main-actor isolated: AVSpeechSynthesizer must be driven from the main
/// thread, and the core's speech callback arrives on the frame-loop thread.
@MainActor
final class SpeechEngine: NSObject, ObservableObject, AVSpeechSynthesizerDelegate {
    private let synth = AVSpeechSynthesizer()
    private let voice: AVSpeechSynthesisVoice?

    /// Mirror of the last thing spoken — the "repeat" command reads it back.
    @Published private(set) var lastSpoken: String = ""

    /// True after the script asked for silence (its R key / Stop speech): game
    /// text is dropped rather than queued, until the player asks for something.
    ///
    /// ⛔ THIS IS WHAT MAKES "STOP SPEECH" STICK. main.lua's own stop_speech()
    /// only reaches the platform; the platform is what has to keep quiet, because
    /// the script's readers go on producing lines every frame. Without it, the
    /// next line spoken a frame later makes the button look broken.
    private var isStopped = false

    /// Queue for lines that arrive before the audio session/synthesizer is
    /// usable; flushed in order so the loader line is never lost.
    private var pendingSpeech: [String] = []

    /// Set while VoiceOver is running.
    ///
    /// VoiceOver owns the audio session and, when a second synthesizer starts
    /// talking over it, speech gets cut off mid-word. So when VoiceOver is on,
    /// the core's per-frame chatter is routed through VoiceOver's own queue
    /// instead of AVSpeechSynthesizer. Without VoiceOver the synthesizer is
    /// used directly (and keeps working when the screen is off).
    private var voiceOverRunning: Bool {
        UIAccessibility.isVoiceOverRunning
    }

    override init() {
        // Prefer a compact voice: game dialogue arrives in short bursts and a
        // slow voice makes the script feel like it is lagging behind the game.
        let preferred = AVSpeechSynthesisVoice.speechVoices().first {
            $0.language.hasPrefix("en") && $0.quality != .premium
        }
        voice = preferred ?? AVSpeechSynthesisVoice(language: "en-US")
        super.init()
        synth.delegate = self
    }

    // MARK: - Script bridge (speech.say / speech.stop / hermes_tts)

    func speak(_ text: String, interrupt: Bool) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }

        // While silenced, automatic narration stays silent — the script's readers
        // produce lines every frame and letting them through is what would make
        // "Stop speech" look broken. A line the player just asked for arrives
        // with interrupt = true (the script's own contract for that: it uses
        // false for background text and true for "you asked for it, say it now"),
        // so it both resumes speech and is spoken immediately.
        if isStopped && !interrupt { return }
        isStopped = false

        lastSpoken = trimmed

        if voiceOverRunning {
            // Route through VoiceOver rather than AVSpeechSynthesizer, so the two
            // do not talk over each other.
            //
            // The queue/interrupt distinction has to be carried across, not
            // dropped. A plain String announcement always INTERRUPTS, which turns
            // the script's background narration into something that cuts off the
            // line the player just asked for — the opposite of the script's own
            // contract, where `interrupt` means "the player requested this". An
            // attributed string with `accessibilitySpeechQueueAnnouncement` is the
            // supported way to say "speak this after whatever is already
            // playing", so background text queues and requested text still jumps
            // the queue.
            announceThroughVoiceOver(trimmed, queue: !interrupt)
            return
        }

        if interrupt { synth.stopSpeaking(at: .immediate) }
        let utterance = AVSpeechUtterance(string: trimmed)
        utterance.voice = voice
        // The script already paces itself; a brisk rate keeps it in step with
        // dialogue boxes that appear a few frames apart.
        utterance.rate = AVSpeechUtteranceDefaultSpeechRate * 1.1
        utterance.postUtteranceDelay = 0
        synth.speak(utterance)
    }

    func stop() {
        stopAll()
    }

    /// Fully silenced: the synthesizer's queue is cleared and nothing further is
    /// spoken until new text arrives. Used for the script's "stop speech".
    func stopAll() {
        synth.stopSpeaking(at: .immediate)
        pendingSpeech.removeAll()
        isStopped = true
    }

    /// UI announcements (not game text) — always immediate and always spoken,
    /// even with VoiceOver on, because they are responses to the player's own
    /// actions and VoiceOver will not have read the changed control yet.
    func announce(_ text: String) {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        // An announcement is a reply to the player's own action, so it always
        // comes out and it lifts any silence the script asked for.
        isStopped = false
        lastSpoken = trimmed
        if voiceOverRunning {
            // A reply to the player's own action: it interrupts, because the
            // player is waiting for it and anything still speaking is stale.
            announceThroughVoiceOver(trimmed, queue: false)
        } else {
            speak(trimmed, interrupt: true)
        }
    }

    /// Post an announcement VoiceOver will speak, optionally behind whatever it
    /// is already saying.
    ///
    /// ⛔ The argument must be an NSAttributedString to carry the queue flag — a
    /// plain String is always an interrupting announcement, and the key has no
    /// effect on it.
    ///
    /// ⛔ AND VOICEOVER SILENTLY DROPS ANNOUNCEMENTS POSTED WHILE IT IS READING A
    /// FOCUSED ELEMENT'S LABEL. They are not queued, they are discarded. This is
    /// the reason announcements are not the only channel here: the game screen
    /// element's `accessibilityValue` carries the same state, so a line that is
    /// dropped can still be reached by re-focusing the screen rather than being
    /// lost outright.
    private func announceThroughVoiceOver(_ text: String, queue: Bool) {
        let announcement = NSAttributedString(
            string: text,
            attributes: [.accessibilitySpeechQueueAnnouncement: queue]
        )
        UIAccessibility.post(notification: .announcement, argument: announcement)
    }

    // MARK: - Audio session
    //
    // Game audio (melonds core) plays through AVAudioEngine; the session is
    // configured once, and is set to mix with VoiceOver so ducking never
    // silences the game permanently.

    func configureAudioSession() {
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try session.setActive(true)
        } catch {
            // Not fatal: the game runs silently rather than not at all.
            NSLog("[poke] audio session unavailable: \(error.localizedDescription)")
        }
    }
}
