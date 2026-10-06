/* Empty translation unit: this target is header-only and links the prebuilt
   emulator archive through linkerSettings. Xcode (unlike SwiftPM on Linux)
   refuses to build a target that produces no object, failing with
   "Build input file cannot be found: CPokeCore.o", so this file exists only to
   give the target something to compile. */
