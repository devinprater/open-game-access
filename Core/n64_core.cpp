/* n64_core.cpp — the Nintendo 64 backend: Mupen64Plus-Next as a static
 * libretro core, behind the n64_* ABI in n64_core.h.
 *
 * HOW THE CORE IS REACHED. Mupen64Plus-Next is a libretro core: its whole API
 * is retro_init/retro_load_game/retro_run plus six callbacks it calls back
 * into. Those callbacks are this file's job (environment, video, audio, input).
 * Every symbol the core defines is renamed oga_m64p_* after compilation
 * (scripts/m64p-isolate.sh), so the entry points are declared here under their
 * renamed names; libretro.h is included for its types only.
 *
 * THE CONFIGURATION IS FIXED, NOT ASKED. The core asks its frontend for ~90
 * options. This frontend answers each with the core's OWN default (collected
 * from RETRO_ENVIRONMENT_SET_VARIABLES, the core-options template's v0 path),
 * except for the three that choose the emulation path, which are pinned:
 *   mupen64plus-rdp-plugin = angrylion            software video, no GL
 *   mupen64plus-rsp-plugin = cxd4                 the LLE RSP angrylion needs
 *   mupen64plus-cpucore    = cached_interpreter   no JIT (iOS cannot)
 * ⛔ THE RDP ANSWER IS LOAD-BEARING. If the option is unanswered the core
 * defaults to GLideN64 — which this build stubs out (Core/n64_m64p_stubs.c) —
 * and retro_load_game then fails asking for an OpenGL context.
 *
 * THREADS. Mupen64Plus-Next runs the emulated machine on a libco cothread
 * that retro_run switches into and out of once per frame: it all happens on
 * the caller's thread, synchronously, exactly like genesis_frame. angrylion
 * renders with its own worker threads inside that call.
 */
#include "n64_core.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

#include <sys/stat.h>

#include <libretro.h>

/* ---- the core's entry points, under the names m64p-isolate.sh gives them ---- */
extern "C" {
void oga_m64p_retro_set_environment(retro_environment_t);
void oga_m64p_retro_set_video_refresh(retro_video_refresh_t);
void oga_m64p_retro_set_audio_sample(retro_audio_sample_t);
void oga_m64p_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void oga_m64p_retro_set_input_poll(retro_input_poll_t);
void oga_m64p_retro_set_input_state(retro_input_state_t);
void oga_m64p_retro_init(void);
void oga_m64p_retro_deinit(void);
void oga_m64p_retro_get_system_info(struct retro_system_info *info);
void oga_m64p_retro_get_system_av_info(struct retro_system_av_info *info);
bool oga_m64p_retro_load_game(const struct retro_game_info *game);
void oga_m64p_retro_unload_game(void);
void oga_m64p_retro_run(void);
void oga_m64p_retro_reset(void);
void *oga_m64p_retro_get_memory_data(unsigned id);
size_t oga_m64p_retro_get_memory_size(unsigned id);
size_t oga_m64p_retro_serialize_size(void);
bool oga_m64p_retro_serialize(void *data, size_t size);
bool oga_m64p_retro_unserialize(const void *data, size_t size);
// libretro.c's own record of the last state load (the core's LOADCOMPLETE
// result). retro_unserialize itself returns true whatever happened.
extern int oga_m64p_retro_savestate_result;
}

struct N64Core {
    bool inited = false;        /* retro_init has run (it needs the system directory) */
    bool loaded = false;
    bool running = false;
    unsigned long long frames = 0;
    std::string error;

    std::string systemDir;      /* where the core writes Mupen64plus/mupen64plus.ini */
    std::string savePath;       /* .srm (the core's saved_memory block); empty = none */
    std::vector<uint8_t> saveShadow;
    std::vector<uint8_t> romData;   /* retro_load_game copies it, but keep it for the call */

    /* Input: N64_BTN_* bits and the stick (-0x7FFF..0x7FFF, libretro's range). */
    uint32_t buttons = 0;
    int16_t stickX = 0, stickY = 0;

    /* Video, RGBA8888. */
    std::vector<uint8_t> frameRGBA;
    int fbW = 0, fbH = 0;

    /* Audio: resampled to kOutRate as it arrives, then drained. */
    double srcRate = 44100.0;
    double resamplePos = 0.0;       /* fractional source position of the next output sample */
    int16_t lastL = 0, lastR = 0;   /* previous source frame, for interpolation across batches */
    std::vector<int16_t> audioOut;  /* interleaved stereo at kOutRate */
    size_t audioRead = 0;

    /* Core options: the core's own defaults, then our pins. */
    std::map<std::string, std::string> options;
};

static const double kOutRate = 32768.0;
static N64Core *gLive = nullptr;
static std::string gVersion;

namespace {

void SetError(N64Core *c, const char *msg) { if (c) c->error = msg ? msg : "unknown error"; }

/* ---- environment ---- */

void CoreLog(enum retro_log_level level, const char *fmt, ...)
{
    /* The core is chatty at DEBUG/INFO; keep warnings and errors, on stderr, so
     * the host proof and device logs show why a load failed. */
    if (level < RETRO_LOG_WARN) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[n64] ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void RecordDefaults(N64Core *c, const struct retro_variable *vars)
{
    /* "Description; default|other|other" — the template puts the default
     * first (libretro_core_options.h, the SET_VARIABLES path). The strings are
     * freed by the core straight after this call, so they are copied here. */
    for (const struct retro_variable *v = vars; v && v->key; v++) {
        if (!v->value) continue;
        const char *semi = strstr(v->value, "; ");
        if (!semi) continue;
        const char *start = semi + 2;
        const char *bar = strchr(start, '|');
        std::string def = bar ? std::string(start, (size_t) (bar - start)) : std::string(start);
        c->options[v->key] = def;
    }
}

void PinOptions(N64Core *c)
{
    c->options["mupen64plus-rdp-plugin"] = "angrylion";
    c->options["mupen64plus-rsp-plugin"] = "cxd4";
    c->options["mupen64plus-cpucore"] = "cached_interpreter";
}

bool Environment(unsigned cmd, void *data)
{
    N64Core *c = gLive;
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        /* Unanswered = version 0: the template then hands us the plain
         * SET_VARIABLES list, which is all this frontend reads. */
        return false;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        if (c) { RecordDefaults(c, (const struct retro_variable *) data); PinOptions(c); }
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = (struct retro_variable *) data;
        if (!c || !v || !v->key) return false;
        auto it = c->options.find(v->key);
        if (it == c->options.end()) { v->value = nullptr; return false; }
        v->value = it->second.c_str();
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (data) *(bool *) data = false;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!c || !data) return false;
        *(const char **) data = c->systemDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        /* The core asks for XRGB8888, which is what the converter below reads. */
        return data && *(const enum retro_pixel_format *) data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (!data) return false;
        ((struct retro_log_callback *) data)->log = CoreLog;
        return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        /* No GPU context in this frontend, ever (see the header note). */
        return false;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return false;   /* the core then polls each button, which InputState answers */
    default:
        return false;
    }
}

/* ---- video ---- */

void VideoRefresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
    N64Core *c = gLive;
    if (!c || !data || width == 0 || height == 0) return;   /* NULL = duplicate frame */
    c->frameRGBA.resize((size_t) width * height * 4);
    for (unsigned y = 0; y < height; y++) {
        const uint32_t *src = (const uint32_t *) ((const uint8_t *) data + y * pitch);
        uint8_t *dst = c->frameRGBA.data() + (size_t) y * width * 4;
        for (unsigned x = 0; x < width; x++) {
            uint32_t p = src[x];    /* 0x00RRGGBB */
            dst[x * 4 + 0] = (uint8_t) ((p >> 16) & 0xFF);
            dst[x * 4 + 1] = (uint8_t) ((p >> 8) & 0xFF);
            dst[x * 4 + 2] = (uint8_t) (p & 0xFF);
            dst[x * 4 + 3] = 0xFF;
        }
    }
    c->fbW = (int) width;
    c->fbH = (int) height;
}

/* ---- audio: linear resampling from the core's rate to the app's ---- */

size_t AudioBatch(const int16_t *data, size_t frames)
{
    N64Core *c = gLive;
    if (!c || !data || frames == 0) return frames;
    /* Bound the backlog: if nobody drains (a paused UI), keep the newest
     * second rather than growing without limit. */
    const size_t kMaxFrames = (size_t) kOutRate;
    if (c->audioRead > 0 && c->audioRead * 2 >= c->audioOut.size()) {
        c->audioOut.erase(c->audioOut.begin(), c->audioOut.begin() + (long) c->audioRead);
        c->audioRead = 0;
    }
    const double step = c->srcRate / kOutRate;
    /* Source frame index -1 is the last frame of the previous batch. */
    while (c->resamplePos < (double) frames - 1.0 + 1e-9) {
        double pos = c->resamplePos;
        long i = (long) std::floor(pos);
        double t = pos - (double) i;
        int16_t l0, r0, l1, r1;
        if (i < 0) { l0 = c->lastL; r0 = c->lastR; }
        else { l0 = data[i * 2]; r0 = data[i * 2 + 1]; }
        l1 = data[(i + 1) * 2];
        r1 = data[(i + 1) * 2 + 1];
        c->audioOut.push_back((int16_t) std::lround(l0 + (l1 - l0) * t));
        c->audioOut.push_back((int16_t) std::lround(r0 + (r1 - r0) * t));
        c->resamplePos += step;
    }
    c->resamplePos -= (double) frames;
    c->lastL = data[(frames - 1) * 2];
    c->lastR = data[(frames - 1) * 2 + 1];
    size_t pending = (c->audioOut.size() / 2) - c->audioRead / 2;
    if (pending > kMaxFrames) {
        size_t drop = (pending - kMaxFrames) * 2;
        c->audioRead += drop;
    }
    return frames;
}

void AudioSample(int16_t left, int16_t right)
{
    int16_t f[2] = { left, right };
    AudioBatch(f, 1);
}

/* ---- input ---- */

void InputPoll(void) {}

int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
    N64Core *c = gLive;
    if (!c || port != 0) return 0;
    /* The core's default mapping (custom/mupen64plus-core/plugin/
     * emulate_game_controller_via_libretro.c, inputGetKeys_default, with
     * alternate_mapping off): N64 A <- JOYPAD_B, B <- JOYPAD_Y, Z <- L2,
     * L <- L, R <- R, Start, the D-pad; the stick on ANALOG_LEFT; the C
     * buttons on ANALOG_RIGHT (negative X = C-left, negative Y = C-up). */
    auto down = [c](int b) { return (c->buttons >> b) & 1u; };
    if (device == RETRO_DEVICE_JOYPAD) {
        switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_B:      return (int16_t) down(N64_BTN_A);
        case RETRO_DEVICE_ID_JOYPAD_Y:      return (int16_t) down(N64_BTN_B);
        case RETRO_DEVICE_ID_JOYPAD_L2:     return (int16_t) down(N64_BTN_Z);
        case RETRO_DEVICE_ID_JOYPAD_START:  return (int16_t) down(N64_BTN_START);
        case RETRO_DEVICE_ID_JOYPAD_UP:     return (int16_t) down(N64_BTN_UP);
        case RETRO_DEVICE_ID_JOYPAD_DOWN:   return (int16_t) down(N64_BTN_DOWN);
        case RETRO_DEVICE_ID_JOYPAD_LEFT:   return (int16_t) down(N64_BTN_LEFT);
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return (int16_t) down(N64_BTN_RIGHT);
        case RETRO_DEVICE_ID_JOYPAD_L:      return (int16_t) down(N64_BTN_L);
        case RETRO_DEVICE_ID_JOYPAD_R:      return (int16_t) down(N64_BTN_R);
        default: return 0;
        }
    }
    if (device == RETRO_DEVICE_ANALOG) {
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) return c->stickX;
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return c->stickY;
        } else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
            if (id == RETRO_DEVICE_ID_ANALOG_X) {
                if (down(N64_BTN_C_LEFT)) return -0x7FFF;
                if (down(N64_BTN_C_RIGHT)) return 0x7FFF;
            }
            if (id == RETRO_DEVICE_ID_ANALOG_Y) {
                if (down(N64_BTN_C_UP)) return -0x7FFF;
                if (down(N64_BTN_C_DOWN)) return 0x7FFF;
            }
        }
    }
    return 0;
}

/* ---- files ---- */

bool ReadFile(const char *path, std::vector<uint8_t> &out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

bool WriteFile(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t w = fwrite(data, 1, size, f);
    fclose(f);
    return w == size;
}

std::string DirOf(const std::string &p)
{
    size_t s = p.find_last_of("/\\");
    if (s == std::string::npos) return ".";
    if (s == 0) return "/";
    return p.substr(0, s);
}

/* The cartridge's 64-byte header in big-endian (z64) order, whatever byte
 * order the dump is in: z64 starts 80 37 12 40, v64 37 80 40 12 (16-bit
 * swapped), n64 40 12 37 80 (32-bit little-endian). */
bool HeaderBigEndian(const std::vector<uint8_t> &rom, uint8_t out[64])
{
    if (rom.size() < 64) return false;
    const uint8_t *p = rom.data();
    if (p[0] == 0x80 && p[1] == 0x37) { memcpy(out, p, 64); return true; }
    if (p[0] == 0x37 && p[1] == 0x80) {
        for (int i = 0; i < 64; i += 2) { out[i] = p[i + 1]; out[i + 1] = p[i]; }
        return true;
    }
    if (p[0] == 0x40 && p[1] == 0x12) {
        for (int i = 0; i < 64; i += 4) {
            out[i] = p[i + 3]; out[i + 1] = p[i + 2]; out[i + 2] = p[i + 1]; out[i + 3] = p[i];
        }
        return true;
    }
    return false;
}

void FlushSave(N64Core *c)
{
    if (!c || !c->loaded || c->savePath.empty()) return;
    void *mem = oga_m64p_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = oga_m64p_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!mem || size == 0) return;
    if (c->saveShadow.size() == size && memcmp(c->saveShadow.data(), mem, size) == 0) return;
    if (WriteFile(c->savePath.c_str(), mem, size))
        c->saveShadow.assign((const uint8_t *) mem, (const uint8_t *) mem + size);
}

void InstallCallbacks()
{
    oga_m64p_retro_set_environment(Environment);
    oga_m64p_retro_set_video_refresh(VideoRefresh);
    oga_m64p_retro_set_audio_sample(AudioSample);
    oga_m64p_retro_set_audio_sample_batch(AudioBatch);
    oga_m64p_retro_set_input_poll(InputPoll);
    oga_m64p_retro_set_input_state(InputState);
}

void Unload(N64Core *c)
{
    if (!c) return;
    if (c->loaded) {
        FlushSave(c);
        oga_m64p_retro_unload_game();
        c->loaded = false;
    }
    c->running = false;
}

} // namespace

/* ================================================================== the ABI */

N64Core *n64_create(void)
{
    if (gLive) return nullptr;      /* one machine: the core is a singleton */
    N64Core *c = new (std::nothrow) N64Core();
    if (!c) return nullptr;
    gLive = c;
    return c;
}

void n64_destroy(N64Core *core)
{
    if (!core) return;
    Unload(core);
    if (core->inited) {
        /* Lets the core's cothread finish and shuts the core down, so a later
         * n64_create + load starts from a clean machine. */
        oga_m64p_retro_deinit();
        core->inited = false;
    }
    if (gLive == core) gLive = nullptr;
    delete core;
}

bool n64_load_rom(N64Core *core, const char *rom_path, const char *save_path, char code_out[16])
{
    if (!core || !rom_path) { SetError(core, "No ROM path given."); return false; }
    if (core->loaded) {
        /* One game per core lifetime: the core's own unload/reload cycle is not
         * something this frontend relies on. pokecore tears the backend down
         * between games. */
        SetError(core, "An N64 game is already loaded in this core.");
        return false;
    }
    core->error.clear();
    core->frames = 0;
    core->buttons = 0;
    core->stickX = core->stickY = 0;
    core->frameRGBA.clear();
    core->fbW = core->fbH = 0;
    core->audioOut.clear();
    core->audioRead = 0;
    core->resamplePos = 0.0;
    core->lastL = core->lastR = 0;

    if (!ReadFile(rom_path, core->romData) || core->romData.empty()) {
        SetError(core, "Could not read that N64 ROM file.");
        return false;
    }
    /* ⛔ ROUTING IS BY EXTENSION, IDENTITY IS BY CONTENT. A file whose first
     * word is none of the three N64 byte orders is not a cartridge dump. */
    uint8_t header[64];
    if (core->romData.size() < 0x1000 || !HeaderBigEndian(core->romData, header)) {
        SetError(core, "That file did not identify as a Nintendo 64 ROM.");
        return false;
    }

    core->savePath = save_path ? save_path : "";
    std::string sys = !core->savePath.empty() ? DirOf(core->savePath) : DirOf(rom_path);
    core->systemDir = sys;
    mkdir(core->systemDir.c_str(), 0755);   /* usually exists; harmless if so */

    if (!core->inited) {
        InstallCallbacks();     /* set_environment delivers the option defaults */
        oga_m64p_retro_init();
        core->inited = true;
        struct retro_system_info info;
        memset(&info, 0, sizeof(info));
        oga_m64p_retro_get_system_info(&info);
        if (info.library_version) gVersion = std::string(info.library_name ? info.library_name : "") + " " + info.library_version;
    }

    struct retro_game_info game;
    memset(&game, 0, sizeof(game));
    game.path = rom_path;
    game.data = core->romData.data();
    game.size = core->romData.size();
    if (!oga_m64p_retro_load_game(&game)) {
        SetError(core, "Mupen64Plus-Next could not load that ROM.");
        return false;
    }
    core->loaded = true;

    struct retro_system_av_info av;
    memset(&av, 0, sizeof(av));
    oga_m64p_retro_get_system_av_info(&av);
    if (av.timing.sample_rate > 1000.0) core->srcRate = av.timing.sample_rate;

    /* Battery saves: the libretro convention is that the frontend fills
     * SAVE_RAM after load_game and before the first frame. */
    core->saveShadow.clear();
    if (!core->savePath.empty()) {
        void *mem = oga_m64p_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
        size_t size = oga_m64p_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
        std::vector<uint8_t> file;
        if (mem && size && ReadFile(core->savePath.c_str(), file) && file.size() == size)
            memcpy(mem, file.data(), size);
        if (mem && size) core->saveShadow.assign((const uint8_t *) mem, (const uint8_t *) mem + size);
    }

    if (code_out) {
        /* The cartridge ID at 0x3B: media type, two-letter game ID, region. */
        char id[5];
        for (int i = 0; i < 4; i++) {
            uint8_t ch = header[0x3B + i];
            id[i] = (ch >= 0x20 && ch < 0x7F) ? (char) ch : '?';
        }
        id[4] = '\0';
        snprintf(code_out, 16, "n64:%s", id);
    }
    return true;
}

bool n64_start(N64Core *core)
{
    if (!core || !core->loaded) { SetError(core, "No N64 ROM is loaded."); return false; }
    core->running = true;
    return true;
}

void n64_stop(N64Core *core)
{
    if (!core) return;
    FlushSave(core);
    core->running = false;
}

bool n64_running(N64Core *core) { return core && core->running; }

bool n64_frame(N64Core *core)
{
    if (!core || !core->loaded || !core->running || gLive != core) return false;
    oga_m64p_retro_run();
    core->frames++;
    return true;
}

bool n64_framebuffer(N64Core *core, int *width, int *height)
{
    if (!core || core->frameRGBA.empty()) return false;
    if (width) *width = core->fbW;
    if (height) *height = core->fbH;
    return true;
}

const uint8_t *n64_framebuffer_ptr(N64Core *core)
{
    if (!core || core->frameRGBA.empty()) return nullptr;
    return core->frameRGBA.data();
}

void n64_set_button(N64Core *core, int n64_button, bool down)
{
    if (!core || n64_button < 0 || n64_button >= N64_BTN_COUNT) return;
    if (down) core->buttons |= (1u << n64_button);
    else core->buttons &= ~(1u << n64_button);
}

void n64_set_analog(N64Core *core, float x, float y)
{
    if (!core) return;
    auto conv = [](float v) -> int16_t {
        if (!(v == v)) return 0;    /* NaN */
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        return (int16_t) std::lround(v * 32767.0f);
    };
    /* libretro's analog Y is +down as well, so both axes pass straight through. */
    core->stickX = conv(x);
    core->stickY = conv(y);
}

int n64_read_audio(N64Core *core, int16_t *out, int max_frames)
{
    if (!core || !out || max_frames <= 0) return 0;
    size_t avail = (core->audioOut.size() - core->audioRead) / 2;
    size_t n = avail < (size_t) max_frames ? avail : (size_t) max_frames;
    if (n == 0) return 0;
    memcpy(out, core->audioOut.data() + core->audioRead, n * 2 * sizeof(int16_t));
    core->audioRead += n * 2;
    if (core->audioRead == core->audioOut.size()) { core->audioOut.clear(); core->audioRead = 0; }
    return (int) n;
}

bool n64_save_state(N64Core *core, const char *path)
{
    if (!core || !core->loaded || !path) { SetError(core, "No N64 game is running."); return false; }
    FlushSave(core);    /* a state and its .srm must not disagree about which is newer */
    size_t size = oga_m64p_retro_serialize_size();
    if (size == 0) { SetError(core, "Mupen64Plus-Next reported an empty state."); return false; }
    std::vector<uint8_t> buf(size);
    if (!oga_m64p_retro_serialize(buf.data(), size)) {
        SetError(core, "Mupen64Plus-Next could not serialize the state.");
        return false;
    }
    if (!WriteFile(path, buf.data(), size)) { SetError(core, "Could not write the state file."); return false; }
    return true;
}

bool n64_load_state(N64Core *core, const char *path)
{
    if (!core || !core->loaded || !path) { SetError(core, "No N64 game is running."); return false; }
    std::vector<uint8_t> buf;
    if (!ReadFile(path, buf) || buf.empty()) { SetError(core, "Could not read the state file."); return false; }
    // ⛔ Check the file HERE, before the core sees it. At the pinned revision
    // the libretro build's savestates_load_m64p returns on a bad magic number
    // WITHOUT unlocking savestates_lock, so the next state job deadlocks the
    // emulation thread for good (the host proof hung exactly that way on a junk
    // file). It also reads the buffer with no length check, and
    // retro_unserialize reports success whatever happened. So: the exact size
    // the core writes, and the core's own magic number, or no call at all.
    // (Version and ROM-MD5 mismatches are refused by the core and DO unlock.)
    static const char kMagic[8] = {'M', '6', '4', '+', 'S', 'A', 'V', 'E'};
    if (buf.size() != oga_m64p_retro_serialize_size() || memcmp(buf.data(), kMagic, sizeof kMagic) != 0) {
        SetError(core, "That file is not a Nintendo 64 state saved by this app.");
        return false;
    }
    if (!oga_m64p_retro_unserialize(buf.data(), buf.size()) || !oga_m64p_retro_savestate_result) {
        SetError(core, "Mupen64Plus-Next refused that state file.");
        return false;
    }
    return true;
}

unsigned long long n64_frames_completed(N64Core *core) { return core ? core->frames : 0; }

const char *n64_last_error(N64Core *core) { return core ? core->error.c_str() : "no core"; }

bool n64_read(N64Core *core, uint32_t addr, int width, uint32_t *out)
{
    if (!core || !core->loaded || !out) return false;
    if (width != 1 && width != 2 && width != 4) return false;
    /* KSEG0/KSEG1 are unmapped windows onto physical memory; strip them. */
    uint32_t phys;
    if (addr >= 0x80000000u && addr < 0xC0000000u) phys = addr & 0x1FFFFFFFu;
    else phys = addr;
    const uint8_t *ram = (const uint8_t *) oga_m64p_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t size = oga_m64p_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    if (!ram || size == 0) return false;
    if ((uint64_t) phys + (uint64_t) width > (uint64_t) size) return false;
    /* ⛔ RDRAM IS HOST-ORDER 32-BIT WORDS. The core stores each big-endian
     * word as a native uint32_t, so on a little-endian host the byte the CPU
     * sees at A lives at A ^ 3 (measured: the host proof's ROM stores the word
     * 0x4F474136 at 0x400 and the byte at 0x400 reads 0x4F). Composing from
     * bytes keeps unaligned reads right and the result is the CPU's view. */
    const uint16_t probe = 1;
    const bool littleHost = *(const uint8_t *) &probe == 1;
    uint32_t v = 0;
    for (int i = 0; i < width; i++) {
        uint32_t a = phys + (uint32_t) i;
        v = (v << 8) | ram[littleHost ? (a ^ 3u) : a];
    }
    *out = v;
    return true;
}

const char *n64_core_version(void) { return gVersion.c_str(); }
