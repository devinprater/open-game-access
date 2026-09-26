import Foundation
import CPokeCore

/// The console a loaded ROM runs on, as the PLAYER experiences it.
///
/// The C core already branches per backend (melonDS / mGBA / PPSSPP); this is
/// the UI half of that same fact: how many screens the console has, which
/// buttons exist on its pad, and which of the Lua script's hotkeys the loaded
/// reader actually answers.
///
/// ⛔ THE HOTKEY SETS ARE PER-SYSTEM BECAUSE THE SCRIPTS DISAGREE. The DS
/// script and the Pokémon (Game Boy) readers both listen for single-letter
/// keys, but they bind different letters to different actions — R is "stop
/// speech" on the DS and "move the camera up" on Game Boy. Showing the DS
/// button set for a Game Boy game would not show a dead button, it would
/// show a button that does something else. Every key listed below was read
/// out of the script that runs for that system.
enum GameSystem {
    case ds
    case gameBoy // gb/gbc: two face buttons, no shoulders
    case gameBoyAdvance // gba: two face buttons plus shoulders
    case psp // one screen, four face buttons, no script keys at all

    /// The system for a ROM file extension, or nil for something the app
    /// cannot run. Nil keeps an unrecognized file out of the picker-facing
    /// paths instead of loading it as the wrong console.
    static func forROMExtension(_ ext: String) -> GameSystem? {
        switch ext.lowercased() {
        case "nds": return .ds
        case "gb", "gbc": return .gameBoy
        case "gba": return .gameBoyAdvance
        case "iso", "cso", "pbp", "elf": return .psp
        default: return nil
        }
    }

    var name: String {
        switch self {
        case .ds: return "Nintendo DS"
        case .gameBoy: return "Game Boy"
        case .gameBoyAdvance: return "Game Boy Advance"
        case .psp: return "PlayStation Portable"
        }
    }

    /// Screens the console draws. Only the DS has two; the Settings screen
    /// picker exists for it alone.
    var screenCount: Int {
        switch self {
        case .ds: return 2
        default: return 1
        }
    }

    /// Shoulder buttons exist on everything here except the original Game Boy.
    var hasShoulders: Bool {
        switch self {
        case .gameBoy: return false
        default: return true
        }
    }

    /// One face-button descriptor per button the console actually has. The raw
    /// value is the shared pad index (POKE_BTN_*): the C core maps it into
    /// each backend (A confirms on all three — Cross on PSP), so the UI sends
    /// the same numbers everywhere and only the labels change.
    var faceButtons: [(title: String, symbol: String, hint: String, raw: Int32)] {
        switch self {
        case .ds:
            return [
                ("A", "a.circle", "Confirm, talk, select", POKE_BTN_A),
                ("B", "b.circle", "Cancel, back", POKE_BTN_B),
                ("X", "x.circle", "Menu", POKE_BTN_X),
                ("Y", "y.circle", "Use item", POKE_BTN_Y),
            ]
        case .gameBoy, .gameBoyAdvance:
            // X/Y have no Game Boy equivalent (the core ignores them), so
            // they are absent, not disabled: a blind player cannot tell a
            // greyed-out button from a broken one.
            return [
                ("A", "a.circle", "Confirm, talk, select", POKE_BTN_A),
                ("B", "b.circle", "Cancel, back", POKE_BTN_B),
            ]
        case .psp:
            return [
                ("Cross", "xmark.circle", "Cross button, confirm", POKE_BTN_A),
                ("Circle", "circle", "Circle button, cancel", POKE_BTN_B),
                ("Triangle", "triangle", "Triangle button", POKE_BTN_X),
                ("Square", "square", "Square button", POKE_BTN_Y),
            ]
        }
    }

    /// Script hotkeys for finding places. The Game Boy reader answers P
    /// (pathfind) and E (tiles) but C ("where am I") is unbound there — its
    /// equivalent is M (map name), which has no button yet.
    var pathKeys: [(title: String, key: String, hint: String)] {
        switch self {
        case .ds:
            return [
                ("Find path", "P", "Guides you to the selected place, turn by turn. Press again to stop. Key P."),
                ("Where am I", "C", "Reads the place name and your coordinates. Key C."),
                ("Tiles", "E", "Reads the tiles around you. Only useful where the ground matters, such as the dark. Key E."),
            ]
        case .gameBoy, .gameBoyAdvance:
            return [
                ("Find path", "P", "Guides you to the selected place, turn by turn. Press again to stop. Key P."),
                ("Tiles", "E", "Reads the tiles around you. Only useful where the ground matters, such as the dark. Key E."),
            ]
        case .psp:
            // Hotkeys are inert on PSP (the C core ignores them); the native
            // adapter is the only reader. No buttons, not dead ones.
            return []
        }
    }

    /// Script hotkeys for reading lists. The Game Boy reader answers K/J/L
    /// (read/previous/next item) but not I/O group switching — there it is
    /// shift-J/shift-L, which a single tap cannot send.
    var readingKeys: [(title: String, key: String, hint: String)] {
        switch self {
        case .ds:
            return [
                ("Read item", "K", "Reads the selected item again. Key K."),
                ("Previous item", "J", "Moves to the previous item. Key J."),
                ("Next item", "L", "Moves to the next item. Key L."),
                ("Previous group", "I", "Previous group of items, such as people or signs. Key I."),
                ("Next group", "O", "Next group of items. Key O."),
            ]
        case .gameBoy, .gameBoyAdvance:
            return [
                ("Read item", "K", "Reads the selected item again. Key K."),
                ("Previous item", "J", "Moves to the previous item. Key J."),
                ("Next item", "L", "Moves to the next item. Key L."),
            ]
        case .psp:
            return []
        }
    }

    /// Script hotkeys that act on speech itself. DS-only: on Game Boy, R
    /// moves the camera and U is unbound, so a "Stop speech" button there
    /// would either do nothing or do harm. Stopping adapter speech on
    /// scriptless systems needs a direct speech-stop path that does not
    /// exist yet.
    var speechKeys: [(title: String, key: String, hint: String)] {
        switch self {
        case .ds:
            return [
                ("Stop speech", "R", "Stops the current speech. Key R."),
                ("Repeat", "U", "Repeats the last thing said. Key U."),
            ]
        case .gameBoy, .gameBoyAdvance, .psp:
            return []
        }
    }
}
