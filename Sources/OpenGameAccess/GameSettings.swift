import Foundation

/// Per-game settings: the model behind the "Per Game Settings" panel on
/// each game's library page (docs/design/per-game-settings.md).
///
/// THE MODEL, in one paragraph. Every setting has a GLOBAL DEFAULT,
/// which lives in the app's Settings screen and applies to every game
/// that can use the setting. A game may carry an OVERRIDE in its own
/// panel. The value a session actually uses is the EFFECTIVE value:
///
///     effective = the game's override, if one exists, else the global.
///
/// A game shows only the settings its reader can use (see
/// `supportedSettings(forGameID:)`); a panel row for a setting the game
/// cannot use would be a dead control, and in this app a dead control
/// is indistinguishable from a broken one.
///
/// This file is the store and the resolver only — no UI, no session —
/// so it stays Foundation-only and type-checkable off-device
/// (scripts/swift-check.py). The panel itself is PerGameSettingsView.
///
/// KEYS. Overrides live in UserDefaults under
/// "gameSetting.<setting>.<game id>", where the game id is the
/// adapter registry id ("khbbs") — the same id the library page is
/// keyed by. Presence of the key IS the override: clearing an override
/// removes the key. The globals are NOT stored here: the battle-cues
/// global is GameSession's "cueBeacon" key, the gauge-cues global is
/// "hudGaugeCuesSound", and each is owned by the session property that
/// persists it; this store only reads them when a caller needs the
/// default away from the session (the panel).
enum GameSettings {

    /// One setting a game can carry an override for. The raw value is
    /// the middle segment of the override key, so it is part of the
    /// on-disk format: rename a case's raw value and every existing
    /// override for it is orphaned.
    enum Setting: String, CaseIterable {
        /// The lock-on beacon + health beeps + gauge-full earcons
        /// family — iOS's "Lock cue" switch, generalised.
        case battleCues = "battle_cues"
        /// Birth by Sleep's HUD gauge-full moments: earcon sounds
        /// (true) or the adapter's spoken lines (false).
        case hudGaugeCues = "hud_gauge_cues"

        /// The row's name, in the words the global Settings use.
        var title: String {
            switch self {
            case .battleCues: return "Battle cue sounds"
            case .hudGaugeCues: return "Gauge-full cues"
            }
        }

        /// What the row's hint says. VoiceOver reads this after the
        /// value, so it carries the fact the name alone cannot.
        var detail: String {
            switch self {
            case .battleCues:
                return "The lock-on beacon, a beep on every hit that drops a health bar, and the gauge-full earcons, played over the game sound."
            case .hudGaugeCues:
                return "When the Focus or D-Link gauge fills: on plays a short sound, off speaks a line instead. Kingdom Hearts: Birth by Sleep only."
            }
        }
    }

    // MARK: - Which games can use which setting

    /// The settings a game can use, in panel order. Keyed by adapter
    /// registry id, kept by hand in the app layer — the same precedent
    /// as AdapterCommand.supported, whose rule this copies: the table
    /// says what each adapter REALLY does today, not what it might do.
    ///
    /// battleCues belongs to exactly the adapters that fill a
    /// CueSnapshot (a non-null cue_snapshot in Core): dissidia, dbzar,
    /// mku, khbbs. hudGaugeCues belongs to khbbs alone — the seam is
    /// that adapter's own setter (oga::KhbbsHudGaugeCues).
    static func supportedSettings(forGameID gameID: String) -> [Setting] {
        switch gameID {
        case "khbbs":
            return [.battleCues, .hudGaugeCues]
        case "dissidia", "dbzar", "mku":
            return [.battleCues]
        default:
            return []
        }
    }

    // MARK: - Global defaults

    /// The current global default for a setting, read from wherever
    /// the global lives. The session owns both globals (its published
    /// properties persist them); these reads exist for the panel,
    /// which must show the default a game is inheriting.
    static func globalDefault(_ setting: Setting,
                              defaults: UserDefaults = .standard) -> Bool {
        switch setting {
        case .battleCues:
            return defaults.object(forKey: "cueBeacon") as? Bool ?? false
        case .hudGaugeCues:
            return defaults.object(forKey: "hudGaugeCuesSound") as? Bool ?? true
        }
    }

    // MARK: - Overrides

    private static func overrideKey(_ setting: Setting, forGameID gameID: String) -> String {
        "gameSetting.\(setting.rawValue).\(gameID)"
    }

    /// The game's override for a setting, or nil when it has none —
    /// nil is the normal state and means "use the global default".
    static func override(_ setting: Setting, forGameID gameID: String,
                         defaults: UserDefaults = .standard) -> Bool? {
        defaults.object(forKey: overrideKey(setting, forGameID: gameID)) as? Bool
    }

    /// Set the game's override — or, with nil, clear it back to
    /// inheriting the global default (the key is removed, not set
    /// false: presence is the override).
    static func setOverride(_ value: Bool?, _ setting: Setting, forGameID gameID: String,
                            defaults: UserDefaults = .standard) {
        let key = overrideKey(setting, forGameID: gameID)
        if let value {
            defaults.set(value, forKey: key)
        } else {
            defaults.removeObject(forKey: key)
        }
    }

    // MARK: - Resolution

    /// The value a session uses: the game's override when one exists,
    /// else the global default. A nil game id (no game identified yet)
    /// resolves to the global — which is also what an unidentified
    /// game would resolve to once named, so the answer never flips
    /// mid-start.
    static func effective(_ setting: Setting, forGameID gameID: String?,
                          globalDefault: Bool,
                          defaults: UserDefaults = .standard) -> Bool {
        guard let gameID, let override = override(setting, forGameID: gameID, defaults: defaults)
        else { return globalDefault }
        return override
    }
}
