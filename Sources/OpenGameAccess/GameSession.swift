import Foundation
import AVFoundation
import SwiftUI
import CPokeCore

/// GameSession owns the emulator core and drives the frame loop.
///
/// The Lua accessibility script runs *inside* the core and is resumed once per
/// frame by the core's own `_Update()` hook, so the script sees the same
/// once-per-game-loop cadence it sees on BizHawk. That is what keeps the
/// timing of speech and synthetic touch identical to the desktop original.
@MainActor
final class GameSession: ObservableObject {
    enum Status: Equatable {
        case idle
        case needROM
        case ready(name: String)
        case running(name: String)
        case failed(String)
    }

    @Published private(set) var status: Status = .idle
    @Published private(set) var romName: String?
    @Published var showDebugLog = false
    @Published private(set) var debugLines: [String] = []
    @Published var audioEnabled = true {
        didSet { poke_set_audio_enabled(core, audioEnabled) }
    }

    /// Which DS screen is rendered large. The accessibility script reads both,
    /// but the player is looking at whichever one matters right now.
    @Published var focusScreen: Int32 = POKE_SCREEN_BOTTOM

    private var core: OpaquePointer?
    private weak var speech: SpeechEngine?
    private var displayLink: CADisplayLink?
    private var frameImage: CGImage?
    private var audioEngine: AVAudioEngine?
    private var audioSource: AudioSourceNode?

    /// The accessibility script the app ships with: the BizHawk→melonDS compat
    /// shim followed by the player's main.lua loader.
    private static var bundledScript: String? {
        guard let shim = BundleResources.text(named: "bizhawk_compat", ext: "lua"),
              let main = BundleResources.text(named: "main", ext: "lua") else { return nil }
        return shim + "\n" + main
    }

    init() {
        core = poke_create()
        guard let core else {
            status = .failed("Could not start the emulator core.")
            return
        }
        poke_set_speech_callback(core, GameSession.speechCallback,
                                 Unmanaged.passUnretained(speechPlaceholder).toOpaque())

        poke_set_log_callback(core, { text, userdata in
            guard let text, let userdata else { return }
            let session = Unmanaged<GameSession>.fromOpaque(userdata).takeUnretainedValue()
            let line = String(cString: text)
            Task { @MainActor in session.appendDebug(line) }
        }, Unmanaged.passUnretained(self).toOpaque())

        if let script = Self.bundledScript {
            poke_set_script(core, script)
        }
        // Let the on-screen pad talk to this session's core.
        InputBridge.connect(self)
        status = .needROM
    }

    /// Placeholder used only until `attach(to:)` supplies the real engine, so
    /// the callbacks can be installed in `init` before the environment object
    /// exists. Speech is redirected on attach.
    private let speechPlaceholder = SpeechEngine()

    // MARK: - Input

    func setButton(_ raw: Int32, down: Bool) {
        guard let core else { return }
        poke_set_button(core, raw, down)
    }

    func setHotkey(_ key: String, down: Bool) {
        guard let core else { return }
        key.withCString { poke_set_hotkey(core, $0, down) }
    }

    /// The native speech callback, shared by `init` and `attach(to:)` so the two
    /// cannot drift apart.
    ///
    /// ⛔ A NULL `text` IS NOT A NO-OP — it is the core's "stop talking now",
    /// sent for the script's R key and the Stop speech button. Swallowing it
    /// (as `guard let text` alone does) leaves a blind player with no way to
    /// silence the game, which is the one control they need most.
    private static let speechCallback: @convention(c)
        (UnsafePointer<CChar>?, Bool, UnsafeMutableRawPointer?) -> Void = { text, interrupt, userdata in
        guard let userdata else { return }
        let engine = Unmanaged<SpeechEngine>.fromOpaque(userdata).takeUnretainedValue()
        guard let text else {
            Task { @MainActor in engine.stop() }
            return
        }
        let string = String(cString: text)
        // Hop to the main actor: AVSpeechSynthesizer must be driven there, and
        // the core calls this from the frame-loop thread.
        Task { @MainActor in engine.speak(string, interrupt: interrupt) }
    }

    func attach(to speech: SpeechEngine) {
        self.speech = speech
        // Re-point the native speech callback at the environment's engine.
        guard let core else { return }
        poke_set_speech_callback(core, GameSession.speechCallback,
                                 Unmanaged.passUnretained(speech).toOpaque())
        speech.configureAudioSession()
    }

    private func appendDebug(_ line: String) {
        debugLines.append(line)
        if debugLines.count > 400 { debugLines.removeFirst(debugLines.count - 400) }
    }

    // MARK: - ROM

    func loadROM(at url: URL) {
        guard let core else { return }
        let accessed = url.startAccessingSecurityScopedResource()
        defer { if accessed { url.stopAccessingSecurityScopedResource() } }

        // Copy into the app's own container: the emulator keeps the file open
        // for random access and security-scoped URLs are not stable enough for
        // that across launches.
        let name = url.lastPathComponent
        guard let local = try? ROMStore.importROM(from: url) else {
            status = .failed("Could not read \(name).")
            return
        }

        let save = ROMStore.savePath(for: local)
        if poke_load_rom(core, local.path, save.path) {
            romName = name
            status = .ready(name: name)
            speech?.announce("\(name) loaded. Choose Start Game to begin.")
        } else {
            let message = String(cString: poke_last_error(core))
            status = .failed(message)
            speech?.announce(message)
        }
    }

    // MARK: - Run loop

    func start() {
        guard let core, case .ready = status else { return }
        guard poke_start(core) else {
            let message = String(cString: poke_last_error(core))
            status = .failed(message)
            speech?.announce(message)
            return
        }
        status = .running(name: romName ?? "game")
        startDisplayLink()
        if audioEnabled { startAudio() }
    }

    func stop() {
        guard let core else { return }
        poke_stop(core)
        displayLink?.invalidate()
        displayLink = nil
        stopAudio()
        if let romName { status = .ready(name: romName) } else { status = .needROM }
    }

    private func startDisplayLink() {
        displayLink?.invalidate()
        let link = CADisplayLink(target: self, selector: #selector(tick))
        // 60 Hz: one emulated frame per screen refresh, which is the DS's own
        // rate. The accessibility script assumes one update per game frame.
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: 60, preferred: 60)
        link.add(to: .main, forMode: .common)
        displayLink = link
    }

    @objc private func tick() {
        guard let core else { return }
        if !poke_frame(core) {
            let message = String(cString: poke_last_error(core))
            stop()
            if !message.isEmpty {
                status = .failed(message)
                speech?.announce(message)
            }
            return
        }
        refreshFramebuffer()
    }

    private func refreshFramebuffer() {
        guard let core else { return }
        var w: Int32 = 0
        var h: Int32 = 0
        guard poke_framebuffer(core, Int32(focusScreen), &w, &h), w > 0, h > 0 else { return }
        // poke_framebuffer hands back a RGBA8888 buffer owned by the core.
        guard let buffer = poke_framebuffer_ptr(core, focusScreen) else { return }
        let bytesPerRow = Int(w) * 4
        let data = Data(bytes: buffer, count: bytesPerRow * Int(h))
        guard let provider = CGDataProvider(data: data as CFData) else { return }
        frameImage = CGImage(
            width: Int(w), height: Int(h),
            bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: bytesPerRow,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false,
            intent: .defaultIntent
        )
        objectWillChange.send()
    }

    var currentFrame: CGImage? { frameImage }

    // MARK: - Audio

    private func startAudio() {
        guard let core else { return }
        let engine = AVAudioEngine()
        guard let format = AVAudioFormat(standardFormatWithSampleRate: 32768, channels: 2) else { return }
        let source = AudioSourceNode(core: core, format: format)
        engine.attach(source)
        engine.connect(source, to: engine.mainMixerNode, format: format)
        do {
            try engine.start()
            audioEngine = engine
            audioSource = source
        } catch {
            NSLog("[poke] audio engine failed: \(error.localizedDescription)")
        }
    }

    private func stopAudio() {
        audioEngine?.stop()
        audioEngine = nil
        audioSource = nil
    }

    // MARK: - Savestates

    func saveState() {
        guard let core else { return }
        let path = ROMStore.statePath.path
        if poke_save_state(core, path) {
            speech?.announce("State saved.")
        } else {
            speech?.announce("Could not save state.")
        }
    }

    func loadState() {
        guard let core else { return }
        let path = ROMStore.statePath.path
        guard FileManager.default.fileExists(atPath: path) else {
            speech?.announce("No saved state yet.")
            return
        }
        if poke_load_state(core, path) {
            speech?.announce("State loaded.")
        } else {
            speech?.announce("Could not load state.")
        }
    }
}

/// Pulls s16 stereo frames out of the emulator core for AVAudioEngine.
private final class AudioSourceNode: AVAudioSourceNode {
    private let core: OpaquePointer

    init(core: OpaquePointer, format: AVAudioFormat) {
        self.core = core
        super.init(format: format) { _, _, frameCount, audioBufferList -> OSStatus in
            let abl = UnsafeMutableAudioBufferListPointer(audioBufferList)
            guard let first = abl.first, let raw = first.mData else { return noErr }
            let ptr = raw.assumingMemoryBound(to: Int16.self)
            let frames = Int(frameCount)
            let got = Int(poke_read_audio(core, ptr, Int32(frames)))
            if got < frames {
                // Underrun: pad with silence rather than repeating a stale buffer.
                memset(ptr + got * 2, 0, (frames - got) * 2 * MemoryLayout<Int16>.size)
            }
            return noErr
        }
    }
}
