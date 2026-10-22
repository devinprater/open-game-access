import SwiftUI
import UniformTypeIdentifiers
import CPokeCore

/// One game that has a native reader, as the app's library presents it.
///
/// The data comes from the adapter registry in C, never from a Swift list, so a
/// reader that ships without its library entry is impossible: add the adapter and
/// it appears here. Console-only placeholder rows are filtered out in C.
struct AdapterGame: Identifiable, Hashable {
    let id: String
    let name: String
    let code: String
    let coverage: String
    let notes: String
    let console: String

    /// "Nintendo DS", "PlayStation Portable", and so on. Spoken on the game page.
    var consoleName: String {
        switch console {
        case "nds":  return "Nintendo DS"
        case "gb":   return "Game Boy, Game Boy Color and Game Boy Advance"
        case "psp":  return "PlayStation Portable"
        case "snes": return "Super Nintendo"
        case "nes":  return "Nintendo Entertainment System"
        case "n64":  return "Nintendo 64"
        default:     return "this console"
        }
    }

    /// The one line naming the file types the Load Game picker offers.
    var formatLine: String {
        switch console {
        case "nds":  return "The picker shows Nintendo DS .nds files."
        case "gb":   return "The picker shows .gba, .gbc and .gb files."
        case "psp":  return "The picker shows PSP .iso, .cso and .pbp files."
        case "snes": return "The picker shows .sfc, .smc, .fig and .swc files."
        case "nes":  return "The picker shows .nes, .fds and .unf files."
        default:     return "The picker shows the file types this console uses."
        }
    }

    /// The supported-version line, when the registry names a code worth showing.
    /// A CRC is a checksum, not a player-facing code, so it is worded as one.
    var versionLine: String? {
        if code.hasPrefix("crc:") {
            return "Verified against the ROM with checksum \(code.dropFirst(4))."
        }
        if code.isEmpty { return nil }
        return "Supported game code: \(code)."
    }

    /// The file picker filter for this console. An unknown console gets the
    /// app's full set rather than an empty picker, which would look broken.
    var contentTypes: [UTType] {
        let exts: [String]
        switch console {
        case "nds":  exts = ["nds"]
        case "gb":   exts = ["gba", "gbc", "gb"]
        case "psp":  exts = ["iso", "cso", "pbp"]
        case "snes": exts = ["sfc", "smc", "fig", "swc"]
        case "nes":  exts = ["nes", "fds", "unf"]
        case "n64":  exts = ["z64", "n64", "v64"]
        default:     return ROMStore.allowedTypes
        }
        let types = exts.compactMap { UTType(filenameExtension: $0) }
        return types.isEmpty ? ROMStore.allowedTypes : types
    }
}

/// The registry, read once per launch. Every pointer the C side returns is
/// static storage, so copying the strings here is safe and keeps Swift from
/// holding a C pointer across view updates.
enum AdapterLibrary {
    static func games() -> [AdapterGame] {
        func text(_ p: UnsafePointer<CChar>?) -> String {
            guard let p else { return "" }
            return String(cString: p)
        }

        let count = Int(poke_adapter_library_count())
        var out: [AdapterGame] = []
        out.reserveCapacity(count)
        for i in 0..<count {
            let id = text(poke_adapter_library_id(Int32(i)))
            guard !id.isEmpty else { continue }
            out.append(AdapterGame(
                id: id,
                name: text(poke_adapter_library_name(Int32(i))),
                code: text(poke_adapter_library_code(Int32(i))),
                coverage: text(poke_adapter_library_coverage(Int32(i))),
                notes: text(poke_adapter_library_notes(Int32(i))),
                console: text(poke_adapter_library_console(Int32(i)))
            ))
        }
        return out
    }
}

/// The home screen: the games that have readers, not a game screen. A player
/// who has not loaded anything has nothing to look at on an emulator screen, so
/// that screen is not shown until there is a game for it.
struct AdapterLibraryView: View {
    @EnvironmentObject private var session: GameSession
    @EnvironmentObject private var speech: SpeechEngine

    @State private var showingOtherPicker = false

    private let games = AdapterLibrary.games()

    var body: some View {
        List {
            Section {
                ForEach(games) { game in
                    NavigationLink {
                        AdapterGamePage(game: game)
                    } label: {
                        VStack(alignment: .leading, spacing: 3) {
                            Text(game.name)
                            if !game.coverage.isEmpty {
                                Text(game.coverage)
                                    .font(.footnote)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                    .accessibilityElement(children: .ignore)
                    .accessibilityLabel(game.coverage.isEmpty
                                        ? game.name
                                        : "\(game.name). \(game.coverage).")
                    .accessibilityHint("Opens the reader notes and the Load Game button.")
                }
            } header: {
                Text("Games with readers")
                    // SECTION HEADERS ARE UPPERCASED BY THE LIST STYLE. A screen
                    // reader spells an all-caps heading letter by letter ("G A M E S"),
                    // so the style must not change the words.
                    .textCase(.none)
            } footer: {
                Text("Each game opens notes on what its reader speaks, the controls it adds, and a Load Game button that shows only that console's files.")
            }

            Section {
                Button {
                    showingOtherPicker = true
                } label: {
                    Label("Load another game", systemImage: "folder")
                }
                .accessibilityHint("Opens the file picker for any supported file. A game with no reader will run without speech or cues.")
            } footer: {
                Text("A game that is not listed can still run, but it has no reader: no speech and no cues.")
            }
        }
        .fileImporter(
            isPresented: $showingOtherPicker,
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

/// One game's page: what the reader does, the controls it adds, and the one
/// action this page exists for — load that game.
struct AdapterGamePage: View {
    let game: AdapterGame

    @EnvironmentObject private var session: GameSession
    @EnvironmentObject private var speech: SpeechEngine
    @Environment(\.dismiss) private var dismiss

    @State private var showingPicker = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text(game.name)
                    .font(.title2)
                    .accessibilityAddTraits(.isHeader)

                Text(game.consoleName)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                if !game.coverage.isEmpty {
                    Text(game.coverage)
                        .font(.headline)
                }

                if let version = game.versionLine {
                    Text(version)
                        .font(.footnote)
                }

                if game.notes.isEmpty {
                    Text("No notes have been written for this game yet.")
                } else {
                    Text(game.notes)
                }

                Button {
                    showingPicker = true
                } label: {
                    Label("Load game", systemImage: "play.circle")
                        .frame(maxWidth: .infinity, minHeight: 48)
                }
                .buttonStyle(.borderedProminent)
                .accessibilityLabel("Load \(game.name)")
                .accessibilityHint(game.formatLine)

                Text(game.formatLine)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding()
        }
        .navigationTitle(game.name)
        .navigationBarTitleDisplayMode(.inline)
        .fileImporter(
            isPresented: $showingPicker,
            allowedContentTypes: game.contentTypes,
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                guard let url = urls.first else { return }
                session.loadROM(at: url)
                // Leave this page either way: on success the root is now the game
                // screen, and on failure the root says what went wrong. Staying here
                // would hide the result behind a pushed page.
                dismiss()
            case .failure(let error):
                speech.announce("Could not open that file: \(error.localizedDescription)")
            }
        }
    }
}
