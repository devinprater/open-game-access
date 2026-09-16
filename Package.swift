// swift-tools-version: 6.0
import Foundation
import PackageDescription

// Open Game Access — iOS.
//
// The emulation core (melonDS + the Lua accessibility layer) is compiled as a
// static archive by scripts/build-core.sh; Sources/CPokeCore is the only thing
// Swift is allowed to see of it.
//
// The archive is force-loaded: nothing in Swift takes the address of a poke_*
// entry point, so without -force_load the linker drops every object in it that
// no Swift symbol referenced, and the app dies at the first core call.
//
// ⛔ THE PATH MUST BE ABSOLUTE, AND #filePath IS WHAT MAKES IT SO. SwiftPM does
// not pin the linker's working directory to the package root, so a relative
// "Vendor/libpokecore.a" resolves against whatever directory xtool happens to
// be in — and on `xtool dev` (as opposed to `xtool dev build`) it does not
// resolve at all. The failure is 24 undefined `poke_*` symbols at the link,
// which reads like the archive is missing, empty, or built for the wrong
// platform. It is none of those: the linker simply never saw it.
//
// POKECORE_LIB still overrides, because CI links the simulator archive from a
// different path. The #filePath default is what makes a bare `xtool dev` work.
let vendorLib = ProcessInfo.processInfo.environment["POKECORE_LIB"]
    ?? URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .appendingPathComponent("Vendor/libpokecore.a")
        .path

// Fail loudly and early if the archive is not where we think it is. Without
// this, a wrong path is indistinguishable from a wrong platform or a broken
// build — all three surface as the same undefined-symbol list.
if !FileManager.default.fileExists(atPath: vendorLib) {
    fatalError("""
        Emulator core archive not found at: \(vendorLib)
        Build it with scripts/build-core.sh, or point POKECORE_LIB at an existing archive.
        """)
}

let package = Package(
    name: "OpenGameAccess",
    platforms: [
        .iOS(.v17),
    ],
    products: [
        .library(name: "OpenGameAccess", targets: ["OpenGameAccess"]),
    ],
    targets: [
        .target(
            name: "CPokeCore",
            path: "Sources/CPokeCore",
            publicHeadersPath: "include",
            cxxSettings: [
                .define("POKE_IOS", to: "1"),
            ],
            linkerSettings: [
                .unsafeFlags([
                    "-Xlinker", "-force_load",
                    "-Xlinker", vendorLib,
                ]),
                .linkedLibrary("c++"),
            ]
        ),
        .target(
            name: "OpenGameAccess",
            dependencies: ["CPokeCore"],
            path: "Sources/OpenGameAccess",
            resources: [.copy("Resources")]
        ),
    ]
)
