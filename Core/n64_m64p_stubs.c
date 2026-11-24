/* n64_m64p_stubs.c — the GLideN64 entry points Mupen64Plus-Next names but
 * this app never calls.
 *
 * WHY THIS FILE EXISTS. The app builds Mupen64Plus-Next's SOFTWARE path only:
 * the angrylion RDP for video and the cxd4 LLE RSP it needs (see the M64P
 * block in scripts/core-sources.sh). Upstream's default renderer is GLideN64,
 * an OpenGL plugin of ~170 C++ files, and the core still REFERENCES it:
 *   - plugin.c builds a gfx_gln64 function table (DEFINE_GFX(gln64)) next to
 *     angrylion's, so all eighteen gln64* entry points must exist to link;
 *   - libretro.c names glsm_ctl, gln64_thr_gl_invoke_command_loop and the
 *     context hooks, all behind `current_rdp_type == RDP_PLUGIN_GLIDEN64`;
 *   - main.c calls Config_LoadConfig (GLideN64's config loader) at start-up;
 *   - GLideN64's N64.cpp is what normally DEFINES the RSP's DMEM/IMEM
 *     pointers, which the cxd4 RSP reads (it assigns them itself on init).
 * Core/n64_core.cpp always answers the core's rdp-plugin option with
 * "angrylion", so none of the GLideN64 paths below is reachable; each stub
 * does the inert thing (no frame, no context, "not available").
 *
 * This TU is compiled with the core's own flags and tagged m64p_ like every
 * core object, so scripts/m64p-isolate.sh renames these definitions together
 * with the core's references to them. */
#include <stdbool.h>
#include <stddef.h>

#include "m64p_types.h"
#include "m64p_plugin.h"

/* cxd4 (mupen64plus-rsp-cxd4/su.h) declares `extern pu8 DMEM, IMEM` and points
 * them at the SP memory itself when the RSP initialises. */
unsigned char *DMEM = NULL;
unsigned char *IMEM = NULL;

/* custom/mupen64plus-next_common.h: GLideN64's threaded renderer handshake. */
bool threaded_gl_safe_shutdown = true;

/* libretro-common glsm (the shim header Core/n64/glsm/glsmsym.h declares it):
 * there is no GL state machine in this build, so every request fails. */
bool glsm_ctl(int state, void *data) { (void) state; (void) data; return false; }

/* main.c: GLideN64 reads its settings from the core-option store here. */
void Config_LoadConfig(void) {}

/* libretro.c: GLideN64's threaded command loop and context hooks. */
void gln64_thr_gl_invoke_command_loop(void) {}
void gln64DestroyGfxContext(void) {}
void gln64ReinitGfxContext(void) {}

/* plugin.c's DEFINE_GFX(gln64) table, with the prototypes it declares. */
m64p_error gln64PluginGetVersion(m64p_plugin_type *type, int *version, int *api, const char **name, int *caps)
{
    if (type) *type = M64PLUGIN_GFX;
    if (version) *version = 0;
    if (api) *api = 0;
    if (name) *name = "none";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}
void gln64ChangeWindow(void) {}
int  gln64InitiateGFX(GFX_INFO info) { (void) info; return 0; }
void gln64MoveScreen(int x, int y) { (void) x; (void) y; }
void gln64ProcessDList(void) {}
void gln64ProcessRDPList(void) {}
void gln64RomClosed(void) {}
int  gln64RomOpen(void) { return 0; }
void gln64ShowCFB(void) {}
void gln64UpdateScreen(void) {}
void gln64ViStatusChanged(void) {}
void gln64ViWidthChanged(void) {}
void gln64ReadScreen2(void *dest, int *width, int *height, int front)
{
    (void) dest; (void) front;
    if (width) *width = 0;
    if (height) *height = 0;
}
void gln64SetRenderingCallback(void (*callback)(int)) { (void) callback; }
void gln64ResizeVideoOutput(int width, int height) { (void) width; (void) height; }
void gln64FBRead(unsigned int addr) { (void) addr; }
void gln64FBWrite(unsigned int addr, unsigned int size) { (void) addr; (void) size; }
void gln64FBGetFrameBufferInfo(void *p) { (void) p; }

/* ---- minizip, for the Project64 zip savestate import/export ----
 * savestates.c can read and write PJ64 ".zip" states. Nothing on the libretro
 * path reaches those functions (states go through retro_serialize, in memory),
 * but the object still references them. Rather than build minizip and link
 * zlib into the app for dead code, they fail cleanly here: open returns NULL,
 * every other call returns an error. Prototypes come from minizip's own
 * headers, so a signature change upstream breaks the build, not the run. */
#include <unzip.h>
#include <zip.h>

unzFile unzOpen(const char *path) { (void) path; return NULL; }
int unzClose(unzFile file) { (void) file; return UNZ_PARAMERROR; }
int unzGoToFirstFile(unzFile file) { (void) file; return UNZ_PARAMERROR; }
int unzGetCurrentFileInfo(unzFile file, unz_file_info *info, char *name, uLong nameSize,
                          void *extra, uLong extraSize, char *comment, uLong commentSize)
{
    (void) file; (void) info; (void) name; (void) nameSize;
    (void) extra; (void) extraSize; (void) comment; (void) commentSize;
    return UNZ_PARAMERROR;
}
int unzOpenCurrentFile(unzFile file) { (void) file; return UNZ_PARAMERROR; }
int unzReadCurrentFile(unzFile file, voidp buf, unsigned len) { (void) file; (void) buf; (void) len; return UNZ_PARAMERROR; }
zipFile zipOpen(const char *path, int append) { (void) path; (void) append; return NULL; }
int zipClose(zipFile file, const char *comment) { (void) file; (void) comment; return ZIP_PARAMERROR; }
int zipCloseFileInZip(zipFile file) { (void) file; return ZIP_PARAMERROR; }
int zipOpenNewFileInZip(zipFile file, const char *name, const zip_fileinfo *info,
                        const void *extraLocal, uInt extraLocalSize,
                        const void *extraGlobal, uInt extraGlobalSize,
                        const char *comment, int method, int level)
{
    (void) file; (void) name; (void) info; (void) extraLocal; (void) extraLocalSize;
    (void) extraGlobal; (void) extraGlobalSize; (void) comment; (void) method; (void) level;
    return ZIP_PARAMERROR;
}
int zipWriteInFileInZip(zipFile file, const void *buf, unsigned len) { (void) file; (void) buf; (void) len; return ZIP_PARAMERROR; }
