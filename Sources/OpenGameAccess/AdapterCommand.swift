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
    /// Logs which menu is live ("MENU main" etc.) for the host's menu tracker.
    /// Host-programmatic: never shown as a player button.
    case menuState = 7
    /// Moves the tracked menu cursor down one row and reads it. Adapters that
    /// are not on a tracked menu ignore it silently.
    case menuNext = 8
    /// Moves up one row on a tracked menu and reads it.
    case menuPrev = 9
    /// Previous value on a tracked options row.
    case menuLeft = 10
    /// Next value on a tracked options row.
    case menuRight = 11
    /// Tracks the Dissidia story-map Customize menu (no RAM signature, so the
    /// player taps on entry and on exit; the adapter tracks rows from Next /
    /// Previous item). Toggles tracking and reads the current row.
    case custToggle = 12
    case charToggle = 13
    /// Dissidia battle Quickmove marker. Detector-driven (app sends these
    /// when the yellow marker appears/vanishes); never a player button,
    /// so they stay out of supported(adapterID:).
    case quickOn = 14
    case quickOff = 15
    case exReady = 16
    case exSpent = 17
    case exActive = 18
    case exEnded = 19
    case exBurstGo = 20
    case exQteUp = 21
    case exQteDown = 22
    case exQteLeft = 23
    case exQteRight = 24
    case exQteCircle = 25
    case exQteSquare = 26
    case exQteTriangle = 27
    case exQteCross = 28
    case exBurstGoMash = 29
    case exBurstLevel = 30
    /// Universal PPSSPP OSK reader. OskToggle is a player button (tap on OSK
    /// entry and exit, like custToggle); the rest are auto-forwarded by the
    /// host on game-pad down-edges and stay out of supported(adapterID:).
    case oskToggle = 31
    case oskType = 32
    case oskDelete = 33
    case oskSpace = 34
    case oskShift = 35
    case oskFinish = 36
    /// Player stop key. Sent by the Stop-speech button, never shown as a
    /// button: it clears the announcement queue and stops the platform voice.
    /// Append-only: raw values are the C ABI shared with the core.
    case stopSpeech = 37

    /// Spoken-history walk. CORE-INTERNAL like stopSpeech: the core intercepts them
    /// before adapter dispatch, so they work on any game — the Lua script, or a game
    /// with no adapter — because the history lives in the announcement queue.
    /// Append-only: raw values are the C ABI shared with the core.
    case repeatNewest = 38
    case repeatOlder = 39

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
        case .menuState:       return "Menu status"
        case .menuNext:        return "Next item"
        case .menuPrev:        return "Previous item"
        case .menuLeft:        return "Previous value"
        case .menuRight:       return "Next value"
        case .custToggle:      return "Customize row"
        case .charToggle:       return "Character"
        case .quickOn:          return "Quickmove available"
        case .quickOff:         return "Quickmove gone"
        case .exReady: return "EX ready"
        case .exSpent: return "EX spent"
        case .exActive: return "EX Mode on"
        case .exEnded: return "EX Mode over"
        case .exBurstGo: return "EX Burst go"
        case .exQteUp: return "QTE up"
        case .exQteDown: return "QTE down"
        case .exQteLeft: return "QTE left"
        case .exQteRight: return "QTE right"
        case .exQteCircle: return "QTE Circle"
        case .exQteSquare: return "QTE Square"
        case .exQteTriangle: return "QTE Triangle"
        case .exQteCross: return "QTE Cross"
        case .exBurstGoMash: return "Mash Burst Go"
        case .exBurstLevel: return "Burst Level"
        case .oskToggle: return "Name entry"
        case .oskType: return "OSK type"
        case .oskDelete: return "OSK delete"
        case .oskSpace: return "OSK space"
        case .oskShift: return "OSK shift"
        case .oskFinish: return "OSK finish"
        case .stopSpeech: return "Stop speech"
        case .repeatNewest: return "Repeat"
        case .repeatOlder:  return "Repeat older"
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
        case .menuState:
            return "Reports which menu is live, for the host's menu tracker."
        case .menuNext:
            return "Next row on a menu, or the next target on a field map. Reads it."
        case .menuPrev:
            return "Previous row on a menu, or the previous target on a field map. Reads it."
        case .menuLeft:
            return "Previous value on this options row."
        case .menuRight:
            return "Next value on this options row."
        case .custToggle:
            return "Tracks the Customize menu. Tap when opening it and when leaving it."
        case .charToggle:
            return "Tracks character select. Tap when opening it and when leaving it."
        case .quickOn:
            return "Sent by the marker detector when Quickmove appears."
        case .quickOff:
            return "Sent by the marker detector when Quickmove vanishes."
        case .exReady:
            return "Detector: EX gauge turned yellow (full)."
        case .exSpent:
            return "Detector: EX gauge no longer full."
        case .exActive:
            return "Sent after the player presses R+Square."
        case .exEnded:
            return "EX gauge drained or Burst finished."
        case .exBurstGo:
            return "HP attack landed in EX Mode; Square prompt live."
        case .exQteUp:
            return "Burst minigame prompt: d-pad up."
        case .exQteDown:
            return "Burst minigame prompt: d-pad down."
        case .exQteLeft:
            return "Burst minigame prompt: d-pad left."
        case .exQteRight:
            return "Burst minigame prompt: d-pad right."
        case .exQteCircle:
            return "Burst minigame prompt: Circle."
        case .exQteSquare:
            return "Burst minigame prompt: Square."
        case .exQteTriangle:
            return "Burst minigame prompt: Triangle."
        case .exQteCross:
            return "Burst minigame prompt: Cross."
        case .exBurstGoMash:
            return "Mash-type Burst started: mash the button."
        case .exBurstLevel:
            return "Mash-type Burst power level up."
        case .oskToggle:
            return "Tracks the system name-entry keyboard. Tap when it opens and when leaving it."
        case .oskType:
            return "Sent when the player presses Cross on the name-entry keyboard."
        case .oskDelete:
            return "Sent when the player presses Circle on the name-entry keyboard."
        case .oskSpace:
            return "Sent when the player presses Square on the name-entry keyboard."
        case .oskShift:
            return "Sent when the player presses Select on the name-entry keyboard."
        case .oskFinish:
            return "Sent when the player presses Start on the name-entry keyboard."
        case .stopSpeech:
            return "Sent by the Stop-speech button; clears queued announcements."
        case .repeatNewest:
            return "Speaks the last announcement again, whatever the game."
        case .repeatOlder:
            return "Steps one announcement further back and speaks it."
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
        case .menuState:       return "list.bullet.rectangle"
        case .menuNext:        return "chevron.down"
        case .menuPrev:        return "chevron.up"
        case .menuLeft:        return "chevron.left"
        case .menuRight:       return "chevron.right"
        case .custToggle:       return "slider.horizontal.3"
        case .charToggle:        return "person"
        case .quickOn:          return "bolt"
        case .quickOff:         return "bolt.slash"
        case .exReady: return "bolt.fill"
        case .exSpent: return "bolt"
        case .exActive: return "flame.fill"
        case .exEnded: return "flame"
        case .exBurstGo: return "exclamationmark.triangle.fill"
        case .exQteUp: return "arrow.up"
        case .exQteDown: return "arrow.down"
        case .exQteLeft: return "arrow.left"
        case .exQteRight: return "arrow.right"
        case .exQteCircle: return "circle"
        case .exQteSquare: return "square"
        case .exQteTriangle: return "triangle"
        case .exQteCross: return "xmark"
        case .exBurstGoMash: return "repeat"
        case .exBurstLevel: return "arrow.up"
        case .oskToggle: return "keyboard"
        case .oskType: return "character"
        case .oskDelete: return "delete.left"
        case .oskSpace: return "space"
        case .oskShift: return "shift"
        case .oskFinish: return "checkmark"
        case .stopSpeech: return "stop.fill"
        case .repeatNewest: return "arrow.counterclockwise"
        case .repeatOlder:  return "arrow.uturn.backward"
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
            // Dissidia PSP: location, menu-row navigation, Customize tracking,
            // name-entry tracking, and debug dump are real. menuState is
            // host-programmatic (menu tracker), not a button.
            return [.whereAmI, .menuNext, .menuPrev, .menuLeft, .menuRight,
                    .custToggle, .charToggle, .oskToggle, .dumpState]

        default:
            return []
        }
    }
}
