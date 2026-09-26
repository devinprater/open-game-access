// swift-tools-version: 6.0
import Foundation
import PackageDescription

// Open Game Access — iOS.
//
// The emulation core (melonDS + the Lua accessibility layer) is compiled as a
// static archive by scripts/build-core.sh; Sources/CPokeCore is the only thing
// Swift is allowed to see of it.
//
// The archive is linked normally (no -force_load): Swift calls the poke_*
// entry points directly, so ordinary archive semantics pull exactly the
// objects the app references. force_load is not just unnecessary, it is
// unworkable: melonDS, mGBA, and PPSSPP each vendor third-party sources
// (LZMA SDK 19.00 twice, and historically lua and xxhash), and force-loading
// every object turns those identical twins into duplicate-symbol errors.
// Normal linking resolves them first-wins instead. The duplicate-symbol gate
// in scripts/verify-sim-app.sh keeps that honest: a NEW overlap where the
// two copies differ semantically must still be deduped by hand.
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
                    "-Xlinker", vendorLib,
                ]),
                .linkedLibrary("c++"),
                .linkedLibrary("z"),
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
