import SwiftUI
import UIKit
import CPokeCore

/// RootView is VoiceOver-first: the whole DS control set is reachable as named
/// elements, and the accessibility script's hotkeys are ordinary buttons rather
/// than hidden gestures, so a player can find them by swiping.
struct RootView: View {
    @EnvironmentObject private var session: GameSession
    @EnvironmentObject private var speech: SpeechEngine

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                ScreenView()
                StatusBar()
                if case .running = session.status {
                    Controls()
                } else {
                    SetupPanel()
                }
            }
            .navigationTitle("Open Game Access")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    NavigationLink {
                        SettingsView()
                    } label: {
                        Label("Settings", systemImage: "gearshape")
                    }
                    .accessibilityLabel("Settings")
                }
            }
        }
    }
}

/// The emulated DS screen. This is deliberately *not* an accessibility element:
/// it is a picture of a game being described to the player by the script, and
/// making it focusable just adds a "image" stop to every swipe.
private struct ScreenView: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        GeometryReader { geo in
            ZStack {
                Color.black
                if let image = session.currentFrame {
                    Image(decorative: image, scale: 1.0)
                        .resizable()
                        .interpolation(.none)
                        .aspectRatio(contentMode: .fit)
                        .frame(width: geo.size.width, height: geo.size.height)
                } else {
                    Text(session.status == .needROM ? "Choose a game to begin" : "Starting…")
                        .font(.title2)
                        .foregroundStyle(.secondary)
                        .padding()
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(.black)
        .accessibilityElement()
        .accessibilityLabel("Game screen")
        .accessibilityValue(screenDescription)
        .accessibilityHint("The game screen is described by spoken output. Use the controls below, or swipe past this to reach them.")
    }

    private var screenDescription: String {
        switch session.status {
        case .running(let name): return "Running \(name)"
        case .ready(let name): return "\(name) loaded, not started"
        case .needROM: return "No game loaded"
        case .failed(let message): return message
        case .idle: return "Starting"
        }
    }
}

/// One line that always reflects the app state and is read first on focus.
private struct StatusBar: View {
    @EnvironmentObject private var session: GameSession

    private var text: String {
        switch session.status {
        case .idle: return "Starting"
        case .needROM: return "No game loaded"
        case .ready(let name): return "\(name) ready"
        case .running(let name): return "Playing \(name)"
        case .failed(let message): return "Problem: \(message)"
        }
    }

    var body: some View {
        Text(text)
            .font(.footnote)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal)
            .padding(.vertical, 6)
            .background(.thinMaterial)
            .accessibilityLabel(text)
    }
}

/// Setup: pick a ROM and start. Two buttons, in the order they are needed.
private struct SetupPanel: View {
    @EnvironmentObject private var session: GameSession
    @EnvironmentObject private var speech: SpeechEngine
    @State private var showingPicker = false

    var body: some View {
        VStack(spacing: 12) {
            Button {
                showingPicker = true
            } label: {
                Label("Select Game", systemImage: "folder")
                    .frame(maxWidth: .infinity, minHeight: 44)
            }
            .buttonStyle(.borderedProminent)
            .accessibilityHint("Opens the file picker. Choose a supported game.")

            Button {
                session.start()
            } label: {
                Label("Start Game", systemImage: "play.fill")
                    .frame(maxWidth: .infinity, minHeight: 44)
            }
            .buttonStyle(.bordered)
            .disabled({ if case .ready = session.status { return false }; return true }())
            .accessibilityHint("Starts the game and the accessibility script.")

            if case .failed(let message) = session.status {
                Text(message)
                    .font(.footnote)
                    .foregroundStyle(.red)
                    .accessibilityLabel("Error: \(message)")
            }
        }
        .padding()
        .fileImporter(
            isPresented: $showingPicker,
            allowedContentTypes: ROMStore.allowedTypes,
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                session.loadROM(at: url)
            case .failure(let error):
                speech.announce("Could not open that file: \(error.localizedDescription)")
            }
        }
    }
}

/// The DS pad, the accessibility script's own keys, and the way out.
///
/// Regrouped by WHAT THE PLAYER IS DOING rather than by which keys exist: moving,
/// running the game, finding a path, reading a list, and the two controls that act
/// on speech itself. The previous flat grid of fifteen buttons made every one of
/// them equally far away — which matters when you are navigating it by swipe.
///
/// Each group is a labelled accessibility container, so a screen reader announces
/// "Path finding and map" as you enter it instead of reading an undifferentiated
/// run of buttons.
private struct Controls: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        ScrollView {
            VStack(spacing: 18) {
                // Movement and the game's own buttons stay first: they are what a
                // player reaches for continuously, as opposed to the reading
                // commands which are consulted at a decision point.
                DPad()
                ActionButtons()
                SystemButtons()
                ReaderGroups()
                SpeechGroup()
                GameControl()
            }
            .padding()
        }
        .frame(maxHeight: 360)
    }
}

/// Leaving, and getting back in.
private struct GameControl: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        Button {
            session.quit()
        } label: {
            Label("Quit game", systemImage: "xmark.circle")
                .frame(maxWidth: .infinity, minHeight: 48)
        }
        .buttonStyle(.bordered)
        .accessibilityLabel("Quit game")
        .accessibilityHint("Saves your progress and closes the game. A two finger scrub also does this.")
        // THE VOICEOVER ESCAPE GESTURE (a two-finger scrub, "Z").
        //
        // `.escape` is the platform's own kind for exactly this: leaving the
        // current context. Wiring it here means the standard gesture a VoiceOver
        // user already knows works, instead of requiring them to find and
        // double-tap this specific button — which is the difference between a
        // control being reachable and being practically usable.
        .accessibilityAction(.escape) {
            session.quit()
        }
    }
}

/// The script's commands, grouped by the screen they apply to.
private struct ReaderGroups: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        VStack(spacing: 18) {
            AdapterGroup()
            ReaderStatusLine()
            // The script's own hotkeys only narrate Pokémon Black/White 1. For
            // every other game they are dead buttons, so they stay hidden.
            if session.isPokemonROM {
                PathFindingGroup()
                ReadingGroup()
            }
        }
    }
}

/// The honest one-line status when there are no reader controls to show.
///
/// Two cases, and they mean different things: a known adapter that is not
/// ready yet is still loading (its game state does not exist yet, e.g. still
/// on a title screen), while a running game with no adapter at all simply has
/// no reader — including the game code so a wrong-region ROM is diagnosable
/// instead of silently buttonless.
private struct ReaderStatusLine: View {
    @EnvironmentObject private var session: GameSession

    private var gameActive: Bool {
        if case .ready = session.status { return true }
        if case .running = session.status { return true }
        return false
    }

    var body: some View {
        if !session.availableAdapterCommands.isEmpty,
           !session.adapterReady, let name = session.adapterName {
            Text("\(name) reader loading…")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .accessibilityLabel("\(name) reader loading")
        } else if gameActive, session.adapterID == nil,
                  let code = session.romGameCode, !code.isEmpty {
            Text("No reader for this game (code \(code)).")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .accessibilityLabel("No reader for this game, code \(code)")
        }
    }
}

/// The game's OWN reader — the native adapter, not the Lua script.
///
/// ⛔ THIS GROUP IS HIDDEN FOR MOST GAMES, AND THAT IS CORRECT. Only a handful of
/// titles have a native reader; for everything else `availableAdapterCommands` is
/// empty and nothing is shown. An always-present set of buttons that answer "this game
/// has no reader controls" would be worse than no buttons, because a blind player
/// cannot tell a deliberately-absent control from a broken one.
///
/// Shown only once the adapter reports it has real game state (`adapterReady`), so the
/// controls never appear while a map or party is still loading.
private struct AdapterGroup: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        let commands = session.availableAdapterCommands
        if !commands.isEmpty, session.adapterReady, let name = session.adapterName {
            VStack(spacing: 6) {
                Text("\(name) reader")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .accessibilityAddTraits(.isHeader)

                // Two per row: the labels are short but the touch targets are not,
                // and a blind player navigating by swipe benefits from predictable
                // row shapes over a long single column.
                ForEach(Array(stride(from: 0, to: commands.count, by: 2)), id: \.self) { i in
                    HStack(spacing: 6) {
                        AdapterButton(command: commands[i])
                        if i + 1 < commands.count {
                            AdapterButton(command: commands[i + 1])
                        } else {
                            Spacer().frame(maxWidth: .infinity)
                        }
                    }
                }
            }
            .accessibilityElement(children: .contain)
            .accessibilityLabel("\(name) reader")
        }
    }
}

/// One native-reader command, as an ordinary button so it is reachable by swiping.
private struct AdapterButton: View {
    @EnvironmentObject private var session: GameSession

    let command: AdapterCommand

    var body: some View {
        Button {
            session.sendAdapterCommand(command)
        } label: {
            Label(command.title, systemImage: command.symbol)
                .labelStyle(.titleOnly)
                .font(.headline)
                .frame(maxWidth: .infinity, minHeight: 48)
                .background(Color(uiColor: .secondarySystemBackground))
                .foregroundStyle(Color.primary)
                .clipShape(RoundedRectangle(cornerRadius: 10))
        }
        .accessibilityAddTraits(.isButton)
        .accessibilityLabel(command.title)
        .accessibilityHint(command.hint)
    }
}

/// Path finding and the map: choosing a destination and being guided to it.
///
/// The buttons come from the loaded system's key list (GameSystem.pathKeys):
/// the DS answers P/C/E, the Game Boy answers P/E, and the PSP answers none
/// (hotkeys are inert there), so this whole group is absent on PSP.
private struct PathFindingGroup: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        let keys = (session.system ?? .ds).pathKeys
        if !keys.isEmpty {
            VStack(spacing: 6) {
                ForEach(Array(keys.chunked(into: 3).enumerated()), id: \.offset) { _, row in
                    HStack(spacing: 6) {
                        ForEach(Array(row.enumerated()), id: \.offset) { _, item in
                            HotkeyButton(item.title, key: item.key, hint: item.hint)
                        }
                    }
                }
            }
            .accessibilityElement(children: .contain)
            .accessibilityLabel("Path finding and map")
        }
    }
}

/// Reading a list: items on the map, or options on a menu.
///
/// Same per-system rule as path finding: K/J/L everywhere a script runs,
/// I/O group switching on the DS only (on Game Boy that is shift-J/shift-L,
/// which a single tap cannot send).
private struct ReadingGroup: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        let keys = (session.system ?? .ds).readingKeys
        if !keys.isEmpty {
            VStack(spacing: 6) {
                ForEach(Array(keys.chunked(into: 3).enumerated()), id: \.offset) { _, row in
                    HStack(spacing: 6) {
                        ForEach(Array(row.enumerated()), id: \.offset) { _, item in
                            HotkeyButton(item.title, key: item.key, hint: item.hint)
                        }
                    }
                }
            }
            .accessibilityElement(children: .contain)
            .accessibilityLabel("Reading items and lists")
        }
    }
}

private struct SystemButtons: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        HStack(spacing: 6) {
            HoldButton(title: "Start", symbol: "play.circle", hint: "Start button, opens the menu") { .start }
            HoldButton(title: "Select", symbol: "square.circle", hint: "Select button") { .select }
            // The original Game Boy has no shoulder buttons; showing L/R for
            // it would be controls that do nothing.
            if (session.system ?? .ds).hasShoulders {
                HoldButton(title: "L", symbol: "l.circle", hint: "Left shoulder button") { .l }
                HoldButton(title: "R", symbol: "r.circle", hint: "Right shoulder button") { .r }
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("System buttons")
    }
}

/// The controls that act on speech itself, kept separate from the game keys
/// because they are the ones a player reaches for when narration goes wrong.
private struct SpeechGroup: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        let system = session.system ?? .ds
        if !system.speechKeys.isEmpty {
            // R/U are DS script keys: R tells the script itself to stop, so
            // its queue clears as well as the audio.
            HStack(spacing: 6) {
                ForEach(Array(system.speechKeys.enumerated()), id: \.offset) { _, item in
                    HotkeyButton(item.title, key: item.key, hint: item.hint)
                }
            }
            .accessibilityElement(children: .contain)
            .accessibilityLabel("Speech controls")
        } else if system.hasDirectSpeechStop {
            // No script key stops speech on this system (on Game Boy, R moves
            // the camera), so stop at the engine: clear the queue and drop
            // background lines until the player asks for something. A
            // VoiceOver two-finger tap only ends the current utterance while
            // the script keeps producing lines; this is what makes it stick.
            // It also works with VoiceOver off, where the tap does not exist.
            HStack(spacing: 6) {
                Button {
                    session.stopSpeech()
                } label: {
                    Text("Stop speech")
                        .font(.subheadline)
                        .frame(maxWidth: .infinity, minHeight: 48)
                }
                .buttonStyle(.bordered)
                .accessibilityLabel("Stop speech")
                .accessibilityHint("Stops the current speech and stays silent until you ask for something.")
            }
            .accessibilityElement(children: .contain)
            .accessibilityLabel("Speech controls")
        }
    }
}

private struct DPad: View {
    var body: some View {
        VStack(spacing: 6) {
            HoldButton(title: "Up", symbol: "arrow.up", hint: "Move up") { .up }
            HStack(spacing: 6) {
                HoldButton(title: "Left", symbol: "arrow.left", hint: "Move left") { .left }
                HoldButton(title: "Right", symbol: "arrow.right", hint: "Move right") { .right }
            }
            HoldButton(title: "Down", symbol: "arrow.down", hint: "Move down") { .down }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Directional pad")
    }
}

private struct ActionButtons: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        HStack(spacing: 6) {
            // The raw values are shared pad indices; the C core maps them per
            // backend, so only the labels change: A/B on Game Boy, Cross/
            // Circle/Triangle/Square on PSP.
            ForEach(Array(((session.system ?? .ds).faceButtons).enumerated()), id: \.offset) { _, item in
                HoldButton(title: item.title, symbol: item.symbol, hint: item.hint) {
                    GamePadButton(raw: item.raw)
                }
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Action buttons")
    }
}

// MARK: - Button primitives

/// A button that stays pressed for as long as the finger is down: the DS reads
/// held directions per frame, so a tap-only button would drop movement.
private struct HoldButton: View {
    let title: String
    let symbol: String
    let hint: String
    let button: () -> GamePadButton

    @State private var isDown = false

    var body: some View {
        Label(title, systemImage: symbol)
            .labelStyle(.titleOnly)
            .font(.headline)
            .frame(maxWidth: .infinity, minHeight: 48)
            .background(isDown ? Color.accentColor : Color(uiColor: .secondarySystemBackground))
            .foregroundStyle(isDown ? Color.white : Color.primary)
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .accessibilityAddTraits(.isButton)
            .accessibilityLabel(title)
            .accessibilityHint(hint)
            .accessibilityAction { tap() }
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in if !isDown { press() } }
                    .onEnded { _ in release() }
            )
    }

    private func tap() {
        press()
        // A game button tapped from VoiceOver should register as a press the
        // core sees on the next frame, then release — a double tap-and-hold is
        // not something VoiceOver can express.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12) { release() }
    }

    private func press() {
        isDown = true
        InputBridge.setButton(button(), down: true)
    }

    private func release() {
        guard isDown else { return }
        isDown = false
        InputBridge.setButton(button(), down: false)
    }
}

/// A script hotkey. These are edge-triggered — the script wants the frame the
/// key went down — so they are plain buttons.
private struct HotkeyButton: View {
    let title: String
    let key: String
    let hint: String

    init(_ title: String, key: String, hint: String) {
        self.title = title
        self.key = key
        self.hint = hint
    }

    var body: some View {
        Button {
            InputBridge.tapHotkey(key)
        } label: {
            Text(title)
                .font(.subheadline)
                .lineLimit(2)
                .minimumScaleFactor(0.8)
                .frame(maxWidth: .infinity, minHeight: 48)
        }
        .buttonStyle(.bordered)
        .accessibilityLabel(title)
        .accessibilityHint(hint)
    }
}

/// GamePadButton by shared pad index, for the per-system face-button lists.
extension GamePadButton {
    init(raw: Int32) {
        switch raw {
        case POKE_BTN_A: self = .a
        case POKE_BTN_B: self = .b
        case POKE_BTN_X: self = .x
        case POKE_BTN_Y: self = .y
        case POKE_BTN_START: self = .start
        case POKE_BTN_SELECT: self = .select
        case POKE_BTN_L: self = .l
        case POKE_BTN_R: self = .r
        case POKE_BTN_UP: self = .up
        case POKE_BTN_DOWN: self = .down
        case POKE_BTN_LEFT: self = .left
        default: self = .right
        }
    }
}

extension Array {
    /// Successive non-overlapping slices, for laying key lists out in rows.
    func chunked(into size: Int) -> [[Element]] {
        guard size > 0 else { return [] }
        return stride(from: 0, to: count, by: size).map { i in
            Array(self[i..<Swift.min(i + size, count)])
        }
    }
}
