/* m64p_oga.h — force-included into every Mupen64Plus-Next translation unit
 * this app builds (scripts/core-sources.sh, m64p_inc).
 *
 * Upstream's Makefile passes GIT_VERSION on the command line as a quoted
 * string literal (-DGIT_VERSION="\" abc123\""). The OGA build scripts hand
 * their flag strings through `xargs bash -c`, which does not keep that
 * quoting intact, so the one literal libretro.c needs is supplied here. It is
 * only ever pasted into the library_version string the core reports. */
#ifndef OGA_M64P_OGA_H
#define OGA_M64P_OGA_H
#ifndef GIT_VERSION
#define GIT_VERSION " oga"
#endif
#endif
