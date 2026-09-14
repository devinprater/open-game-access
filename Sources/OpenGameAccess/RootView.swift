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

/// The DS pad plus the accessibility script's own keys.
///
/// Both groups are laid out as real buttons with labels and hints, so VoiceOver
/// users can find them by swiping; a sighted player can just tap them.
private struct Controls: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                DPad()
                ActionButtons()
                AccessibilityKeys()
                SystemButtons()
            }
            .padding()
        }
        .frame(maxHeight: 340)
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

/// The script's hotkeys, named in the player's language rather than letters.
/// The letter each one maps to is in the hint, because the desktop docs teach
/// them that way and players who know the tool will look for them.
private struct AccessibilityKeys: View {
    var body: some View {
        VStack(spacing: 6) {
            HStack(spacing: 6) {
                HotkeyButton("Read", key: "K", hint: "Reads the selected item. Key K.")
                HotkeyButton("Previous", key: "J", hint: "Previous item. Key J.")
                HotkeyButton("Next", key: "L", hint: "Next item. Key L.")
            }
            HStack(spacing: 6) {
                HotkeyButton("Filter back", key: "I", hint: "Previous item filter. Key I.")
                HotkeyButton("Filter on", key: "O", hint: "Next item filter. Key O.")
                HotkeyButton("Map", key: "C", hint: "Reads the place name and coordinates. Key C.")
            }
            HStack(spacing: 6) {
                HotkeyButton("Path", key: "P", hint: "Find a path to the selected item. Key P.")
                HotkeyButton("Where", key: "Y", hint: "Reads your position. Key Y.")
                HotkeyButton("Tiles", key: "E", hint: "Reads the surrounding tiles. Key E.")
            }
            HStack(spacing: 6) {
                HotkeyButton("Enemy HP", key: "H", hint: "Reads the opponent's health in a battle. Key H.")
                HotkeyButton("My HP", key: "N", hint: "Reads your character's health in a battle. Key N.")
                HotkeyButton("Text", key: "T", hint: "Reads the text currently on screen. Key T.")
            }
            HStack(spacing: 6) {
                HotkeyButton("Stop speech", key: "R", hint: "Stops the current speech. Key R.")
                HotkeyButton("Repeat", key: "U", hint: "Repeats the last thing said. Key U.")
                HotkeyButton("Camera", key: "F", hint: "Moves the reading camera to your position. Key F.")
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Reading commands")
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
