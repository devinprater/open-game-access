import Foundation
import SwiftUI
import UniformTypeIdentifiers
import CPokeCore

/// The pad buttons, named as the player thinks of them.
///
/// The raw values are the shared pad indices (POKE_BTN_*, melonDS bit order);
/// the C core maps them per backend (A confirms everywhere — Cross on PSP),
/// so one enum serves every console and only the UI labels change per system.
enum GamePadButton {
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

    static func setButton(_ button: GamePadButton, down: Bool) {
        let raw = button.rawValue
        if down { held.insert(raw) } else { held.remove(raw) }
        session?.setButton(raw, down: down)
        // Universal OSK echo: the pad button the game also received is echoed
        // to the reader on the down-edge. Pad ids are console-mapped by the
        // core (A confirms everywhere — Cross on PSP), so one mapping serves
        // every system; adapters without a live OSK ignore these silently.
        if down {
            switch button {
            case .a: session?.forwardPadCommand(.oskType)
            case .b: session?.forwardPadCommand(.oskDelete)
            case .x: session?.forwardPadCommand(.oskSpace)
            case .start: session?.forwardPadCommand(.oskFinish)
            case .select: session?.forwardPadCommand(.oskShift)
            // Tracked-menu echo: the game also received this D-pad edge, so the
            // reader moves its own cursor alongside it (the MenuNext/Prev/Left/
            // Right contract in Core/adapter.h). Silent: adapters with no tracked
            // menu ignore these; RAM-gated menus just re-announce the settled row.
            case .up: session?.forwardPadCommand(.menuPrev)
            case .down: session?.forwardPadCommand(.menuNext)
            case .left: session?.forwardPadCommand(.menuLeft)
            case .right: session?.forwardPadCommand(.menuRight)
            default: break
            }
        }
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
        // PSP images run in the PPSSPP core (see Core/psp_core.cpp).
        if let iso = UTType(filenameExtension: "iso") { types.append(iso) }
        if let cso = UTType(filenameExtension: "cso") { types.append(cso) }
        if let pbp = UTType(filenameExtension: "pbp") { types.append(pbp) }
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
            .filter { ["nds", "gba", "gbc", "gb", "iso", "cso", "pbp"].contains($0.pathExtension.lowercased()) }
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
    /// Filesystem path of the bundled PPSSPP runtime assets (ppsspp-assets/).
    /// Two homes, checked in order: the bundle root (put there by
    /// scripts/package-sim-app.sh or a manual stage), then the SwiftPM
    /// resource bundle (Sources/OpenGameAccess/Resources/ppsspp-assets,
    /// vendored from the pinned PPSSPP — this is what the xtool device
    /// build carries, since its packager re-assembles the .app and wipes
    /// anything staged post-hoc). Nil when both are missing — the PSP core
    /// then fails loudly at load, it does not boot a game with no compat
    /// tables.
    static var ppssppAssetDir: String? {
        let fm = FileManager.default
        let atRoot = Bundle.main.bundleURL.appendingPathComponent("ppsspp-assets", isDirectory: true)
        if fm.fileExists(atPath: atRoot.path) { return atRoot.path }
        if let base = Bundle.module.resourceURL?.appendingPathComponent("ppsspp-assets", isDirectory: true),
           fm.fileExists(atPath: base.path) { return base.path }
        return nil
    }

    /// Human-readable account of where the PSP assets were looked for, for
    /// the load-failure message. The core's own error only names the dir it
    /// got (often the unset relative fallback), which cannot tell a stale
    /// install from a wrong resource path — this can.
    static var ppssppAssetDiagnosis: String {
        let atRoot = Bundle.main.bundleURL.appendingPathComponent("ppsspp-assets", isDirectory: true).path
        let modBase = Bundle.module.resourceURL?.appendingPathComponent("Resources/ppsspp-assets", isDirectory: true).path
        let fm = FileManager.default
        let modList = (try? fm.contentsOfDirectory(atPath: Bundle.module.resourceURL?.path ?? "")) ?? []
        return "bundle-root: \(atRoot) (\(fm.fileExists(atPath: atRoot) ? "present" : "absent")); " +
            "module: \(modBase ?? "nil") (\(modBase != nil && fm.fileExists(atPath: modBase!) ? "present" : "absent")); " +
            "bundle contents: \(modList.joined(separator: ","))"
    }

    static var gbaScriptDir: String? {
        // .copy("Resources") flattens: the bundle holds gba-lua/ at top level,
        // not under Resources/ (proven by on-device bundle listing 2026-09-27).
        let base = Bundle.module.resourceURL ?? Bundle.main.resourceURL
        let url = base?.appendingPathComponent("gba-lua", isDirectory: true)
        guard let url, FileManager.default.fileExists(atPath: url.path) else { return nil }
        return url.path
    }
}
