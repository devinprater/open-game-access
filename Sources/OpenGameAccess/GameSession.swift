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

    /// The lock-on beacon cue (item 5). A separate switch from game sound on purpose: a
    /// player may want the beacon with the game muted, or the game's music without a tone
    /// over it. Off while the feature is new — the skill's own rule is to default a new
    /// feature off in development and verify the effective config at handoff.
    ///
    /// ⛔ PERSISTED. A cue the player switched off and which came back after a restart is
    /// worse than one they never found.
    @Published var cueBeaconEnabled: Bool =
        UserDefaults.standard.object(forKey: "cueBeacon") as? Bool ?? false {
        didSet {
            UserDefaults.standard.set(cueBeaconEnabled, forKey: "cueBeacon")
            if !cueBeaconEnabled { CueSynth.shared.silenceAll() }
            speech?.announce(cueBeaconEnabled ? "Lock cue on." : "Lock cue off.")
        }
    }

    /// Which DS screen is rendered large. The accessibility script reads both,
    /// but the player is looking at whichever one matters right now.
    @Published var focusScreen: Int32 = POKE_SCREEN_BOTTOM

    /// The console the loaded ROM runs on. Set from the file extension at
    /// load; the pad, the reader groups, and the Settings screen picker all
    /// read it. Nil before the first load — nothing system-specific shows
    /// until a game is in.
    @Published private(set) var system: GameSystem?

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

    /// Name of the bundled reader set staged for this ROM ("Zelda1Access"), or nil when
    /// no bundled reader covers this game. Set from the core's own answer (the ROM's
    /// CRC32, never its filename), so the UI reports what was actually attached. Nil is
    /// the normal case for an unknown dump or a translation patch.
    @Published private(set) var readerSetName: String?
    /// The loaded ROM's four-letter game code (header 0x0C), e.g. "IRBO" for
    /// Pokémon Black or "YFEE" for Fire Emblem USA. Read once per ROM like the
    /// adapter id. The UI uses it to show the Lua script's buttons only for the
    /// games the script actually knows (Pokémon Black/White 1) instead of
    /// offering dead controls for everything else.
    @Published private(set) var romGameCode: String?

    /// True only for the ROMs the bundled Lua script can narrate: Pokémon
    /// Black/White 1. The bundled loader's GAMES table knows IRAO/IRBO by
    /// header (plus a ROM-name fallback); every other game gets "unknown"
    /// and the script stays silent, so its buttons must stay hidden.
    var isPokemonROM: Bool {
        romGameCode == "IRAO" || romGameCode == "IRBO"
    }

    private var core: OpaquePointer?
    private weak var speech: SpeechEngine?
    private var displayLink: CADisplayLink?
    /// The emulated screen, published on its OWN object: only ScreenView
    /// observes it, so a new frame redraws the picture and nothing else.
    let frames = FrameStore()
    private var audioEngine: AVAudioEngine?
    /// Plays the reader's own WAV cues with pan applied. Separate from the

    /// engine because the reader's files are 8/16-bit at 44100 Hz, not the

    /// engine's 32768 Hz int16, so they cannot share a render graph.

    private let cuePlayer = CuePlayer()
    private var audioSource: AudioSourceNode?
    private var thermalObserver: NSObjectProtocol?

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
        poke_set_speech_id_callback(core, GameSession.speechCallback,
                                    Unmanaged.passUnretained(speechPlaceholder).toOpaque())

        poke_set_log_callback(core, { text, userdata in
            guard let text, let userdata else { return }
            let session = Unmanaged<GameSession>.fromOpaque(userdata).takeUnretainedValue()
            let line = String(cString: text)
            Task { @MainActor in session.appendDebug(line) }
        }, Unmanaged.passUnretained(self).toOpaque())

        // The reader's positional cues (audio.play): 42 sites that pan a WAV to an
        // obstacle's side. oga_audio.lua forwards them here through _G.oga_play_sound.
        // Same contract as the Android bridge's sound callback.
        poke_set_sound_callback(core, { path, pan, volume, userdata in
            guard let path, let userdata else { return }
            let session = Unmanaged<GameSession>.fromOpaque(userdata).takeUnretainedValue()
            let p = String(cString: path)
            // The callback arrives on whatever thread runs the emulator's frame; the
            // player is main-thread state.
            Task { @MainActor in session.cuePlayer.play(path: p, pan: Int(pan), volume: Int(volume)) }
        }, Unmanaged.passUnretained(self).toOpaque())

        if let script = Self.bundledScript {
            poke_set_script(core, script)
        }
        // Let the on-screen pad talk to this session's core.
        InputBridge.connect(self)
        status = .needROM

        // Heat, not leaks, is what makes a long PSP session choppy: the
        // interpreter and software GPU run flat out on the main thread, and
        // iOS throttles the CPU as the phone warms. Log every change so a
        // slowdown report can be matched to it, and say so when it is bad
        // enough to be heard.
        thermalObserver = NotificationCenter.default.addObserver(
            forName: ProcessInfo.thermalStateDidChangeNotification,
            object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.thermalStateChanged() }
        }
    }

    private func thermalStateChanged() {
        let state = ProcessInfo.processInfo.thermalState
        let name: String
        switch state {
        case .nominal: name = "nominal"
        case .fair: name = "fair"
        case .serious: name = "serious"
        case .critical: name = "critical"
        @unknown default: name = "unknown"
        }
        appendDebug("thermal: \(name)")
        if state == .serious || state == .critical, case .running = status {
            speech?.announce("The phone is getting hot. The game may slow down.")
        }
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

    /// Silence that sticks, without any script key.
    ///
    /// A VoiceOver two-finger tap (or the synth stopping) only ends the
    /// current utterance; the script produces new lines every frame, so
    /// speech resumes a frame later. Engine-level silence drops those lines
    /// until the player asks for something. This is the only stop available
    /// on systems whose script has no stop key (Game Boy, PSP).
    /// Silence that sticks: stop the platform voice AND clear the core's
    /// announcement queue, so a queued High line cannot resume speech after
    /// the stop. Silent by contract (no refusal speech): with no adapter the
    /// command is refused and there is nothing queued anyway.
    func stopSpeech() {
        speech?.stopAll()
        if let core { _ = poke_command(core, AdapterCommand.stopSpeech.rawValue) }
    }

    /// Walk the spoken history: speak the last line again, or step one line further back.
    ///
    /// Returns true when the core spoke something. The history lives in the core's
    /// announcement queue, because the queue is the only thing that knows what actually
    /// reached the platform — so a line suppressed as a duplicate is correctly NOT
    /// repeatable, and the walk refuses honestly at the oldest recorded line.
    ///
    /// ⛔ A CORE WITH NO QUEUE RETURNS FALSE, and the caller falls back to its own
    /// last-spoken mirror. That is not a duplicate history: it is the pre-queue path
    /// still working, which is what keeps this from breaking an older host.
    @discardableResult
    func repeatSpoken(newest: Bool) -> Bool {
        guard let core else { return false }
        let cmd = newest ? AdapterCommand.repeatNewest : AdapterCommand.repeatOlder
        // Silence on refusal is deliberate: the caller says "Nothing to repeat." itself,
        // so a refusal must not also produce a line here or the player hears it twice.
        return poke_command(core, cmd.rawValue)
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
    ///
    /// `id` is the announcement-queue utterance id; the engine hands it back
    /// through `poke_announce_done` when the platform voice finishes the line.
    private static let speechCallback: @convention(c)
        (UnsafePointer<CChar>?, Bool, UInt32, UnsafeMutableRawPointer?) -> Void = { text, interrupt, id, userdata in
        guard let userdata else { return }
        let engine = Unmanaged<SpeechEngine>.fromOpaque(userdata).takeUnretainedValue()
        guard let text else {
            Task { @MainActor in engine.stop() }
            return
        }
        let string = String(cString: text)
        // Hop to the main actor: AVSpeechSynthesizer must be driven there, and
        // the core calls this from the frame-loop thread.
        Task { @MainActor in engine.speak(string, interrupt: interrupt, id: id) }
    }

    func attach(to speech: SpeechEngine) {
        self.speech = speech
        // Re-point the native speech callback at the environment's engine.
        guard let core else { return }
        poke_set_speech_id_callback(core, GameSession.speechCallback,
                                    Unmanaged.passUnretained(speech).toOpaque())
        speech.queueCore = core
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
        readerSetName = nil
        adapterReady = false
        romGameCode = nil

        let name = url.lastPathComponent
        guard let local = try? ROMStore.importROM(from: url) else {
            status = .failed("Could not read \(name).")
            return
        }

        system = GameSystem.forROMExtension(local.pathExtension)
        if let system, system.analogSticks > 0 || system.id == 4 {
            if let assets = BundleResources.ppssppAssetDir {
                // The PSP core refuses to boot without its staged assets (compat
                // tables, soft-GPU atlas, VFPU LUTs); the path is set here so a
                // missing bundle fails loudly at load, not mid-boot.
                assets.withCString { poke_set_psp_asset_dir(core, $0) }
            } else {
                status = .failed("PPSSPP assets missing. \(BundleResources.ppssppAssetDiagnosis)")
                return
            }
        }

        let save = ROMStore.savePath(for: local)
        if poke_load_rom(core, local.path, save.path) {
            // Game Boy ROMs do not use the concatenated NDS script installed in
            // init (the GBA core ignores it): their reader set loads itself
            // from this directory. Set before Start Game, which boots it.
            if ["gba", "gbc", "gb"].contains(local.pathExtension.lowercased()),
               let dir = BundleResources.gbaScriptDir {
                dir.withCString { poke_set_script_dir(core, $0) }
                // The cue player resolves the reader's relative cue paths against
                // this same directory: the reader joins `scriptpath` itself, so the
                // cues arrive as paths under it.
                cuePlayer.setScriptDir(dir)
                cuePlayerCuesEnabled()
            }
            // NES readers are a per-GAME choice, so the CORE names the set (by the
            // ROM's CRC32, never by filename) and this stages a writable copy of it.
            // ⛔ "" IS THE NORMAL ANSWER for a game no reader covers: the console then
            // boots with no reader rather than someone else's, which is the honest
            // outcome for an unknown dump or a translation patch.
            if system?.id == 5 {   // OGA_SYS_NES
                let set = String(cString: poke_reader_set(core))
                readerSetName = set.isEmpty ? nil : set
                if let dir = ReaderStore.stage(set) {
                    dir.path.withCString { poke_set_script_dir(core, $0) }
                    // Same directory the cues resolve against (see the GB path above).
                    cuePlayer.setScriptDir(dir.path)
                    cuePlayerCuesEnabled()
                }
            }
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

    /// Poll the reader for a cue snapshot and drive the beacon. Called once per frame
    /// from the frame loop, on the main thread.
    ///
    /// ⛔ CHEAP AND FAIL-CLOSED. poke_cue_snapshot returns 0 whenever there is no battle,
    /// no adapter, or no cue data — and 0 silences the beacon, so a game without cues is
    /// simply quiet rather than wrong. Nothing here allocates or speaks.
    private func refreshCue() {
        guard let core else { return }
        var dist: Float = 0
        let state = poke_cue_snapshot(core, &dist)
        CueSynth.shared.updateBeacon(state: state, distance: dist,
                                     enabled: audioEnabled && cueBeaconEnabled)
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
        refreshCue()
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
                             ? "No reader."
                             : "Not ready yet.")
            return
        }
    }

    /// Forward a game-pad down-edge to the native reader for OSK echo.
    ///
    /// Silent by contract: adapters that are not tracking an OSK ignore these
    /// commands, and a not-ready core drops them without the refusal speech —
    /// announcing "not ready" on every Cross press would be chatter, not help.
    func forwardPadCommand(_ command: AdapterCommand) {
        guard let core, adapterReady else { return }
        _ = poke_command(core, command.rawValue)
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
        // ⛔ NOT objectWillChange on the session: that re-ran the body of every
        // view observing GameSession — the whole control panel — 60 times a
        // second, on the same main thread that runs the emulator.
        frames.image = CGImage(
            width: Int(w), height: Int(h),
            bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: bytesPerRow,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false,
            intent: .defaultIntent
        )
    }

    // MARK: - Audio

    /// Apply the user's "Game sound" setting to the reader's cues.
    ///
    /// A positional cue IS game sound, so it rides the setting that already exists for
    /// that rather than adding a second toggle to discover. Called wherever the reader
    /// directory is set, so a muted session never plays a cue at all.
    private func cuePlayerCuesEnabled() {
        cuePlayer.level = audioEnabled ? 0.8 : 0
    }

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
        // Silence the cue as well as the game: a tone left running after the emulator
        // stops is a stuck note with nothing to explain it.
        CueSynth.shared.silenceAll()
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
            // ⛔ MIX THE CUE IN HERE, AFTER THE CORE'S FRAMES, so there is one output path.
            // Only over the frames the core actually produced: writing into the starved
            // tail would put a tone where the fade below is trying to remove a click.
            if got > 0 { CueSynth.shared.render(into: ptr, frames: got) }
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

/// The latest emulated frame. Separate from GameSession so the 60 Hz picture
/// update invalidates only the view that draws it (FrameImage in RootView),
/// not every view that reads session state.
@MainActor
final class FrameStore: ObservableObject {
    @Published var image: CGImage?
}
