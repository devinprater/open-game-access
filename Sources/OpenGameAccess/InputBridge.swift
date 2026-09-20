import Foundation
import SwiftUI
import UniformTypeIdentifiers
import CPokeCore

/// The DS buttons, named as the player thinks of them.
enum DSButton {
    case a, b, x, y, start, select, up, down, left, right, l, r

    /// melonDS's own key bit order (NDS::SetKeyMask): A,B,Select,Start,Right,
    /// Left,Up,Down,R,L,X,Y.
    var rawValue: Int32 {
        switch self {
        case .a: return POKE_BTN_A
        case .b: return POKE_BTN_B
        case .select: return POKE_BTN_SELECT
        case .start: return POKE_BTN_START
        case .right: return POKE_BTN_RIGHT
        case .left: return POKE_BTN_LEFT
        case .up: return POKE_BTN_UP
        case .down: return POKE_BTN_DOWN
        case .r: return POKE_BTN_R
        case .l: return POKE_BTN_L
        case .x: return POKE_BTN_X
        case .y: return POKE_BTN_Y
        }
    }
}

/// InputBridge is the single path from the UI into the core's input state.
///
/// It holds one `GameSession`-owned reference so the button views do not each
/// need the core pointer, and it keeps the held-button state so a button view
/// disappearing mid-press (the controls hide when the game stops) cannot leave
/// a key stuck down in the emulated console.
@MainActor
enum InputBridge {
    private static var session: GameSession?
    private static var held: Set<Int32> = []
    private static var heldHotkeys: Set<String> = []

    static func connect(_ session: GameSession) {
        self.session = session
    }

    static func setButton(_ button: DSButton, down: Bool) {
        let raw = button.rawValue
        if down { held.insert(raw) } else { held.remove(raw) }
        session?.setButton(raw, down: down)
    }

    /// Hotkeys are edge-triggered by the script: send down, then up a frame or
    /// two later so the script's per-frame edge detector sees exactly one press.
    static func tapHotkey(_ key: String) {
        guard !heldHotkeys.contains(key) else { return }
        heldHotkeys.insert(key)
        session?.setHotkey(key, down: true)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.08) {
            heldHotkeys.remove(key)
            session?.setHotkey(key, down: false)
        }
    }

    /// Release everything — called when the game stops or the app backgrounds.
    static func releaseAll() {
        for raw in held { session?.setButton(raw, down: false) }
        held.removeAll()
        for key in heldHotkeys { session?.setHotkey(key, down: false) }
        heldHotkeys.removeAll()
    }
}

/// ROMStore keeps games and saves inside the app container.
///
/// The DS needs the ROM as a real file it can seek in, and it writes saves
/// beside it, so picker results are copied in rather than referenced.
enum ROMStore {
    static var allowedTypes: [UTType] {
        // .nds has no system type; declare it locally so the picker can filter.
        var types: [UTType] = []
        if let nds = UTType(filenameExtension: "nds") { types.append(nds) }
        // Game Boy ROMs run in the mGBA core (see Core/gba_core.cpp).
        if let gba = UTType(filenameExtension: "gba") { types.append(gba) }
        if let gbc = UTType(filenameExtension: "gbc") { types.append(gbc) }
        if let gb = UTType(filenameExtension: "gb") { types.append(gb) }
        types.append(.data)
        return types
    }

    static var romDirectory: URL {
        let base = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("Games", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    static var stateDirectory: URL {
        let base = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("States", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    static func importROM(from url: URL) throws -> URL {
        let destination = romDirectory.appendingPathComponent(url.lastPathComponent)
        if FileManager.default.fileExists(atPath: destination.path) {
            // Re-selecting the same game should not re-copy a 128 MB ROM.
            return destination
        }
        try FileManager.default.copyItem(at: url, to: destination)
        return destination
    }

    /// melonDS writes the .sav next to the ROM it was given.
    static func savePath(for rom: URL) -> URL {
        rom.deletingPathExtension().appendingPathExtension("sav")
    }

    static var statePath: URL {
        stateDirectory.appendingPathComponent("quicksave.state")
    }

    /// Games already imported, newest first, for the "recent games" list.
    static func recentROMs() -> [URL] {
        let contents = (try? FileManager.default.contentsOfDirectory(
            at: romDirectory,
            includingPropertiesForKeys: [.contentModificationDateKey],
            options: [.skipsHiddenFiles]
        )) ?? []
        return contents
            .filter { ["nds", "gba", "gbc", "gb"].contains($0.pathExtension.lowercased()) }
            .sorted { a, b in
                let da = (try? a.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
                let db = (try? b.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
                return da > db
            }
    }
}

/// Bundle resources live in a copied folder, so look them up by name+extension
/// rather than by subdirectory.
enum BundleResources {
    static func text(named name: String, ext: String) -> String? {
        let url = Bundle.main.url(forResource: name, withExtension: ext)
            ?? Bundle.module.url(forResource: name, withExtension: ext)
        guard let url, let data = try? Data(contentsOf: url) else { return nil }
        return String(data: data, encoding: .utf8)
    }

    /// Filesystem path of the bundled Pokémon Access reader set (gba-lua/),
    /// handed to poke_set_script_dir for Game Boy ROMs. Nil when the resource
    /// is missing — the core then fails loudly at start, it does not boot a
    /// game with no reader.
    static var gbaScriptDir: String? {
        let base = Bundle.module.resourceURL ?? Bundle.main.resourceURL
        let url = base?.appendingPathComponent("Resources/gba-lua", isDirectory: true)
        guard let url, FileManager.default.fileExists(atPath: url.path) else { return nil }
        return url.path
    }
}
