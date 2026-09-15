pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "PokemonAccess"
include(":app")
// The emulator core: melonDS-android with the melonDS-lua (NPO-197) patch applied.
// Clone https://github.com/rafaelvcaetano/melonDS-android, replace melonDS-android-lib
// with https://github.com/NPO-197/melonDS-lua (Lua-enabled core), then add this module:
// includeBuild("native/melonDS-android")  // uncomment once the core is vendored
