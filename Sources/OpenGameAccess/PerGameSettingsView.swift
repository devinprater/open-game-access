import SwiftUI

/// The Per Game Settings panel: one game's overrides, reached from the
/// "Per Game Settings" control at the top right of its library page
/// (docs/design/per-game-settings.md).
///
/// WHAT THE PLAYER SEES. One section per setting this game's reader
/// can actually use — a game never lists a setting it cannot use. Each
/// section's switch shows the value the game will use (its override
/// when one exists, else the app default from Settings), the footer
/// says WHICH of those it is and what the app default is, and a game
/// carrying an override gets a "Use app default" button that clears
/// it. Every change is spoken by the panel itself, for the reason
/// SettingsView gives about its own switches: with no screen, an
/// unspoken change is an invisible one.
///
/// A game whose reader uses no per-game setting still gets this panel
/// (the control sits on every game page, always in the same place) and
/// the panel says so in words rather than opening empty.
struct PerGameSettingsView: View {
    let game: AdapterGame

    @EnvironmentObject private var speech: SpeechEngine

    /// Bumped after every store write so the body re-reads the
    /// overrides — the store is UserDefaults, not observable state.
    @State private var revision = 0

    private var settings: [GameSettings.Setting] {
        GameSettings.supportedSettings(forGameID: game.id)
    }

    var body: some View {
        // Read here so a store write (which bumps revision) re-renders
        // the rows: the store is UserDefaults, not observable state.
        let _ = revision
        return Form {
            Section {
                Text("These settings apply to \(game.name) only. A setting with no override uses the app default from Settings; changing it here overrides it for this game. Settings apply when the game starts.")
                    .font(.footnote)
            }

            if settings.isEmpty {
                Section {
                    Text("This game has no per-game settings yet. The settings that apply to every game live in Settings.")
                }
            }

            ForEach(settings, id: \.rawValue) { setting in
                Section {
                    Toggle(setting.title, isOn: binding(for: setting))
                        .accessibilityHint(setting.detail)
                    if GameSettings.override(setting, forGameID: game.id) != nil {
                        Button("Use app default") { clearOverride(setting) }
                            .accessibilityHint("Removes this game's override, so \(setting.title) follows the app default again.")
                    }
                } footer: {
                    Text(provenance(for: setting))
                }
            }
        }
        .navigationTitle("Per Game Settings")
    }

    /// The switch's value: the game's effective value (override, else
    /// app default). Writing always writes an OVERRIDE — even when the
    /// new value equals the default, the player made a choice for this
    /// game, and the footer will say so until they clear it.
    private func binding(for setting: GameSettings.Setting) -> Binding<Bool> {
        Binding(
            get: {
                GameSettings.effective(setting, forGameID: game.id,
                                       globalDefault: GameSettings.globalDefault(setting))
            },
            set: { newValue in
                GameSettings.setOverride(newValue, setting, forGameID: game.id)
                revision += 1
                speech.announce("\(setting.title): \(newValue ? "on" : "off"), for \(game.name).")
            }
        )
    }

    private func clearOverride(_ setting: GameSettings.Setting) {
        GameSettings.setOverride(nil, setting, forGameID: game.id)
        revision += 1
        let fallback = GameSettings.globalDefault(setting)
        speech.announce("\(setting.title) for \(game.name) now uses the app default: \(fallback ? "on" : "off").")
    }

    /// Whether the shown value is this game's own choice or the
    /// inherited default, and what that default is — the fact a player
    /// needs before deciding whether to touch the switch.
    private func provenance(for setting: GameSettings.Setting) -> String {
        let global = GameSettings.globalDefault(setting)
        if GameSettings.override(setting, forGameID: game.id) != nil {
            return "\(setting.detail) Set for this game. The app default is \(global ? "on" : "off")."
        }
        return "\(setting.detail) Using the app default: \(global ? "on" : "off"). Change the app default in Settings."
    }
}
