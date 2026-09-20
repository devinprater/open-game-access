import Foundation

/// The accessibility commands a game's native reader understands.
///
/// ⛔ THE RAW VALUES ARE THE C ABI. They are the numeric ids documented on
/// `poke_command` in Sources/CPokeCore/include/pokecore.h and defined by
/// `oga::Command` in Core/adapter.h. They must stay in lockstep with that enum —
/// reordering it there without changing this file would make a button silently
/// trigger a DIFFERENT command, which is worse than not working at all.
///
/// Why an enum instead of raw ints at the call sites: the compiler then catches a
/// typo, and the one place that knows the numbers is this file.
enum AdapterCommand: Int32, CaseIterable {
    /// Reads the current place and the player's coordinates.
    case whereAmI = 0
    /// Moves to the next party member and reads them.
    case nextAlly = 1
    /// Moves to the previous party member and reads them.
    case prevAlly = 2
    /// Moves to the next enemy and reads it.
    case nextEnemy = 3
    /// Moves to the previous enemy and reads it.
    case prevEnemy = 4
    /// Jumps to the next party member who has not acted yet.
    case nextUnactedAlly = 5
    /// Writes the reader's current state to the debug log, for reporting bugs.
    case dumpState = 6

    /// The label a player hears and sees. Kept here rather than in the view so the
    /// command and its wording cannot drift apart.
    var title: String {
        switch self {
        case .whereAmI:        return "Where am I"
        case .nextAlly:        return "Next ally"
        case .prevAlly:        return "Previous ally"
        case .nextEnemy:       return "Next enemy"
        case .prevEnemy:       return "Previous enemy"
        case .nextUnactedAlly: return "Next waiting ally"
        case .dumpState:       return "Copy state to log"
        }
    }

    /// The VoiceOver hint. Says what the command does AND that it is the game's own
    /// reader, because a player needs to know these buttons are not the Lua script's
    /// hotkeys and follow different rules.
    var hint: String {
        switch self {
        case .whereAmI:
            return "Reads your location from the game itself, not from the picture."
        case .nextAlly:
            return "Reads the next party member's name and health."
        case .prevAlly:
            return "Reads the previous party member's name and health."
        case .nextEnemy:
            return "Reads the next enemy."
        case .prevEnemy:
            return "Reads the previous enemy."
        case .nextUnactedAlly:
            return "Reads the next party member who has not acted yet."
        case .dumpState:
            return "Writes the reader's current state to the debug log."
        }
    }

    /// A short symbol for the button face.
    var symbol: String {
        switch self {
        case .whereAmI:        return "location.circle"
        case .nextAlly:        return "person.crop.circle.badge.plus"
        case .prevAlly:        return "person.crop.circle.badge.minus"
        case .nextEnemy:       return "exclamationmark.triangle"
        case .prevEnemy:       return "exclamationmark.triangle.fill"
        case .nextUnactedAlly: return "hourglass"
        case .dumpState:       return "doc.text.magnifyingglass"
        }
    }

    /// ⛔ NOT EVERY ADAPTER IMPLEMENTS EVERY COMMAND, AND SOME IMPLEMENT IT AS A REFUSAL.
    /// Attack of the Saiyans has a full command switch, but its enemy cases answer
    /// "not applicable" (no enemy structure was ever located) and its next-unacted
    /// case is an alias for next-ally. Showing those buttons would give a blind player
    /// a control that either refuses or lies.
    ///
    /// So the list below is what each adapter REALLY does today, not what its switch
    /// compiles. Keep it in step with the adapters: if one gains a real implementation,
    /// add it here, and if one is refactored to a stub, remove it — a dead button is
    /// worse than a missing one, because the player cannot tell it from a bug.
    ///
    /// Keyed on the adapter id from `poke_adapter_id`, which is the stable identifier
    /// (`fe11`, `dbz-saiyans`, `gba`). The ROM game code is not used because the core
    /// does not expose it to Swift, and the adapter id already encodes the selection.
    static func supported(adapterID: String?) -> [AdapterCommand] {
        guard let id = adapterID, !id.isEmpty else { return [] }
        switch id {
        case "fe11":
            // Fire Emblem: map, units, and enemy navigation are all real.
            return [.whereAmI, .nextAlly, .prevAlly, .nextEnemy, .prevEnemy,
                    .nextUnactedAlly, .dumpState]

        case "dbz-saiyans":
            // Party and location are real. Enemy navigation is NOT: the adapter
            // answers "Not applicable in this game", and next-unacted is an alias for
            // next-ally rather than a real waiting-list query.
            return [.whereAmI, .nextAlly, .prevAlly, .dumpState]

        case "gba":
            // The GBA reader exists, but its player-facing interaction is the Lua
            // script's own hotkeys, which are already on screen. Exposing a second,
            // parallel set of controls would be confusing rather than helpful.
            return []

        case "dissidia":
            // Dissidia Final Fantasy: board directions, map markers, battle
            // foe/lock state, and dump are real. The prev/next fallbacks in the
            // adapter just repeat position (aliases for whereAmI), so they stay
            // hidden by the same rule as dbz-saiyans' alias.
            return [.whereAmI, .nextAlly, .nextEnemy, .dumpState]

        default:
            return []
        }
    }
}
