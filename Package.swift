// swift-tools-version: 6.0
import Foundation
import PackageDescription

// Pokémon Access Mobile — iOS.
//
// The emulation core (melonDS + the Lua accessibility layer) is compiled as a
// static archive by scripts/build-core.sh; Sources/CPokeCore is the only thing
// Swift is allowed to see of it.
//
// The archive is force-loaded: nothing in Swift takes the address of a poke_*
// entry point, so without -force_load the linker drops every object in it that
// no Swift symbol referenced, and the app dies at the first core call.
// The path is resolved through POKECORE_LIB because the linker resolves it
// relative to the process working directory, which SwiftPM does not pin to the
// package root.
let vendorLib = ProcessInfo.processInfo.environment["POKECORE_LIB"]
    ?? "Vendor/libpokecore.a"

let package = Package(
    name: "PokemonAccess",
    platforms: [
        .iOS(.v17),
    ],
    products: [
        .library(name: "PokemonAccess", targets: ["PokemonAccess"]),
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
            name: "PokemonAccess",
            dependencies: ["CPokeCore"],
            path: "Sources/PokemonAccess",
            resources: [.copy("Resources")]
        ),
    ]
)
