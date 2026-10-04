import Foundation
import CPokeCore

/// The console a loaded ROM runs on, as the PLAYER experiences it.
///
/// ⛔ TWO KINDS OF FACT LIVE NEAR HERE, AND THEY MUST NOT BE MERGED.
///
/// HARDWARE facts — how many screens, which buttons exist, shoulder buttons,
/// analog sticks — come from the CORE's registry (`Core/systems.{h,cpp}`), read
/// through the accessors in pokecore.h. That is the single source of truth, it
/// lists every console the app knows (not just the four with cores), and adding
/// a console is a row there rather than an edit here. This type carries a
/// registry HANDLE; it does not keep its own copy of those facts.
///
/// SCRIPT facts — which letter repeats speech, which key pathfinds — stay HERE,
/// keyed by console, and that is not a compromise. They belong to the loaded
/// LUA SCRIPT, not to the hardware: the DS reader binds R to "stop speech" while
/// the Game Boy reader binds R to "move the camera up". They have different
/// lifetimes from a console's hardware and a console can have several scripts or
/// none. Moving them into the registry would bake Pokémon Access into the core,
/// which is the coupling the registry exists to remove. Every key below was read
/// out of the script that runs for that system.
struct GameSystem: Equatable {
    /// The registry row for this console, kept as an OpaquePointer for identity
    /// and Equatable. The C accessors take `const void*`, which Swift imports as
    /// `UnsafeRawPointer` for a PARAMETER (a returned `const void*` imports as
    /// OpaquePointer instead — the two are not interchangeable), so every call
    /// goes through `raw` below rather than converting at each site.
    let handle: OpaquePointer

    /// The handle as the C ABI expects it. One place, because the conversion
    /// error otherwise appears at every individual call site.
    private var raw: UnsafeRawPointer { UnsafeRawPointer(handle) }
    /// Console id (OgaSystemId), for the switch statements that must exist.
    let id: Int32

    /// What the pad shows BEFORE a ROM is loaded.
    ///
    /// ⛔ The DS, deliberately, and it is the only honest choice available: the
    /// registry answers "which console is this FILE", and at this point there is
    /// no file. Every view that draws the pad reads `session.system ?? DSDefault`
    /// rather than inventing its own answer, so changing the pre-load default
    /// changes it everywhere.
    /// ⛔ @MainActor, NOT A WORKAROUND. GameSystem holds an OpaquePointer into the
    /// core's registry, which is genuinely not Sendable, and every caller of this
    /// is a SwiftUI view. Swift 6 therefore rejects a plain `static let` of a
    /// non-Sendable type; the global-actor annotation is the accurate constraint.
    @MainActor
    static let DSDefault: GameSystem = {
        // The DS is id 1 and is always in the registry, but this is the ONE place
        // the app would trap if it were not — so fall back to an empty row rather
        // than force-unwrapping a pointer at launch.
        guard let h = oga_system_by_id(1) else {
            return GameSystem(handle: OpaquePointer(bitPattern: 1)!, id: 0)
        }
        return GameSystem(handle: OpaquePointer(h), id: 1)
    }()

    // MARK: - The registry, read once per console

    /// The system for a ROM file extension, or nil for something the app
    /// cannot run.
    ///
    /// ⛔ nil is the whole point: an unrecognised extension must NOT resolve to
    /// a console, or the file gets loaded into the wrong emulator. That default
    /// is what once made a .gba try to boot melonDS.
    static func forROMExtension(_ ext: String) -> GameSystem? {
        guard let h = ext.withCString({ oga_system_for_path($0) }) else { return nil }
        return GameSystem(handle: OpaquePointer(h), id: oga_system_id(h))
    }

    /// Every console the app knows, INCLUDING ones with no core in this build.
    /// The picker reads this so it can name a file it cannot run yet.
    static var all: [GameSystem] {
        var count: Int32 = 0
        guard let arr = oga_all_system_handles(&count) else { return [] }
        return (0..<Int(count)).compactMap { i in
            guard let h = arr[i] else { return nil }
            return GameSystem(handle: OpaquePointer(h), id: oga_system_id(h))
        }
    }

    /// Whether this build has a core that can actually load this console.
    /// The picker uses it to decide between "play" and an explanation.
    var isRunnable: Bool { oga_system_is_runnable(raw) }

    /// What to say about a file this build cannot run. Names the extension, or
    /// the console when the file was understood. Never empty.
    var unsupportedReason: String {
        guard let c = oga_unsupported_reason("") else { return "That file cannot be played." }
        return String(cString: c)
    }

    // MARK: - Hardware facts (from the registry)

    var name: String {
        guard let c = oga_system_name(raw) else { return "" }
        return String(cString: c)
    }

    /// Screens the console draws. Only the DS family has two; the Settings
    /// screen picker exists for those alone.
    var screenCount: Int { Int(oga_system_screen_count(raw)) }

    var hasShoulders: Bool { oga_system_has_shoulders(raw) }

    var analogSticks: Int { Int(oga_system_analog_sticks(raw)) }

    /// One face-button descriptor per button the console ACTUALLY HAS, straight
    /// from the registry. A blind player cannot tell a greyed-out button from a
    /// broken one, so a console must not advertise a button its hardware lacks —
    /// which is why this is not a switch with a default case.
    var faceButtons: [(title: String, symbol: String, hint: String, raw: Int32)] {
        let max = 8
        var titles = [UnsafePointer<CChar>?](repeating: nil, count: max)
        var symbols = [UnsafePointer<CChar>?](repeating: nil, count: max)
        var hints = [UnsafePointer<CChar>?](repeating: nil, count: max)
        var raws = [Int32](repeating: 0, count: max)
        let n = Int(oga_system_face_buttons(raw, &titles, &symbols, &hints, &raws, Int32(max)))
        return (0..<n).map { i in
            (title: titles[i].map { String(cString: $0) } ?? "",
             symbol: symbols[i].map { String(cString: $0) } ?? "",
             hint: hints[i].map { String(cString: $0) } ?? "",
             raw: raws[i])
        }
    }

    // MARK: - Console identity, for the script tables below

    /// The registry ids, spelled out so the switches below cannot silently
    /// mismatch a number. These MUST match OgaSystemId in Core/systems.h, which
    /// is append-only because the values cross this boundary.
    private enum ID {
        static let ds: Int32 = 1
        static let gameBoy: Int32 = 2
        static let gameBoyAdvance: Int32 = 3
        static let psp: Int32 = 4
    }

    /// True for the Game Boy family (GB/GBC/GBA), which share the Pokémon
    /// Access reader and therefore its key bindings.
    private var isGameBoyFamily: Bool {
        id == ID.gameBoy || id == ID.gameBoyAdvance
    }

    // MARK: - SCRIPT facts (the loaded reader's keys — see the note at the top)

    /// Whether the system has any script speech keys. When it does not (Game
    /// Boy, PSP), the speech group offers a direct engine stop instead of
    /// hotkey buttons.
    var hasDirectSpeechStop: Bool {
        if id == ID.ds { return false }
        return true
    }

    /// Script hotkeys for finding places. The Game Boy reader answers P
    /// (pathfind) and E (tiles) but C ("where am I") is unbound there — its
    /// equivalent is M (map name).
    var pathKeys: [(title: String, key: String, hint: String)] {
        if id == ID.ds {
            return [
                ("Find path", "P", "Guides you to the selected place, turn by turn. Press again to stop. Key P."),
                ("Where am I", "C", "Reads the place name and your coordinates. Key C."),
                ("Tiles", "E", "Reads the tiles around you. Only useful where the ground matters, such as the dark. Key E."),
            ]
        }
        if isGameBoyFamily {
            return [
                ("Find path", "P", "Guides you to the selected place, turn by turn. Press again to stop. Key P."),
                ("Where am I", "M", "Reads the current map name. Key M."),
                ("Tiles", "E", "Reads the tiles around you. Only useful where the ground matters, such as the dark. Key E."),
            ]
        }
        // Hotkeys are inert on PSP (the core ignores them); the native adapter
        // is the only reader. No buttons, not dead ones.
        return []
    }

    /// Script hotkeys for reading lists. The Game Boy reader answers K/J/L
    /// (read/previous/next item) but not I/O group switching — there it is
    /// shift-J/shift-L, which a single tap cannot send.
    var readingKeys: [(title: String, key: String, hint: String)] {
        if id == ID.ds {
            return [
                ("Read item", "K", "Reads the selected item again. Key K."),
                ("Previous item", "J", "Moves to the previous item. Key J."),
                ("Next item", "L", "Moves to the next item. Key L."),
                ("Previous group", "I", "Previous group of items, such as people or signs. Key I."),
                ("Next group", "O", "Next group of items. Key O."),
            ]
        }
        if isGameBoyFamily {
            return [
                ("Read item", "K", "Reads the selected item again. Key K."),
                ("Previous item", "J", "Moves to the previous item. Key J."),
                ("Next item", "L", "Moves to the next item. Key L."),
            ]
        }
        return []
    }

    /// Script hotkeys that act on speech itself. DS-only: on Game Boy, R moves
    /// the camera and U is unbound, so hotkey buttons there would either do
    /// nothing or do harm. Systems without speech keys get a direct engine stop
    /// instead (see SpeechGroup) — no script key needed.
    var speechKeys: [(title: String, key: String, hint: String)] {
        if id == ID.ds {
            return [
                ("Stop speech", "R", "Stops the current speech. Key R."),
                ("Repeat", "U", "Repeats the last thing said. Key U."),
            ]
        }
        return []
    }
}
