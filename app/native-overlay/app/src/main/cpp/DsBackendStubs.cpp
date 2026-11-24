// DsBackendStubs.cpp — honest refusals for the backends this Android build
// does not carry, so the shared DS host can link.
//
// WHY THIS FILE EXISTS. The Nintendo DS host is Core/pokecore.cpp — the
// same translation unit iOS drives — and it is a MULTI-CONSOLE host: its
// ROM resolver can route a file to the DS, GBA, NES or SNES backend
// (Core/oga_core.cpp's wrappers), and the shared adapter registry
// (Core/adapters.cpp) names adapters for all of them. Linking pokecore
// into the Android frontend therefore references the gba_*/nes_*/snes_*
// C ABIs even though the DS session is the only caller this stage adds.
//
// Every console pokecore can route to is now really compiled on Android:
//   - GB/GBC/GBA: Core/gba_core.cpp rides the DS glue (overlay section 9)
//     since the Game Boy session moved onto the shared Core, so its stubs
//     are GONE — defining them would duplicate the real symbols in one .so.
//   - PSP: psp_* is really compiled, in the oga-ppsspp-glue objects.
//   - NES/SNES: the overlay's section 11 compiles the real MesenCE glue
//     (Core/mesen_core.cpp and Core/snes_core.cpp, the oga-mesen
//     subproject) into the same frontend library.
//   - Genesis, PlayStation and Nintendo 64: their own subprojects
//     (sections 10, 10b, 10c).
//
// The file stays as the one place a backend pokecore references, but this
// build does not carry, gets its refusal. Today that set is empty.
//
// MEASURED, NOT GUESSED, when a stub is added: inventory the undefined
// symbols of the Core translation units the DS glue compiles (nm on the
// host), and stub every function the missing backend's header declares,
// so the set stays complete if a future Core TU reaches for one more.
//
// SEMANTICS for any stub: create() returns null, and pokecore's load path
// checks for exactly that and refuses the ROM with its own error — so a
// file handed to the wrong session fails cleanly, in words, instead of
// crashing or half-starting.

#include <cstdint>


// (No backend is refused in this build; see the header above.)
