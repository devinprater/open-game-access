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
    var body: some View {
        VStack(spacing: 18) {
            PathFindingGroup()
            ReadingGroup()
        }
    }
}

/// Path finding and the map: choosing a destination and being guided to it.
private struct PathFindingGroup: View {
    var body: some View {
        VStack(spacing: 6) {
            HStack(spacing: 6) {
                HotkeyButton("Find path", key: "P",
                             hint: "Guides you to the selected place, turn by turn. Press again to stop. Key P.")
                HotkeyButton("Where am I", key: "C",
                             hint: "Reads the place name and your coordinates. Key C.")
                HotkeyButton("Tiles", key: "E",
                             hint: "Reads the tiles around you. Only useful where the ground matters, such as the dark. Key E.")
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Path finding and map")
    }
}

/// Reading a list: items on the map, or options on a menu.
private struct ReadingGroup: View {
    var body: some View {
        VStack(spacing: 6) {
            HStack(spacing: 6) {
                HotkeyButton("Read item", key: "K",
                             hint: "Reads the selected item again. Key K.")
                HotkeyButton("Previous item", key: "J",
                             hint: "Moves to the previous item. Key J.")
                HotkeyButton("Next item", key: "L",
                             hint: "Moves to the next item. Key L.")
            }
            HStack(spacing: 6) {
                HotkeyButton("Previous group", key: "I",
                             hint: "Previous group of items, such as people or signs. Key I.")
                HotkeyButton("Next group", key: "O",
                             hint: "Next group of items. Key O.")
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Reading items and lists")
    }
}

private struct SystemButtons: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        HStack(spacing: 6) {
            HoldButton(title: "Start", symbol: "play.circle", hint: "Start button, opens the menu") { .start }
            HoldButton(title: "Select", symbol: "square.circle", hint: "Select button") { .select }
            HoldButton(title: "L", symbol: "l.circle", hint: "Left shoulder button") { .l }
            HoldButton(title: "R", symbol: "r.circle", hint: "Right shoulder button") { .r }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("System buttons")
    }
}

/// The controls that act on speech itself, kept separate from the game keys
/// because they are the ones a player reaches for when narration goes wrong.
private struct SpeechGroup: View {
    var body: some View {
        HStack(spacing: 6) {
            HotkeyButton("Stop speech", key: "R",
                         hint: "Stops the current speech. Key R.")
            HotkeyButton("Repeat", key: "U",
                         hint: "Repeats the last thing said. Key U.")
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Speech controls")
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
    var body: some View {
        HStack(spacing: 6) {
            HoldButton(title: "A", symbol: "a.circle", hint: "Confirm, talk, select") { .a }
            HoldButton(title: "B", symbol: "b.circle", hint: "Cancel, back") { .b }
            HoldButton(title: "X", symbol: "x.circle", hint: "Menu") { .x }
            HoldButton(title: "Y", symbol: "y.circle", hint: "Use item") { .y }
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
    let button: () -> DSButton

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
