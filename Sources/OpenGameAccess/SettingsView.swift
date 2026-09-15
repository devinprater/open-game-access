import SwiftUI
import CPokeCore

struct SettingsView: View {
    @EnvironmentObject private var session: GameSession
    @EnvironmentObject private var speech: SpeechEngine

    var body: some View {
        Form {
            Section {
                Picker("Screen shown", selection: $session.focusScreen) {
                    Text("Bottom screen").tag(Int32(POKE_SCREEN_BOTTOM))
                    Text("Top screen").tag(Int32(POKE_SCREEN_TOP))
                }
                .accessibilityHint("Which of the two DS screens is shown. Both are always read by the script.")
            } header: {
                Text("Display")
            } footer: {
                Text("The reading commands work on both screens regardless of which one is shown.")
            }

            Section {
                Toggle("Game sound", isOn: $session.audioEnabled)
                    .accessibilityHint("Plays the game's own audio. Speech is never turned off.")
            } header: {
                Text("Sound")
            }

            Section {
                Button("Save state") { session.saveState() }
                Button("Load state") { session.loadState() }
                Button("Repeat last speech") { speech.speak(speech.lastSpoken, interrupt: true) }
                    .disabled(speech.lastSpoken.isEmpty)
            } header: {
                Text("Game state")
            }

            Section {
                NavigationLink("Reading log") {
                    ReadingLogView()
                }
                .accessibilityHint("Shows the text the accessibility script has produced, for troubleshooting.")
            } header: {
                Text("Troubleshooting")
            }

            Section {
                Text("Open Game Access reads a running game's own memory and speaks what is there — dialogue, menus, the map, characters and their health — instead of guessing from the screen. Which game it can describe depends on the reader script loaded for it.")
                    .font(.footnote)
            } header: {
                Text("About")
            }
        }
        .navigationTitle("Settings")
    }
}

/// The script writes a silent developer trail to the Lua console. Those lines
/// are kept here: they are the only way to see what the reader is doing when a
/// screen is not being announced as expected.
struct ReadingLogView: View {
    @EnvironmentObject private var session: GameSession

    var body: some View {
        List {
            if session.debugLines.isEmpty {
                Text("Nothing yet. Start a game and this fills with the script's own log.")
                    .foregroundStyle(.secondary)
            } else {
                ForEach(Array(session.debugLines.enumerated()), id: \.offset) { _, line in
                    Text(line)
                        .font(.system(.footnote, design: .monospaced))
                        .textSelection(.enabled)
                }
            }
        }
        .navigationTitle("Reading log")
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                ShareLink(item: session.debugLines.joined(separator: "\n")) {
                    Label("Share", systemImage: "square.and.arrow.up")
                }
                .accessibilityLabel("Share the log")
            }
        }
    }
}
