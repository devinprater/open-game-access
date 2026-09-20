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

    /// ⛔ THIS MUST BE @Published, NOT COMPUTED. Whether an adapter is ready is read
    /// from live C state, but SwiftUI only re-renders when an @Published property
    /// changes. A computed `adapterReady` would read the correct value and still never
    /// re-draw, so the reader controls would silently never appear on their own —
    /// which is indistinguishable from the feature not existing.
    ///
    /// It is refreshed once per frame in `tick()` (see `refreshAdapterState`), which
    /// costs one C call per frame and only assigns (and so only republishes) when the
    /// value actually changes.
    @Published private(set) var adapterReady = false
    /// The adapter's name and id, likewise published so the UI can show and label the
    /// reader group the moment a game with a native reader finishes loading.
    @Published private(set) var adapterName: String?
    @Published private(set) var adapterID: String?
    /// The loaded ROM's four-letter game code (header 0x0C), e.g. "IRBO" for
    /// Pokémon Black or "YFEE" for Fire Emblem USA. Read once per ROM like the
    /// adapter id. The UI uses it to show the Lua script's buttons only for the
    /// games the script actually knows (Pokémon Black/White 1) instead of
    /// offering dead controls for everything else.
    @Published private(set) var romGameCode: String?

    /// True only for the ROMs the bundled Lua script can narrate: Pokémon
    /// Black/White 1. main.lua's detect_game() knows IRAO/IRBO by header (plus
    /// a ROM-name fallback the core cannot see); every other game gets
    /// "unknown" and the script stays silent, so its buttons must stay hidden.
    var isPokemonROM: Bool {
        romGameCode == "IRAO" || romGameCode == "IRBO"
    }

    private var core: OpaquePointer?
    private weak var speech: SpeechEngine?
    private var displayLink: CADisplayLink?
    private var frameImage: CGImage?
    private var audioEngine: AVAudioEngine?
    private var audioSource: AudioSourceNode?

    /// The emulation core's output sample rate.
    ///
    /// Must equal `args.OutputSampleRate` in Core/pokecore.cpp. It is duplicated
    /// here because it is a contract between two languages with no shared
    /// constant — but a mismatch is audible (pitch shift), not fatal, which is
    /// why the render block also logs the format it was actually handed.
    private static let audioSampleRate: Double = 32768

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
        // A new ROM is a new adapter: clear the identity so the next frame re-reads it.
        // Without this, loading a second game would keep showing the first one's
        // reader controls until the app restarted.
        adapterID = nil
        adapterName = nil
        adapterReady = false
        romGameCode = nil

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
        adapterReady = false
        if let romName { status = .ready(name: romName) } else { status = .needROM }
    }

    /// Leave the running game, saving first.
    ///
    /// Saving is not a courtesy here, it is the difference between quitting and
    /// losing progress: this tool is driven by VoiceOver, so closing it is one
    /// gesture away at all times and an unsaved quit would silently discard
    /// whatever the player just did. The state goes to the same slot the manual
    /// Save state control uses, so a player can also get back to it by loading
    /// the game again.
    ///
    /// Deliberately does NOT go through `saveState()`, which announces on the
    /// main-actor path for a user-initiated save; here the two messages are
    /// combined into one so quitting does not produce two overlapping
    /// announcements.
    func quit() {
        guard let core, case .running = status else { return }

        let path = ROMStore.statePath.path
        let saved = poke_save_state(core, path)

        stop()

        // Report the outcome, because silence after a destructive-looking action
        // is indistinguishable from the app having crashed.
        speech?.announce(saved ? "Game closed. Progress saved." : "Game closed. Could not save.")
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
        refreshAdapterState()
    }

    // MARK: - Game adapters (native readers)

    /// ⛔ THE ADAPTER CONTROLS WERE UNREACHABLE UNTIL NOW. `poke_command` existed in
    /// the core and every adapter was written, registered and linked — but nothing in
    /// this app ever called it, so no native reader could fire for any game. That is
    /// why Fire Emblem never spoke despite compiling cleanly: the plumbing stopped one
    /// step short of the UI. These three members are that missing step.
    ///
    /// The adapter is the game's own semantic reader (the map, the party, the units).
    /// It is separate from the Lua script, and a game may have neither, one, or both.

    /// Read the adapter's identity and readiness out of the core and publish any
    /// change. Called once per frame from `tick()`.
    ///
    /// ⛔ ONLY ASSIGN WHEN THE VALUE CHANGES. A bare assignment to an @Published
    /// property fires objectWillChange on EVERY frame, which re-runs the whole view
    /// body 60 times a second while a game is running — the kind of cost that shows up
    /// as audio crackle and dropped frames, and is very hard to trace back here.
    ///
    /// The name and id are read once (on the first frame after a ROM loads) rather
    /// than every frame: they are fixed for the life of the ROM, and the C accessors
    /// return pointers to static storage.
    private func refreshAdapterState() {
        guard let core else { return }

        if adapterID == nil, let c = poke_adapter_id(core) {
            let s = String(cString: c)
            if !s.isEmpty { adapterID = s }
        }
        if adapterName == nil, let c = poke_adapter_name(core) {
            let s = String(cString: c)
            if !s.isEmpty { adapterName = s }
        }
        if romGameCode == nil, let c = poke_game_code(core) {
            let s = String(cString: c)
            if !s.isEmpty { romGameCode = s }
        }

        let ready = poke_adapter_ready(core)
        if ready != adapterReady { adapterReady = ready }
    }

    /// The reader controls that make sense for the loaded game. Empty is the normal
    /// answer for a ROM with no native reader, and the UI shows nothing in that case.
    var availableAdapterCommands: [AdapterCommand] {
        AdapterCommand.supported(adapterID: adapterID)
    }

    /// Send one accessibility command to the game's native reader.
    ///
    /// The raw command ids match `oga::Command` in Core/adapter.h. They are written
    /// out here rather than passed as raw integers from the view so the mapping is
    /// in one place and a wrong number cannot silently address a different command.
    func sendAdapterCommand(_ command: AdapterCommand) {
        guard let core else { return }
        guard poke_command(core, command.rawValue) else {
            // Refused, not crashed: either this game has no adapter, or its state is
            // not ready yet. Saying so is the honest report — silence here reads as
            // a broken button.
            speech?.announce(adapterName == nil
                             ? "This game has no reader controls."
                             : "Game state is not ready yet.")
            return
        }
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

        // ⛔ THE FORMAT MUST MATCH WHAT THE CORE ACTUALLY PRODUCES, AND IT DID NOT.
        //
        // pokecore.h says it plainly: "Audio: interleaved stereo s16."
        // `standardFormatWithSampleRate:channels:` does not mean "the standard
        // way to ask for audio" — the SDK header defines it as "deinterleaved
        // float with the specified sample rate and channel count". So the engine
        // was handed a deinterleaved-float32 format while the render block wrote
        // interleaved int16 into it.
        //
        // The result is not merely wrong audio, it is LOUD:
        //   * each 4-byte group of int16 samples [L_lo,L_hi,R_lo,R_hi] was read
        //     as ONE float32, so the exponent came from the right channel's high
        //     byte. Ordinary music levels put that byte in the 0x70-0x7F range,
        //     giving values ~2^96 to 2^127 times full scale — clamped to maximum
        //     output and held there;
        //   * nothing ever wrote the RIGHT channel's buffer. A deinterleaved
        //     stereo format has TWO buffers and the render block only filled the
        //     first, so that channel played uninitialised memory.
        //
        // Declaring int16 + interleaved makes the buffer layout exactly what
        // `poke_read_audio` writes: one buffer, (L,R) pairs, 2 bytes each.
        guard let format = AVAudioFormat(
            commonFormat: .pcmFormatInt16,
            sampleRate: Self.audioSampleRate,
            channels: 2,
            interleaved: true
        ) else {
            NSLog("[poke] could not build the int16 stereo format; audio disabled")
            return
        }

        // Prove the layout at runtime rather than trusting the request: a silent
        // mismatch here is what produced the full-volume blast, and the engine
        // will happily accept a format the render block cannot fill correctly.
        assert(format.commonFormat == .pcmFormatInt16 && format.isInterleaved,
               "audio format drifted from the core's interleaved-int16 contract")
        NSLog("[poke] audio format: \(format) interleaved=\(format.isInterleaved)")

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
///
/// Audio production and consumption are driven by two different clocks, and the
/// render block is where they meet:
///
///   * PRODUCTION — `poke_frame()` on the CADisplayLink (main thread), which
///     advances the emulator one 60 Hz frame and thereby makes ~546 samples.
///   * CONSUMPTION — this block, called by the audio thread at the declared
///     32768 Hz, also ~546 samples per frame.
///
/// Those rates match on average, so there is no drift. But the buffer only holds
/// ~62 ms (melonDS sizes it at 2048 frames), so any stall longer than that on the
/// main thread — a slow emulated frame, a VoiceOver utterance, a SwiftUI update —
/// empties it. That is the crackling: not a format problem, but the buffer
/// running dry under jitter.
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
                // ⛔ DO NOT HARD-ZERO THE TAIL. A jump from full-scale audio
                // straight to 0 in the middle of a waveform IS the click — the
                // discontinuity is what is heard, not the missing samples. Fading
                // the last real sample out over the gap removes the transient, so
                // a starved buffer sounds like a brief drop in volume instead of a
                // crackle. (memset here was the audible symptom.)
                AudioSourceNode.fadeOutTail(ptr, from: got, to: frames)
            }
            return noErr
        }
    }

    /// Ramp the unwritten remainder from the last produced sample down to zero.
    ///
    /// `static` because the render block runs before `self` is fully
    /// initialised — a `super.init` argument cannot capture `self`.
    private static func fadeOutTail(_ ptr: UnsafeMutablePointer<Int16>, from got: Int, to frames: Int) {
        let missing = frames - got
        guard missing > 0 else { return }
        let lastL = got > 0 ? ptr[(got - 1) * 2] : 0
        let lastR = got > 0 ? ptr[(got - 1) * 2 + 1] : 0
        for j in 0..<missing {
            // 1.0 at the first missing frame -> 0.0 at the last.
            let gain = Float(missing - j) / Float(missing)
            ptr[(got + j) * 2] = Int16(Float(lastL) * gain)
            ptr[(got + j) * 2 + 1] = Int16(Float(lastR) * gain)
        }
    }
}
