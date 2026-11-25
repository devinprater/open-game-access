/* ps2_core.cpp — the PlayStation 2 wrapper. See ps2_core.h for the design:
 * dlopen()ed libretro cores (Play! and ARMSX2), an owned headless GL
 * context for the hardware-rendered video, and the ps1_core.h contract.
 *
 * Two deliberate differences from ps1_core.cpp, both forced by the cores:
 *   - the core is a shared library resolved at run time, not a static
 *     link with prefixed symbols (the header explains why);
 *   - video arrives through an OpenGL framebuffer this file owns, not
 *     through the video callback's pointer — the callback only says
 *     "a frame is in the FBO", and the pixels are read back here.
 *
 * A developer knob the header does not mention: the environment variable
 * OGA_PS2_CORE names a core .so exactly, and beats the folder search
 * (an explicit ps2_set_core_path beats both). OGA_PS2_LOG=1 sends the
 * cores' log lines to stderr.
 */

#include "ps2_core.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include <algorithm>

#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>

#include "libretro.h"

#if defined(__has_include)
#  if __has_include(<EGL/egl.h>) && (__has_include(<GL/gl.h>) || __has_include(<GLES3/gl3.h>))
#    define OGA_PS2_HAVE_EGL 1
#  endif
#endif
#ifdef OGA_PS2_HAVE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#if __has_include(<GL/gl.h>)
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#else
/* Android: GLES3's header declares everything the FBO path uses. */
#include <GLES3/gl3.h>
#endif
#endif

/* ------------------------------------------------------------------ the core
 *
 * The libretro entry points, resolved out of the dlopen()ed library. */
struct LibretroApi {
    void *lib = nullptr;
    void (*set_environment)(retro_environment_t) = nullptr;
    void (*set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (*set_audio_sample)(retro_audio_sample_t) = nullptr;
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*set_input_poll)(retro_input_poll_t) = nullptr;
    void (*set_input_state)(retro_input_state_t) = nullptr;
    void (*init)(void) = nullptr;
    void (*deinit)(void) = nullptr;
    unsigned (*api_version)(void) = nullptr;
    void (*get_system_info)(struct retro_system_info *) = nullptr;
    void (*get_system_av_info)(struct retro_system_av_info *) = nullptr;
    bool (*load_game)(const struct retro_game_info *) = nullptr;
    void (*unload_game)(void) = nullptr;
    void (*run)(void) = nullptr;
    void (*reset)(void) = nullptr;
    size_t (*serialize_size)(void) = nullptr;
    bool (*serialize)(void *, size_t) = nullptr;
    bool (*unserialize)(const void *, size_t) = nullptr;
    void *(*get_memory_data)(unsigned) = nullptr;
    size_t (*get_memory_size)(unsigned) = nullptr;
    void (*set_controller_port_device)(unsigned, unsigned) = nullptr;
};

template <typename T>
static bool Sym(LibretroApi &api, T &out, const char *name, bool required)
{
    void *p = dlsym(api.lib, name);
    if (!p && required) return false;
    out = (T) p;
    return true;
}

static bool LoadApi(LibretroApi &api, std::string &err)
{
    bool ok = true;
    ok &= Sym(api, api.set_environment, "retro_set_environment", true);
    ok &= Sym(api, api.set_video_refresh, "retro_set_video_refresh", true);
    ok &= Sym(api, api.set_audio_sample, "retro_set_audio_sample", true);
    ok &= Sym(api, api.set_audio_sample_batch, "retro_set_audio_sample_batch", true);
    ok &= Sym(api, api.set_input_poll, "retro_set_input_poll", true);
    ok &= Sym(api, api.set_input_state, "retro_set_input_state", true);
    ok &= Sym(api, api.init, "retro_init", true);
    ok &= Sym(api, api.deinit, "retro_deinit", true);
    ok &= Sym(api, api.api_version, "retro_api_version", false);
    ok &= Sym(api, api.get_system_info, "retro_get_system_info", true);
    ok &= Sym(api, api.get_system_av_info, "retro_get_system_av_info", true);
    ok &= Sym(api, api.load_game, "retro_load_game", true);
    ok &= Sym(api, api.unload_game, "retro_unload_game", true);
    ok &= Sym(api, api.run, "retro_run", true);
    ok &= Sym(api, api.reset, "retro_reset", false);
    ok &= Sym(api, api.serialize_size, "retro_serialize_size", false);
    ok &= Sym(api, api.serialize, "retro_serialize", false);
    ok &= Sym(api, api.unserialize, "retro_unserialize", false);
    ok &= Sym(api, api.get_memory_data, "retro_get_memory_data", false);
    ok &= Sym(api, api.get_memory_size, "retro_get_memory_size", false);
    ok &= Sym(api, api.set_controller_port_device, "retro_set_controller_port_device", false);
    if (!ok) err = "the library is missing required libretro entry points";
    return ok;
}

/* ------------------------------------------------------------------ wrapper */

struct Ps2Core {
    bool loaded = false;
    bool running = false;
    unsigned long long frames = 0;

    LibretroApi api;
    std::string corePath;       /* the .so chosen ("" until one is) */
    std::string coreName;       /* library_name, from the core itself */
    std::string validExt;       /* the core's own valid_extensions list */
    std::string coreDir;        /* folder to search when no exact path */
    std::string explicitPath;   /* ps2_set_core_path / OGA_PS2_CORE */
    std::string biosDir;        /* also the core's system directory */
    std::string saveDir;        /* the core's save directory (cards) */
    std::string error;
    std::string lastCoreError;  /* the core's own last error-level log line */

    uint16_t padMask = 0;               /* bit i = PS2_BTN_* i is down */
    int16_t axis[2][2] = {{0, 0}, {0, 0}};
    double fps = 0.0;
    int coreRate = 44100;               /* the core's own audio rate, from AV info */
    bool serializeOk = false;           /* probed at load */
    bool hleBios = true;

    /* Memory the core published: SYSTEM_RAM through get_memory_data, plus
     * whatever SET_MEMORY_MAPS described (the scratchpad, an IOP window). */
    const uint8_t *ram = nullptr;
    size_t ramSize = 0;
    const uint8_t *scratch = nullptr;
    const uint8_t *iopRam = nullptr;
    size_t iopRamSize = 0;

    /* Video: the wrapper's RGBA staging buffer, filled from the GL
     * framebuffer (hardware frames) or converted (software frames). */
    std::vector<uint8_t> frameRGBA;
    int fbW = 0, fbH = 0;

    /* Audio from the core at its own rate, resampled to the app's on read.
     * The mutex is load-bearing (the PS1 wrapper learned this): the app
     * drains audio on its render thread while frames run elsewhere. */
    std::mutex audioMutex;
    std::vector<int16_t> audio;
    size_t audioRead = 0;
    double audioFrac = 0.0;
};

namespace {

constexpr size_t kAudioCapFrames = 16384;   /* ~340 ms at 48000 Hz */

Ps2Core *gLive = nullptr;                   /* the singleton guard */
char gCreateError[256] = "";                /* why the last create refused */

void SetError(Ps2Core *c, const char *msg) { if (c) c->error = msg ? msg : "unknown error"; }

bool FileExists(const std::string &path)
{
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool EndsWithNoCase(const std::string &s, const char *suffix)
{
    size_t n = strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char) s[s.size() - n + i]) != tolower((unsigned char) suffix[i]))
            return false;
    return true;
}

std::vector<std::string> ListDir(const std::string &dir)
{
    std::vector<std::string> out;
    DIR *d = opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent *e = readdir(d))
        if (e->d_name[0] != '.') out.push_back(e->d_name);
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

/* A PS2 BIOS is present when a .bin sits where the PCSX2 lineage looks:
 * <biosDir>/pcsx2/bios/, or directly in <biosDir> (be lenient — the
 * staging is ours, but a folder the player filled by hand counts too). */
bool BiosPresent(const std::string &biosDir)
{
    if (biosDir.empty()) return false;
    const std::string spots[2] = { biosDir + "/pcsx2/bios", biosDir };
    for (const std::string &spot : spots)
        for (const std::string &name : ListDir(spot))
            if (EndsWithNoCase(name, ".bin") && FileExists(spot + "/" + name)) return true;
    return false;
}

/* WHICH CORE: see the header. Returns "" when nothing usable is found. */
std::string PickCore(Ps2Core *c)
{
    if (!c->explicitPath.empty())
        return FileExists(c->explicitPath) ? c->explicitPath : "";
    if (c->coreDir.empty()) return "";
    static const char *const kArmsx2[] = {
        "armsx2_libretro_android.so", "libarmsx2_libretro.so", "armsx2_libretro.so", nullptr };
    static const char *const kPlay[] = {
        "play_libretro_android.so", "libplay_libretro.so", "play_libretro.so", nullptr };
    auto search = [&](const char *const *names) -> std::string {
        for (int i = 0; names[i]; i++) {
            std::string p = c->coreDir + "/" + names[i];
            if (FileExists(p)) return p;
        }
        return "";
    };
    if (BiosPresent(c->biosDir)) {
        std::string p = search(kArmsx2);
        if (!p.empty()) return p;
    }
    std::string p = search(kPlay);
    if (!p.empty()) return p;
    p = search(kArmsx2);
    if (!p.empty()) return p;
    /* A core the player supplied under a name of its own. */
    for (const std::string &name : ListDir(c->coreDir))
        if (name.find("_libretro") != std::string::npos && EndsWithNoCase(name, ".so"))
            return c->coreDir + "/" + name;
    return "";
}

/* ---- the disc serial, out of SYSTEM.CNF ----
 *
 * A plain cooked .iso only: sector 16 is the ISO 9660 primary volume
 * descriptor, its root directory record names SYSTEM.CNF;1, and the
 * file's BOOT2 line names the boot ELF (cdrom0:\SLUS_209.98;1). The
 * serial is those letters and digits with the punctuation dropped —
 * the spelling every PS2 reader keys on. Anything else (a CHD, a raw
 * image, a parse failure) yields "", never a guess. */
uint32_t Le32At(const uint8_t *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }

bool ReadAt(FILE *f, long off, uint8_t *buf, size_t n)
{
    return fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}

std::string SerialFromIso(const std::string &path)
{
    if (!EndsWithNoCase(path, ".iso")) return "";
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string out;
    uint8_t pvd[2048];
    if (ReadAt(f, 16 * 2048, pvd, sizeof(pvd)) && pvd[0] == 1 &&
        memcmp(pvd + 1, "CD001", 5) == 0) {
        const uint8_t *root = pvd + 156;             /* the root directory record */
        uint32_t rootLba = Le32At(root + 2);
        uint32_t rootSize = Le32At(root + 10);
        std::vector<uint8_t> dir(rootSize < 65536 ? rootSize : 65536);
        if (ReadAt(f, (long) rootLba * 2048, dir.data(), dir.size())) {
            size_t i = 0;
            while (i + 33 <= dir.size() && dir[i] != 0) {
                size_t recLen = dir[i];
                if (recLen < 34) break;
                uint32_t lba = Le32At(&dir[i + 2]);
                uint32_t size = Le32At(&dir[i + 10]);
                size_t nameLen = dir[i + 32];
                std::string name((const char *) &dir[i + 33],
                                 nameLen < recLen - 33 ? nameLen : recLen - 33);
                if (name.size() >= 10 &&
                    strncasecmp(name.c_str(), "SYSTEM.CNF", 10) == 0 && size > 0 && size <= 65536) {
                    std::vector<uint8_t> cnf(size);
                    if (ReadAt(f, (long) lba * 2048, cnf.data(), size)) {
                        std::string text((const char *) cnf.data(), size);
                        size_t b = text.find("BOOT2");
                        if (b != std::string::npos) {
                            size_t eq = text.find('=', b);
                            size_t semi = eq == std::string::npos ? std::string::npos
                                                                 : text.find(';', eq);
                            if (semi != std::string::npos && semi > eq) {
                                std::string tok = text.substr(eq + 1, semi - eq - 1);
                                for (char ch : tok) {
                                    if (ch == '\\' || ch == ':') { out.clear(); continue; }
                                    if (isalnum((unsigned char) ch)) out += (char) toupper((unsigned char) ch);
                                }
                            }
                        }
                    }
                    break;
                }
                i += recLen;
            }
        }
    }
    fclose(f);
    /* The shape of a PS2 serial: four letters, five digits (SLUS20998). */
    if (out.size() != 9) return "";
    for (int i = 0; i < 4; i++) if (!isalpha((unsigned char) out[i])) return "";
    for (int i = 4; i < 9; i++) if (!isdigit((unsigned char) out[i])) return "";
    return out;
}

/* ---- GL: the wrapper's own headless context (see the header) ---- */
#ifdef OGA_PS2_HAVE_EGL
struct GlState {
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface surf = EGL_NO_SURFACE;       /* a 1x1 pbuffer when NO_SURFACE fails */
    GLuint fbo = 0, color = 0, depth = 0;
    unsigned fboW = 0, fboH = 0;
    unsigned wantW = 0, wantH = 0;          /* a resize asked for by the video callback */
    retro_hw_context_reset_t ctxReset = nullptr;
    retro_hw_context_reset_t ctxDestroy = nullptr;
    bool up = false;
};
GlState gGl;

bool GlMakeFbo(unsigned w, unsigned h)
{
    if (w == 0 || h == 0) { w = 640; h = 448; }
    if (gGl.fbo && gGl.fboW == w && gGl.fboH == h) return true;
    if (gGl.fbo) {
        glDeleteFramebuffers(1, &gGl.fbo);
        glDeleteRenderbuffers(1, &gGl.color);
        glDeleteRenderbuffers(1, &gGl.depth);
    }
    glGenFramebuffers(1, &gGl.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gGl.fbo);
    glGenRenderbuffers(1, &gGl.color);
    glBindRenderbuffer(GL_RENDERBUFFER, gGl.color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, (GLsizei) w, (GLsizei) h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, gGl.color);
    glGenRenderbuffers(1, &gGl.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, gGl.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (GLsizei) w, (GLsizei) h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gGl.depth);
    gGl.fboW = w; gGl.fboH = h;
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

bool GlInit(unsigned contextType, unsigned verMajor, unsigned verMinor)
{
    if (gGl.up) return true;
    bool es = contextType == RETRO_HW_CONTEXT_OPENGLES2 ||
              contextType == RETRO_HW_CONTEXT_OPENGLES3 ||
              contextType == RETRO_HW_CONTEXT_OPENGLES_VERSION;
#if defined(__ANDROID__)
    gGl.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatform =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (getPlatform) gGl.dpy = getPlatform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (gGl.dpy == EGL_NO_DISPLAY) gGl.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
    if (gGl.dpy == EGL_NO_DISPLAY || !eglInitialize(gGl.dpy, nullptr, nullptr)) return false;
    if (!eglBindAPI(es ? EGL_OPENGL_ES_API : EGL_OPENGL_API)) return false;
    const EGLint cfgAttrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                EGL_RENDERABLE_TYPE, es ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT,
                                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                EGL_DEPTH_SIZE, 24, EGL_NONE };
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(gGl.dpy, cfgAttrs, &cfg, 1, &n) || n < 1) return false;
    EGLint ctxAttrs[7];
    int a = 0;
    ctxAttrs[a++] = EGL_CONTEXT_MAJOR_VERSION; ctxAttrs[a++] = (EGLint) (verMajor ? verMajor : 3);
    ctxAttrs[a++] = EGL_CONTEXT_MINOR_VERSION; ctxAttrs[a++] = (EGLint) verMinor;
    if (!es && contextType == RETRO_HW_CONTEXT_OPENGL_CORE) {
        ctxAttrs[a++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
        ctxAttrs[a++] = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
    }
    ctxAttrs[a] = EGL_NONE;
    gGl.ctx = eglCreateContext(gGl.dpy, cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (gGl.ctx == EGL_NO_CONTEXT) return false;
    if (!eglMakeCurrent(gGl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, gGl.ctx)) {
        /* Some drivers refuse a surfaceless current context; a 1x1 pbuffer
         * is the portable fallback (we still render into our own FBO). */
        const EGLint pb[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        gGl.surf = eglCreatePbufferSurface(gGl.dpy, cfg, pb);
        if (gGl.surf == EGL_NO_SURFACE) return false;
        if (!eglMakeCurrent(gGl.dpy, gGl.surf, gGl.surf, gGl.ctx)) return false;
    }
    if (!GlMakeFbo(640, 448)) return false;
    gGl.up = true;
    return true;
}

uintptr_t GlFramebuffer(void) { return (uintptr_t) gGl.fbo; }

retro_proc_address_t GlProcAddress(const char *sym)
{
    retro_proc_address_t p = (retro_proc_address_t) eglGetProcAddress(sym);
    if (!p) p = (retro_proc_address_t) dlsym(RTLD_DEFAULT, sym);
    return p;
}
#endif /* OGA_PS2_HAVE_EGL */

/* ---- libretro callbacks ----
 *
 * libretro has no user pointer, so the callbacks reach the one live
 * wrapper through gLive — the same reason the core is a singleton. */

void RETRO_CALLCONV LogCb(enum retro_log_level level, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    if (level >= RETRO_LOG_ERROR && gLive) gLive->lastCoreError = line;
    if (getenv("OGA_PS2_LOG")) fprintf(stderr, "[ps2core] %s\n", line);
}

bool EnvCb(unsigned cmd, void *data)
{
    Ps2Core *c = gLive;
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback *cb = (struct retro_log_callback *) data;
        cb->log = LogCb;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (!c || c->biosDir.empty()) return false;
        *(const char **) data = c->biosDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!c || c->saveDir.empty()) return false;
        *(const char **) data = c->saveDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
        if (!c || c->saveDir.empty()) return false;
        *(const char **) data = c->saveDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        /* No option is answered: every core keeps its own defaults, which
         * is what a first integration should run. */
        struct retro_variable *v = (struct retro_variable *) data;
        if (v) v->value = nullptr;
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *) data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(const enum retro_pixel_format *) data;
        return f == RETRO_PIXEL_FORMAT_XRGB8888 || f == RETRO_PIXEL_FORMAT_RGB565;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
#ifdef OGA_PS2_HAVE_EGL
        struct retro_hw_render_callback *h = (struct retro_hw_render_callback *) data;
        /* Only the GL family is served. A whitelist (not a blacklist) so
         * this compiles against any libretro.h vintage: a Vulkan/Metal/D3D
         * request lands on the refusal, never on a GL context. */
        if (h->context_type != RETRO_HW_CONTEXT_OPENGL &&
            h->context_type != RETRO_HW_CONTEXT_OPENGL_CORE &&
            h->context_type != RETRO_HW_CONTEXT_OPENGLES2 &&
            h->context_type != RETRO_HW_CONTEXT_OPENGLES3 &&
            h->context_type != RETRO_HW_CONTEXT_OPENGLES_VERSION) {
            if (c) c->lastCoreError = "the core asked for a non-OpenGL renderer, which this build does not serve";
            return false;
        }
        if (!GlInit(h->context_type, h->version_major, h->version_minor)) {
            if (c) c->lastCoreError = "could not create the headless OpenGL context the core requires";
            return false;
        }
        h->get_current_framebuffer = GlFramebuffer;
        h->get_proc_address = GlProcAddress;
        gGl.ctxReset = h->context_reset;
        gGl.ctxDestroy = h->context_destroy;
        if (gGl.ctxReset) gGl.ctxReset();
        return true;
#else
        (void) data;
        if (c) c->lastCoreError = "the core requires hardware rendering, and this build has no GL (PS2 is Android/Linux-only for now)";
        return false;
#endif
    }
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *(unsigned *) data = RETRO_HW_CONTEXT_OPENGL_CORE;
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
        const struct retro_memory_map *m = (const struct retro_memory_map *) data;
        if (getenv("OGA_PS2_LOG"))
            fprintf(stderr, "[ps2core] memmaps called num=%u\n", m ? m->num_descriptors : 0);
        if (!c || !m) return true;
        for (unsigned i = 0; i < m->num_descriptors; i++) {
            const struct retro_memory_descriptor &d = m->descriptors[i];
            if (getenv("OGA_PS2_LOG"))
                fprintf(stderr, "[ps2core] memmap desc start=0x%llx select=0x%llx len=0x%llx offset=0x%llx ptr=%p\n",
                        (unsigned long long) d.start, (unsigned long long) d.select,
                        (unsigned long long) d.len, (unsigned long long) d.offset, (void *) d.ptr);
            if (!d.ptr) continue;
            if (d.start == PS2_SCRATCH_BASE && d.len >= PS2_SCRATCH_SIZE)
                c->scratch = (const uint8_t *) d.ptr + d.offset;
            if (d.len == PS2_IOP_RAM_SIZE && !c->iopRam) {
                c->iopRam = (const uint8_t *) d.ptr + d.offset;
                c->iopRamSize = d.len;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

void VideoCb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    Ps2Core *c = gLive;
    if (!c || width == 0 || height == 0) return;
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
#ifdef OGA_PS2_HAVE_EGL
        if (!gGl.fbo) return;
        /* Read the frame the core just drew, at the FBO's own size; a size
         * change is applied before the NEXT frame (resizing now would
         * discard the frame being reported). */
        unsigned w = gGl.fboW, hgt = gGl.fboH;
        if (width != w || height != hgt) { gGl.wantW = width; gGl.wantH = height; }
        c->frameRGBA.assign((size_t) w * hgt * 4, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, gGl.fbo);
        glReadPixels(0, 0, (GLsizei) w, (GLsizei) hgt, GL_RGBA, GL_UNSIGNED_BYTE, c->frameRGBA.data());
        /* GL's origin is bottom-left; the app's frames are top-down. */
        size_t rowBytes = (size_t) w * 4;
        std::vector<uint8_t> flip(c->frameRGBA.size());
        for (unsigned y = 0; y < hgt; y++)
            memcpy(flip.data() + (size_t) y * rowBytes,
                   c->frameRGBA.data() + (size_t) (hgt - 1 - y) * rowBytes, rowBytes);
        c->frameRGBA.swap(flip);
        for (size_t i = 3; i < c->frameRGBA.size(); i += 4) c->frameRGBA[i] = 0xFF;
        c->fbW = (int) w; c->fbH = (int) hgt;
#endif
        return;
    }
    if (!data) return;
    /* A software frame (no shipping PS2 core sends one, but the contract
     * is cheap to honour): XRGB8888 or RGB565, already top-down. */
    c->frameRGBA.assign((size_t) width * height * 4, 0);
    const uint8_t *src = (const uint8_t *) data;
    bool rgb565 = pitch == (size_t) width * 2;
    for (unsigned y = 0; y < height; y++) {
        const uint8_t *row = src + (size_t) y * pitch;
        uint8_t *dst = c->frameRGBA.data() + (size_t) y * width * 4;
        for (unsigned x = 0; x < width; x++) {
            uint8_t *px = dst + x * 4;
            if (rgb565) {
                uint16_t v = (uint16_t) (row[x * 2] | (row[x * 2 + 1] << 8));
                px[0] = (uint8_t) (((v >> 11) & 0x1F) << 3);
                px[1] = (uint8_t) (((v >> 5) & 0x3F) << 2);
                px[2] = (uint8_t) ((v & 0x1F) << 3);
            } else {
                const uint8_t *p = row + x * 4;   /* XRGB8888 in memory: B,G,R,X */
                px[0] = p[2]; px[1] = p[1]; px[2] = p[0];
            }
            px[3] = 0xFF;
        }
    }
    c->fbW = (int) width; c->fbH = (int) height;
}

size_t AudioBatchCb(const int16_t *data, size_t frames)
{
    Ps2Core *c = gLive;
    if (!c || !data || frames == 0) return frames;
    std::lock_guard<std::mutex> lock(c->audioMutex);
    size_t have = c->audio.size() / 2 - c->audioRead;
    if (have + frames > kAudioCapFrames) {
        size_t drop = have + frames - kAudioCapFrames;
        if (drop > have) drop = have;
        c->audioRead += drop;
    }
    if (c->audioRead > 0) {
        c->audio.erase(c->audio.begin(), c->audio.begin() + (ptrdiff_t) (c->audioRead * 2));
        c->audioRead = 0;
    }
    c->audio.insert(c->audio.end(), data, data + frames * 2);
    return frames;
}

void AudioSampleCb(int16_t left, int16_t right)
{
    int16_t f[2] = { left, right };
    AudioBatchCb(f, 1);
}

void InputPollCb(void) {}

int16_t InputStateCb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    Ps2Core *c = gLive;
    if (!c || port != 0) return 0;
    if (device == RETRO_DEVICE_ANALOG) {
        if (index > RETRO_DEVICE_INDEX_ANALOG_RIGHT || id > RETRO_DEVICE_ID_ANALOG_Y) return 0;
        return c->axis[index][id];
    }
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    /* libretro joypad ids → this core's buttons: Sony's layout on
     * libretro's SNES-shaped pad, as in the PS1 wrapper. */
    int b = -1;
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_B:      b = PS2_BTN_CROSS; break;
    case RETRO_DEVICE_ID_JOYPAD_A:      b = PS2_BTN_CIRCLE; break;
    case RETRO_DEVICE_ID_JOYPAD_X:      b = PS2_BTN_TRIANGLE; break;
    case RETRO_DEVICE_ID_JOYPAD_Y:      b = PS2_BTN_SQUARE; break;
    case RETRO_DEVICE_ID_JOYPAD_L:      b = PS2_BTN_L1; break;
    case RETRO_DEVICE_ID_JOYPAD_R:      b = PS2_BTN_R1; break;
    case RETRO_DEVICE_ID_JOYPAD_L2:     b = PS2_BTN_L2; break;
    case RETRO_DEVICE_ID_JOYPAD_R2:     b = PS2_BTN_R2; break;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: b = PS2_BTN_SELECT; break;
    case RETRO_DEVICE_ID_JOYPAD_START:  b = PS2_BTN_START; break;
    case RETRO_DEVICE_ID_JOYPAD_UP:     b = PS2_BTN_UP; break;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   b = PS2_BTN_DOWN; break;
    case RETRO_DEVICE_ID_JOYPAD_LEFT:   b = PS2_BTN_LEFT; break;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:  b = PS2_BTN_RIGHT; break;
    case RETRO_DEVICE_ID_JOYPAD_L3:     b = PS2_BTN_L3; break;
    case RETRO_DEVICE_ID_JOYPAD_R3:     b = PS2_BTN_R3; break;
    default: return 0;
    }
    return (c->padMask >> b) & 1u;
}

void Unload(Ps2Core *c)
{
    if (!c || !c->loaded) return;
    if (c->api.unload_game) c->api.unload_game();
    c->loaded = false;
    c->running = false;
}

} // namespace

/* ------------------------------------------------------------------ API */

Ps2Core *ps2_create(void)
{
    if (gLive) {
        snprintf(gCreateError, sizeof(gCreateError),
                 "A PlayStation 2 core is already live; destroy it first.");
        return nullptr;
    }
    Ps2Core *c = new (std::nothrow) Ps2Core();
    if (!c) {
        snprintf(gCreateError, sizeof(gCreateError), "Out of memory.");
        return nullptr;
    }
    gCreateError[0] = '\0';
    if (const char *env = getenv("OGA_PS2_CORE")) c->explicitPath = env;
    gLive = c;   /* the claim is made at create, not at first load: a wrapper
                  * that exists but has not loaded yet still owns the core
                  * slot the callbacks will reach through gLive. */
    return c;
}

const char *ps2_create_error(void) { return gCreateError; }

void ps2_destroy(Ps2Core *core)
{
    if (!core) return;
    Unload(core);
    if (gLive == core) {
        if (core->api.deinit) core->api.deinit();
        gLive = nullptr;
    }
    /* Deliberately NO dlclose: these cores run worker threads whose
     * teardown is not guaranteed finished when retro_deinit returns, and
     * unmapping the library under a still-exiting thread segfaults —
     * measured 2026-11-25 as a flaky crash on the second wrapper in one
     * process. The library is a process-lifetime singleton in practice
     * (one wrapper, one core); the next ps2_create's dlopen returns the
     * same image. */
    delete core;
}

void ps2_set_core_path(Ps2Core *core, const char *path)
{
    if (core) core->explicitPath = path ? path : "";
}

void ps2_set_core_dir(Ps2Core *core, const char *dir)
{
    if (core) core->coreDir = dir ? dir : "";
}

void ps2_set_bios_dir(Ps2Core *core, const char *dir)
{
    if (!core) return;
    core->biosDir = dir ? dir : "";
    while (core->biosDir.size() > 1 &&
           (core->biosDir.back() == '/' || core->biosDir.back() == '\\'))
        core->biosDir.pop_back();
}

bool ps2_load_disc(Ps2Core *core, const char *disc_path, const char *save_path,
                   char code_out[16])
{
    if (code_out) code_out[0] = '\0';
    if (!core || !disc_path || !*disc_path) { SetError(core, "No disc path given."); return false; }
    Unload(core);
    core->error.clear();
    core->lastCoreError.clear();
    core->frames = 0;
    core->padMask = 0;
    memset(core->axis, 0, sizeof(core->axis));
    core->fps = 0.0;
    core->coreRate = 44100;
    core->serializeOk = false;
    core->ram = nullptr; core->ramSize = 0;
    core->scratch = nullptr;
    core->iopRam = nullptr; core->iopRamSize = 0;
    core->frameRGBA.clear();
    core->fbW = core->fbH = 0;
    {
        std::lock_guard<std::mutex> lock(core->audioMutex);
        core->audio.clear();
        core->audioRead = 0;
        core->audioFrac = 0.0;
    }

    /* The save folder: the cores keep their memory cards there. */
    core->saveDir = save_path ? save_path : "";
    if (core->saveDir.empty()) {
        const char *tmp = getenv("TMPDIR");
        core->saveDir = std::string(tmp ? tmp : "/tmp") + "/oga-ps2-saves";
    }
    mkdir(core->saveDir.c_str(), 0777);

    FILE *probe = fopen(disc_path, "rb");
    if (!probe) { SetError(core, "Could not open the disc image."); return false; }
    fclose(probe);

    /* Resolve and open the core library (once per wrapper: a second load
     * reuses it, so a disc swap never dlcloses a live core). */
    if (!core->api.lib) {
        core->corePath = PickCore(core);
        if (core->corePath.empty()) {
            SetError(core, !core->explicitPath.empty()
                ? "The named PlayStation 2 core library could not be found."
                : "No PlayStation 2 core library was found in the cores folder.");
            return false;
        }
        core->api.lib = dlopen(core->corePath.c_str(), RTLD_LOCAL | RTLD_NOW);
        if (!core->api.lib) {
            std::string msg = "Could not load the PlayStation 2 core library.";
            if (const char *e = dlerror()) msg += std::string(" (") + e + ")";
            SetError(core, msg.c_str());
            return false;
        }
        std::string err;
        if (!LoadApi(core->api, err)) {
            SetError(core, ("The PlayStation 2 core library is not a libretro core: " + err + ".").c_str());
            return false;
        }
        gLive = core;
        core->api.set_environment(EnvCb);
        core->api.set_video_refresh(VideoCb);
        core->api.set_audio_sample(AudioSampleCb);
        core->api.set_audio_sample_batch(AudioBatchCb);
        core->api.set_input_poll(InputPollCb);
        core->api.set_input_state(InputStateCb);
        core->api.init();
        struct retro_system_info info;
        memset(&info, 0, sizeof(info));
        core->api.get_system_info(&info);
        core->coreName = info.library_name ? info.library_name : "";
        core->validExt = info.valid_extensions ? info.valid_extensions : "";
    }

    /* The extension gate: Play!'s load_game accepts almost anything and
     * only fails later, in silence, so the wrapper enforces the core's
     * OWN declared extension list up front, in words. A core that
     * declares no list is taken at its word and not gated. */
    if (!core->validExt.empty()) {
        std::string ext;
        const char *dot = strrchr(disc_path, '.');
        if (dot && dot[1]) {
            for (const char *p = dot + 1; *p && *p != '/' && *p != '\\'; p++)
                ext += (char) tolower((unsigned char) *p);
        }
        bool accepted = false;
        std::string list = core->validExt;
        for (size_t at = 0;;) {
            size_t bar = list.find('|', at);
            std::string one = list.substr(at, bar == std::string::npos ? bar : bar - at);
            if (one == ext) { accepted = true; break; }
            if (bar == std::string::npos) break;
            at = bar + 1;
        }
        if (!accepted) {
            SetError(core, (core->coreName + " does not accept " +
                            (ext.empty() ? std::string("extensionless") : "." + ext) +
                            " files (it takes: " + core->validExt + ").").c_str());
            return false;
        }
    }

    struct retro_game_info game;
    memset(&game, 0, sizeof(game));
    game.path = disc_path;
    if (!core->api.load_game(&game)) {
        std::string msg = core->coreName + " could not load that file as a PlayStation 2 disc.";
        if (!core->lastCoreError.empty()) msg += " (" + core->lastCoreError + ")";
        SetError(core, msg.c_str());
        return false;
    }
    if (core->api.set_controller_port_device)
        core->api.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    core->loaded = true;
    {
        struct retro_system_av_info av;
        memset(&av, 0, sizeof(av));
        core->api.get_system_av_info(&av);
        core->fps = av.timing.fps > 0.0 ? av.timing.fps : 0.0;
        if (av.timing.sample_rate > 0.0) core->coreRate = (int) (av.timing.sample_rate + 0.5);
    }
    core->serializeOk = core->api.serialize_size && core->api.serialize_size() > 0 &&
                        core->api.serialize && core->api.unserialize;
    /* Play! is HLE by construction; the PCSX2 lineage is HLE only until a
     * BIOS is in its hands. */
    core->hleBios = core->coreName.rfind("Play", 0) == 0 || !BiosPresent(core->biosDir);
    if (core->api.get_memory_data) {
        const uint8_t *p = (const uint8_t *) core->api.get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        size_t n = core->api.get_memory_size ? core->api.get_memory_size(RETRO_MEMORY_SYSTEM_RAM) : 0;
        /* A core that publishes less than the EE's RAM is not believed:
         * adapter reads fail closed rather than reading a short buffer.
         * A core that publishes nothing yet (its VM starts lazily, on a
         * thread of its own) keeps whatever SET_MEMORY_MAPS described;
         * Ps2EnsureRam asks again once frames are running. */
        if (p && n >= PS2_RAM_SIZE) { core->ram = p; core->ramSize = n; }
    }
    if (code_out) {
        std::string serial = SerialFromIso(disc_path);
        size_t n = serial.size() < 15 ? serial.size() : 15;
        memcpy(code_out, serial.c_str(), n);
        code_out[n] = '\0';
    }
    return true;
}

bool ps2_start(Ps2Core *core)
{
    if (!core || !core->loaded) { SetError(core, "No PlayStation 2 disc is loaded."); return false; }
    core->running = true;
    return true;
}

void ps2_stop(Ps2Core *core)
{
    if (!core) return;
    /* The memory cards are the core's own files in the save folder; there
     * is nothing of ours to flush (the PS1 difference, stated in the header). */
    core->running = false;
}

bool ps2_running(Ps2Core *core)
{
    return core && core->running;
}

bool ps2_frame(Ps2Core *core)
{
    if (!core || !core->loaded || !core->running) return false;
#ifdef OGA_PS2_HAVE_EGL
    if (gGl.wantW || gGl.wantH) {
        GlMakeFbo(gGl.wantW, gGl.wantH);
        gGl.wantW = gGl.wantH = 0;
    }
#endif
    core->api.run();
    core->frames++;
    return true;
}

bool ps2_framebuffer(Ps2Core *core, int *width, int *height)
{
    if (!core || core->frameRGBA.empty()) return false;
    if (width) *width = core->fbW;
    if (height) *height = core->fbH;
    return true;
}

const uint8_t *ps2_framebuffer_ptr(Ps2Core *core)
{
    if (!core || core->frameRGBA.empty()) return nullptr;
    return core->frameRGBA.data();
}

void ps2_set_button(Ps2Core *core, int ps2_button, bool down)
{
    if (!core || ps2_button < 0 || ps2_button >= PS2_BTN_COUNT) return;
    uint16_t bit = (uint16_t) (1u << ps2_button);
    if (down) core->padMask |= bit;
    else core->padMask &= (uint16_t) ~bit;
}

void ps2_set_analog(Ps2Core *core, int stick, float x, float y)
{
    if (!core || stick < PS2_STICK_LEFT || stick > PS2_STICK_RIGHT) return;
    auto axis = [](float v) -> int16_t {
        if (!(v == v)) v = 0.0f;                  /* NaN reads as centred */
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        return (int16_t) (v >= 0.0f ? v * 32767.0f : v * 32768.0f);
    };
    core->axis[stick][0] = axis(x);
    core->axis[stick][1] = axis(y);
}

double ps2_frame_rate(Ps2Core *core)
{
    return core && core->loaded ? core->fps : 0.0;
}

int ps2_read_audio(Ps2Core *core, int16_t *out, int max_frames)
{
    if (!core || !out || max_frames <= 0) return 0;
    std::lock_guard<std::mutex> lock(core->audioMutex);
    size_t avail = core->audio.size() / 2 - core->audioRead;
    if (avail < 2) return 0;
    /* Linear resampling core-rate → app rate, the PS1 wrapper's method,
     * with the rate this core actually reported (44100 or 48000). */
    const double step = (double) core->coreRate / PS2_APP_AUDIO_RATE;
    double pos = core->audioFrac;
    const int16_t *src = core->audio.data() + core->audioRead * 2;
    int written = 0;
    while (written < max_frames) {
        size_t i = (size_t) pos;
        if (i + 1 >= avail) break;
        double t = pos - (double) i;
        for (int ch = 0; ch < 2; ch++) {
            double a = src[i * 2 + ch], b = src[(i + 1) * 2 + ch];
            out[written * 2 + ch] = (int16_t) (a + (b - a) * t);
        }
        written++;
        pos += step;
    }
    size_t used = (size_t) pos;
    if (used > avail) used = avail;
    core->audioRead += used;
    core->audioFrac = pos - (double) used;
    return written;
}

bool ps2_save_state(Ps2Core *core, const char *path)
{
    if (!core || !core->loaded || !path) { SetError(core, "No PlayStation 2 game is running."); return false; }
    if (!core->serializeOk) {
        SetError(core, (core->coreName + " does not support save states.").c_str());
        return false;
    }
    size_t n = core->api.serialize_size();
    std::vector<uint8_t> buf(n);
    if (n == 0 || !core->api.serialize(buf.data(), n)) {
        SetError(core, (core->coreName + " could not save the state.").c_str());
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) { SetError(core, "Could not open the state file for writing."); return false; }
    size_t wrote = fwrite(buf.data(), 1, n, f);
    fclose(f);
    if (wrote != n) { SetError(core, "Could not write the whole state file."); return false; }
    return true;
}

bool ps2_load_state(Ps2Core *core, const char *path)
{
    if (!core || !core->loaded || !path) { SetError(core, "No PlayStation 2 game is running."); return false; }
    if (!core->serializeOk) {
        SetError(core, (core->coreName + " does not support save states.").c_str());
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) { SetError(core, "Could not open the state file."); return false; }
    std::vector<uint8_t> buf;
    uint8_t chunk[65536];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) buf.insert(buf.end(), chunk, chunk + got);
    fclose(f);
    if (buf.empty()) { SetError(core, "The state file is empty."); return false; }
    if (!core->api.unserialize(buf.data(), buf.size())) {
        SetError(core, (core->coreName + " refused that state file.").c_str());
        return false;
    }
    return true;
}

unsigned long long ps2_frames_completed(Ps2Core *core)
{
    return core ? core->frames : 0;
}

const char *ps2_last_error(Ps2Core *core)
{
    return core ? core->error.c_str() : "no core";
}

const char *ps2_core_name(Ps2Core *core)
{
    return core ? core->coreName.c_str() : "";
}

bool ps2_using_hle_bios(Ps2Core *core)
{
    return core && core->hleBios;
}

/* The address rules, in one place (see the header): the scratchpad is
 * checked before any masking because the mask would destroy it; only the
 * KUSEG/KSEG0/KSEG1 windows and the 0x2/0x3 uncached aliases are served;
 * the physical result must land inside the 32 MiB of EE RAM. */
/* Some cores start their VM lazily: at load time get_memory_data has
 * nothing to give, and the RAM pointer only exists once frames run.
 * Ask again whenever a read finds no RAM yet. */
static void Ps2EnsureRam(Ps2Core *core)
{
    if (core->ram || !core->api.get_memory_data) return;
    const uint8_t *p = (const uint8_t *) core->api.get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t n = core->api.get_memory_size ? core->api.get_memory_size(RETRO_MEMORY_SYSTEM_RAM) : 0;
    static int tries = 0;
    if (getenv("OGA_PS2_LOG") && tries < 8)
        fprintf(stderr, "[ps2core] ensure-ram try %d: ptr=%p size=%zu\n", tries, (const void *) p, n);
    tries++;
    if (p && n >= PS2_RAM_SIZE) { core->ram = p; core->ramSize = n; }
}

static const uint8_t *Ps2Translate(Ps2Core *core, uint32_t addr, int width, uint32_t *offset)
{
    if (addr >= PS2_SCRATCH_BASE && addr + (uint32_t) width <= PS2_SCRATCH_BASE + PS2_SCRATCH_SIZE) {
        if (!core->scratch) return nullptr;
        *offset = addr - PS2_SCRATCH_BASE;
        return core->scratch;
    }
    bool window = addr < 0x40000000u ||
                  (addr >= 0x80000000u && addr < 0xC0000000u);
    if (!window) return nullptr;
    uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys + (uint32_t) width > PS2_RAM_SIZE) return nullptr;
    if (!core->ram) Ps2EnsureRam(core);
    if (!core->ram) return nullptr;
    *offset = phys;
    return core->ram;
}

bool ps2_read(Ps2Core *core, uint32_t addr, int width, uint32_t *out)
{
    if (!core || !core->loaded || !out) return false;
    if (width != 1 && width != 2 && width != 4) return false;
    uint32_t off = 0;
    const uint8_t *base = Ps2Translate(core, addr, width, &off);
    if (!base) return false;
    uint32_t v = 0;
    for (int i = width - 1; i >= 0; i--) v = (v << 8) | base[off + (uint32_t) i];
    *out = v;
    return true;
}

bool ps2_read_iop(Ps2Core *core, uint32_t offset, int width, uint32_t *out)
{
    if (!core || !core->loaded || !out) return false;
    if (width != 1 && width != 2 && width != 4) return false;
    if (!core->iopRam || offset + (uint32_t) width > core->iopRamSize) return false;
    uint32_t v = 0;
    for (int i = width - 1; i >= 0; i--) v = (v << 8) | core->iopRam[offset + (uint32_t) i];
    *out = v;
    return true;
}
