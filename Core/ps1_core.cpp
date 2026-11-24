/* ps1_core.cpp — the Sony PlayStation, PCSX ReARMed underneath.
 *
 * The shape follows Core/genesis_core.cpp: an opaque wrapper, an error string
 * on every failure path, the frame captured into OUR buffer, and reads that
 * refuse outside the console's RAM rather than inventing values.
 *
 * ⛔ THE CORE IS DRIVEN THROUGH ITS OWN LIBRETRO FRONTEND. PCSX ReARMed's
 * frontend/libretro.c is linked in-process and this file is the "frontend" it
 * talks to: it answers the environment callback (BIOS folder, options, memory
 * map, pixel format), receives video and audio, and reports the pad. That file
 * is where upstream keeps disc loading, multi-disc, BIOS selection and states
 * working, so using it beats rebuilding those from libpcsxcore by hand.
 *
 * ⛔ EVERY CORE SYMBOL IS PREFIXED. scripts/ps1-prefix-symbols.py renames each
 * global the PCSX objects define to oga_ps1_<name> after the compile (the
 * core has generic C names — Config, gpu, spu — and libretro's retro_* entry
 * points, any of which another core in this link could share). So the calls
 * below name oga_ps1_retro_init and friends. They are declared here by hand;
 * libretro.h supplies only the types.
 *
 * ⛔ THE CORE IS A SINGLETON, AND THE WRAPPER SAYS SO. libretro cores are one
 * global machine; ps1_create refuses a second while one lives.
 */
#include "ps1_core.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "libretro.h"

/* ------------------------------------------------------------------ the core
 *
 * The prefixed libretro entry points and the one core global this wrapper
 * reads (the disc serial). C linkage: the core is C. */
extern "C" {
void oga_ps1_retro_set_environment(retro_environment_t);
void oga_ps1_retro_set_video_refresh(retro_video_refresh_t);
void oga_ps1_retro_set_audio_sample(retro_audio_sample_t);
void oga_ps1_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void oga_ps1_retro_set_input_poll(retro_input_poll_t);
void oga_ps1_retro_set_input_state(retro_input_state_t);
void oga_ps1_retro_init(void);
void oga_ps1_retro_deinit(void);
bool oga_ps1_retro_load_game(const struct retro_game_info *game);
void oga_ps1_retro_unload_game(void);
void oga_ps1_retro_run(void);
void oga_ps1_retro_get_system_av_info(struct retro_system_av_info *info);
void oga_ps1_retro_set_controller_port_device(unsigned port, unsigned device);
size_t oga_ps1_retro_serialize_size(void);
bool oga_ps1_retro_serialize(void *data, size_t size);
bool oga_ps1_retro_unserialize(const void *data, size_t size);
void *oga_ps1_retro_get_memory_data(unsigned id);
size_t oga_ps1_retro_get_memory_size(unsigned id);
/* libpcsxcore/misc.c: the boot serial CheckCdrom read from SYSTEM.CNF. */
extern char oga_ps1_CdromId[10];
}

/* ------------------------------------------------------------------ wrapper */

struct Ps1Core {
    bool loaded = false;
    bool running = false;
    bool hleBios = true;
    unsigned long long frames = 0;

    uint16_t padMask = 0;               /* bit i = PS1_BTN_* i is down */
    int16_t axis[2][2] = {{0, 0}, {0, 0}};  /* [stick][x,y], libretro's -32768..32767 */
    double fps = 0.0;                   /* the loaded disc's frame rate */
    std::string error;
    std::string lastCoreError;          /* the core's own last error-level log line */
    std::string biosDir;                /* "" = HLE */
    std::string saveDir;                /* directory of savePath, for the core's asks */
    std::string savePath;               /* the slot-1 memory card file; "" = none */
    std::vector<uint8_t> cardShadow;    /* last-flushed card bytes, for change detection */

    enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
    std::vector<uint8_t> frameRGBA;
    int fbW = 0, fbH = 0;

    /* From the memory map the core publishes at load: the scratchpad, and
     * whether a BIOS ROM region is mapped (only when a real BIOS is in use). */
    const uint8_t *scratch = nullptr;
    bool biosRomMapped = false;

    /* Audio from the core at 44100 Hz, resampled to the app's rate on read.
     * The mutex is load-bearing: the iOS app drains audio on its render thread
     * while frames run elsewhere (the PSP backend learned this as a tear). */
    std::mutex audioMutex;
    std::vector<int16_t> audio;         /* interleaved stereo at kCoreRate */
    size_t audioRead = 0;               /* frames consumed */
    double audioFrac = 0.0;             /* resampler phase carried across reads */
};

namespace {

constexpr int kCoreRate = 44100;
constexpr size_t kAudioCapFrames = 16384;   /* ~371 ms at 44100 Hz */

Ps1Core *gLive = nullptr;                   /* the singleton guard */

void SetError(Ps1Core *c, const char *msg) { if (c) c->error = msg ? msg : "unknown error"; }

/* ---- the core's options, answered the same every time ----
 *
 * Anything not listed is unanswered, which leaves the core's own default. */
const char *OptionValue(const Ps1Core *c, const char *key)
{
    if (!key) return nullptr;
    /* XRGB8888 frames: the conversion below is then one byte shuffle. */
    if (!strcmp(key, "pcsx_rearmed_rgb32_output")) return "enabled";
    /* "auto" looks for a BIOS file in the system folder and falls back to
     * HLE; with no folder there is nothing to look in, so say HLE outright. */
    if (!strcmp(key, "pcsx_rearmed_bios")) return c->biosDir.empty() ? "HLE" : "auto";
    if (!strcmp(key, "pcsx_rearmed_show_bios_bootlogo")) return "disabled";
    /* Slot 1 is libretro-managed: the card lives in memory and this wrapper
     * persists it to the app's file. Slot 2 is empty. */
    if (!strcmp(key, "pcsx_rearmed_memcard1")) return "libretro";
    if (!strcmp(key, "pcsx_rearmed_memcard2")) return "disabled";
    /* The interpreter is the only CPU in this build (DRC_DISABLE); saying so
     * keeps the core from asking for a dynarec that is not compiled. */
    if (!strcmp(key, "pcsx_rearmed_drc")) return "disabled";
    /* Never skip frames on our behalf: the app paces frames itself. */
    if (!strcmp(key, "pcsx_rearmed_frameskip_type")) return "disabled";
    /* The pad is a DualShock (set at load). L3 + R3 together flip it between
     * digital and analog by hand, the way the real controller's ANALOG button
     * does; the core also switches it on for a game that asks. */
    if (!strcmp(key, "pcsx_rearmed_analog_combo")) return "l3+r3";
    return nullptr;
}

/* ---- libretro callbacks ----
 *
 * libretro has no user pointer, so the callbacks reach the one live wrapper
 * through gLive — the same reason the core is a singleton at all. */

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
    if (getenv("OGA_PS1_LOG")) fprintf(stderr, "[pcsx] %s\n", line);
}

bool EnvCb(unsigned cmd, void *data)
{
    Ps1Core *c = gLive;
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
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = (struct retro_variable *) data;
        if (!c || !v) return false;
        v->value = OptionValue(c, v->key);
        return v->value != nullptr;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *) data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(const enum retro_pixel_format *) data;
        if (f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565) return false;
        if (c) c->pixfmt = f;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
        const struct retro_memory_map *m = (const struct retro_memory_map *) data;
        if (!c || !m) return true;
        c->scratch = nullptr;
        c->biosRomMapped = false;
        for (unsigned i = 0; i < m->num_descriptors; i++) {
            const struct retro_memory_descriptor &d = m->descriptors[i];
            if (d.start == PS1_SCRATCH_BASE && d.len >= PS1_SCRATCH_SIZE && d.ptr)
                c->scratch = (const uint8_t *) d.ptr + d.offset;
            if (d.start == 0x1FC00000u && d.ptr) c->biosRomMapped = true;
        }
        return true;
    }
    default:
        /* Everything else (messages, rumble, controller info, VFS, core option
         * registration) is an offer the core can live without; unanswered. */
        return false;
    }
}

void VideoCb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    Ps1Core *c = gLive;
    /* NULL data is libretro's "same frame as last time": keep the previous. */
    if (!c || !data || width == 0 || height == 0) return;
    c->frameRGBA.resize((size_t) width * height * 4);
    for (unsigned y = 0; y < height; y++) {
        const uint8_t *row = (const uint8_t *) data + (size_t) y * pitch;
        uint8_t *dst = c->frameRGBA.data() + (size_t) y * width * 4;
        if (c->pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            const uint32_t *src = (const uint32_t *) row;
            for (unsigned x = 0; x < width; x++) {
                uint32_t p = src[x];
                dst[x * 4 + 0] = (uint8_t) (p >> 16);
                dst[x * 4 + 1] = (uint8_t) (p >> 8);
                dst[x * 4 + 2] = (uint8_t) p;
                dst[x * 4 + 3] = 0xFF;
            }
        } else {
            const uint16_t *src = (const uint16_t *) row;
            for (unsigned x = 0; x < width; x++) {
                uint16_t p = src[x];
                uint8_t r = (uint8_t) ((p >> 11) & 0x1F), g = (uint8_t) ((p >> 5) & 0x3F),
                        b = (uint8_t) (p & 0x1F);
                dst[x * 4 + 0] = (uint8_t) ((r << 3) | (r >> 2));
                dst[x * 4 + 1] = (uint8_t) ((g << 2) | (g >> 4));
                dst[x * 4 + 2] = (uint8_t) ((b << 3) | (b >> 2));
                dst[x * 4 + 3] = 0xFF;
            }
        }
    }
    c->fbW = (int) width;
    c->fbH = (int) height;
}

size_t AudioBatchCb(const int16_t *data, size_t frames)
{
    Ps1Core *c = gLive;
    if (!c || !data || frames == 0) return frames;
    std::lock_guard<std::mutex> lock(c->audioMutex);
    size_t have = c->audio.size() / 2 - c->audioRead;
    /* A reader that stopped draining must not grow the buffer forever: drop
     * the oldest audio, which is what a listener would hear lag behind. */
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
    Ps1Core *c = gLive;
    if (!c || port != 0) return 0;
    if (device == RETRO_DEVICE_ANALOG) {
        if (index > RETRO_DEVICE_INDEX_ANALOG_RIGHT || id > RETRO_DEVICE_ID_ANALOG_Y) return 0;
        return c->axis[index][id];
    }
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    /* libretro joypad ids → this core's buttons. Sony's layout on libretro's
     * SNES-shaped pad: Cross is B (bottom), Circle is A (right), Triangle is X
     * (top), Square is Y (left) — upstream's own input descriptors say so. */
    int b = -1;
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_B:      b = PS1_BTN_CROSS; break;
    case RETRO_DEVICE_ID_JOYPAD_A:      b = PS1_BTN_CIRCLE; break;
    case RETRO_DEVICE_ID_JOYPAD_X:      b = PS1_BTN_TRIANGLE; break;
    case RETRO_DEVICE_ID_JOYPAD_Y:      b = PS1_BTN_SQUARE; break;
    case RETRO_DEVICE_ID_JOYPAD_L:      b = PS1_BTN_L1; break;
    case RETRO_DEVICE_ID_JOYPAD_R:      b = PS1_BTN_R1; break;
    case RETRO_DEVICE_ID_JOYPAD_L2:     b = PS1_BTN_L2; break;
    case RETRO_DEVICE_ID_JOYPAD_R2:     b = PS1_BTN_R2; break;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: b = PS1_BTN_SELECT; break;
    case RETRO_DEVICE_ID_JOYPAD_START:  b = PS1_BTN_START; break;
    case RETRO_DEVICE_ID_JOYPAD_UP:     b = PS1_BTN_UP; break;
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   b = PS1_BTN_DOWN; break;
    case RETRO_DEVICE_ID_JOYPAD_LEFT:   b = PS1_BTN_LEFT; break;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:  b = PS1_BTN_RIGHT; break;
    case RETRO_DEVICE_ID_JOYPAD_L3:     b = PS1_BTN_L3; break;
    case RETRO_DEVICE_ID_JOYPAD_R3:     b = PS1_BTN_R3; break;
    default: return 0;
    }
    return (c->padMask >> b) & 1u;
}

/* ---- the memory card ---- */

uint8_t *CardData(size_t *size)
{
    void *p = oga_ps1_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t n = oga_ps1_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (size) *size = p ? n : 0;
    return (uint8_t *) p;
}

/* Write the card back only when it changed: a game that never saves must not
 * rewrite its card on every stop (the Genesis SRAM rule). */
void FlushCard(Ps1Core *c)
{
    if (!c || !c->loaded || c->savePath.empty()) return;
    size_t n = 0;
    uint8_t *card = CardData(&n);
    if (!card || n == 0) return;
    if (c->cardShadow.size() == n && memcmp(c->cardShadow.data(), card, n) == 0) return;
    FILE *f = fopen(c->savePath.c_str(), "wb");
    if (!f) return;
    size_t wrote = fwrite(card, 1, n, f);
    fclose(f);
    if (wrote == n) c->cardShadow.assign(card, card + n);
}

std::string DirOf(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

void Unload(Ps1Core *c)
{
    if (!c || !c->loaded) return;
    FlushCard(c);
    oga_ps1_retro_unload_game();
    c->loaded = false;
    c->running = false;
}

} // namespace

/* ------------------------------------------------------------------ API */

Ps1Core *ps1_create(void)
{
    if (gLive) return nullptr;      /* one machine; see the note at the top */
    Ps1Core *c = new (std::nothrow) Ps1Core();
    if (!c) return nullptr;
    gLive = c;
    oga_ps1_retro_set_environment(EnvCb);
    oga_ps1_retro_set_video_refresh(VideoCb);
    oga_ps1_retro_set_audio_sample(AudioSampleCb);
    oga_ps1_retro_set_audio_sample_batch(AudioBatchCb);
    oga_ps1_retro_set_input_poll(InputPollCb);
    oga_ps1_retro_set_input_state(InputStateCb);
    oga_ps1_retro_init();
    return c;
}

void ps1_destroy(Ps1Core *core)
{
    if (!core) return;
    Unload(core);
    if (gLive == core) {
        oga_ps1_retro_deinit();
        gLive = nullptr;
    }
    delete core;
}

void ps1_set_bios_dir(Ps1Core *core, const char *dir)
{
    if (!core) return;
    core->biosDir = dir ? dir : "";
    while (core->biosDir.size() > 1 &&
           (core->biosDir.back() == '/' || core->biosDir.back() == '\\'))
        core->biosDir.pop_back();
}

bool ps1_load_disc(Ps1Core *core, const char *disc_path, const char *save_path,
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
    core->scratch = nullptr;
    core->biosRomMapped = false;
    core->frameRGBA.clear();
    core->fbW = core->fbH = 0;
    core->cardShadow.clear();
    {
        std::lock_guard<std::mutex> lock(core->audioMutex);
        core->audio.clear();
        core->audioRead = 0;
        core->audioFrac = 0.0;
    }
    core->savePath = save_path ? save_path : "";
    core->saveDir = DirOf(core->savePath);

    /* The core reads the file itself (need_fullpath), so a path is enough —
     * and it is what lets a .cue find its .bin tracks beside it. */
    FILE *probe = fopen(disc_path, "rb");
    if (!probe) { SetError(core, "Could not open the disc image."); return false; }
    fclose(probe);

    /* A PS-EXE has no disc and the core leaves the last disc's serial in
     * place, so start from empty; CheckCdrom fills it for a real disc. */
    oga_ps1_CdromId[0] = '\0';

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = disc_path;
    if (!oga_ps1_retro_load_game(&info)) {
        std::string msg = "PCSX ReARMed could not load that file as a PlayStation disc.";
        if (!core->lastCoreError.empty()) msg += " (" + core->lastCoreError + ")";
        SetError(core, msg.c_str());
        return false;
    }
    /* A DualShock, not a digital pad: it starts in digital mode, so every
     * digital game still sees the pad it expects, and the sticks are there
     * for the games that use them. upstream's subclass id, by value. */
    oga_ps1_retro_set_controller_port_device(0, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1));
    core->loaded = true;
    {
        struct retro_system_av_info av;
        memset(&av, 0, sizeof(av));
        oga_ps1_retro_get_system_av_info(&av);
        core->fps = av.timing.fps > 0.0 ? av.timing.fps : 0.0;
    }

    /* HLE or a real BIOS: the core publishes the BIOS ROM region in its
     * memory map only when a real BIOS is in use (set_retro_memmap), so its
     * absence means HLE, including a folder that held no usable BIOS. */
    core->hleBios = !core->biosRomMapped;

    /* The memory card: restore the app's file into slot 1. */
    size_t cardSize = 0;
    uint8_t *card = CardData(&cardSize);
    if (card && cardSize > 0) {
        if (!core->savePath.empty()) {
            if (FILE *f = fopen(core->savePath.c_str(), "rb")) {
                std::vector<uint8_t> buf(cardSize);
                size_t got = fread(buf.data(), 1, cardSize, f);
                fclose(f);
                if (got == cardSize) memcpy(card, buf.data(), cardSize);
            }
        }
        core->cardShadow.assign(card, card + cardSize);
    }

    /* "SLUS99999" is the core's placeholder for "no serial" (a PS-EXE, or a
     * disc whose SYSTEM.CNF names none): it would key every such disc to one
     * reader, so it is reported as no serial at all. */
    if (code_out && strncmp(oga_ps1_CdromId, "SLUS99999", sizeof(oga_ps1_CdromId)) != 0) {
        size_t n = strnlen(oga_ps1_CdromId, sizeof(oga_ps1_CdromId));
        if (n > 15) n = 15;
        memcpy(code_out, oga_ps1_CdromId, n);
        code_out[n] = '\0';
    }
    return true;
}

bool ps1_start(Ps1Core *core)
{
    if (!core || !core->loaded) { SetError(core, "No PlayStation disc is loaded."); return false; }
    core->running = true;
    return true;
}

void ps1_stop(Ps1Core *core)
{
    if (!core) return;
    FlushCard(core);
    core->running = false;
}

bool ps1_running(Ps1Core *core)
{
    return core && core->running;
}

bool ps1_frame(Ps1Core *core)
{
    if (!core || !core->loaded || !core->running) return false;
    oga_ps1_retro_run();
    core->frames++;
    return true;
}

bool ps1_framebuffer(Ps1Core *core, int *width, int *height)
{
    if (!core || core->frameRGBA.empty()) return false;
    if (width) *width = core->fbW;
    if (height) *height = core->fbH;
    return true;
}

const uint8_t *ps1_framebuffer_ptr(Ps1Core *core)
{
    if (!core || core->frameRGBA.empty()) return nullptr;
    return core->frameRGBA.data();
}

void ps1_set_button(Ps1Core *core, int ps1_button, bool down)
{
    if (!core || ps1_button < 0 || ps1_button >= PS1_BTN_COUNT) return;
    uint16_t bit = (uint16_t) (1u << ps1_button);
    if (down) core->padMask |= bit;
    else core->padMask &= (uint16_t) ~bit;
}

void ps1_set_analog(Ps1Core *core, int stick, float x, float y)
{
    if (!core || stick < PS1_STICK_LEFT || stick > PS1_STICK_RIGHT) return;
    auto axis = [](float v) -> int16_t {
        if (!(v == v)) v = 0.0f;                  /* NaN reads as centred */
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        return (int16_t) (v >= 0.0f ? v * 32767.0f : v * 32768.0f);
    };
    core->axis[stick][0] = axis(x);
    core->axis[stick][1] = axis(y);
}

double ps1_frame_rate(Ps1Core *core)
{
    return core && core->loaded ? core->fps : 0.0;
}

int ps1_read_audio(Ps1Core *core, int16_t *out, int max_frames)
{
    if (!core || !out || max_frames <= 0) return 0;
    std::lock_guard<std::mutex> lock(core->audioMutex);
    size_t avail = core->audio.size() / 2 - core->audioRead;
    if (avail < 2) return 0;
    /* Linear resampling 44100 → 32768, the PSP backend's method: step through
     * the source at 44100/32768 frames per output frame, carrying the phase so
     * consecutive reads join without a click. */
    const double step = (double) kCoreRate / PS1_APP_AUDIO_RATE;
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

bool ps1_save_state(Ps1Core *core, const char *path)
{
    if (!core || !core->loaded || !path) { SetError(core, "No PlayStation game is running."); return false; }
    FlushCard(core);
    size_t n = oga_ps1_retro_serialize_size();
    std::vector<uint8_t> buf(n);
    if (n == 0 || !oga_ps1_retro_serialize(buf.data(), n)) {
        SetError(core, "PCSX ReARMed could not save the state.");
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) { SetError(core, "Could not open the state file for writing."); return false; }
    size_t wrote = fwrite(buf.data(), 1, n, f);
    fclose(f);
    if (wrote != n) { SetError(core, "Could not write the whole state file."); return false; }
    return true;
}

bool ps1_load_state(Ps1Core *core, const char *path)
{
    if (!core || !core->loaded || !path) { SetError(core, "No PlayStation game is running."); return false; }
    FILE *f = fopen(path, "rb");
    if (!f) { SetError(core, "Could not open the state file."); return false; }
    std::vector<uint8_t> buf;
    uint8_t chunk[65536];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) buf.insert(buf.end(), chunk, chunk + got);
    fclose(f);
    if (buf.empty()) { SetError(core, "The state file is empty."); return false; }
    if (!oga_ps1_retro_unserialize(buf.data(), buf.size())) {
        SetError(core, "PCSX ReARMed refused that state file.");
        return false;
    }
    /* The state carries its own card; resync the shadow so the next flush
     * compares against what the game now holds. */
    size_t n = 0;
    if (uint8_t *card = CardData(&n)) core->cardShadow.assign(card, card + n);
    return true;
}

unsigned long long ps1_frames_completed(Ps1Core *core)
{
    return core ? core->frames : 0;
}

const char *ps1_last_error(Ps1Core *core)
{
    return core ? core->error.c_str() : "no core";
}

bool ps1_using_hle_bios(Ps1Core *core)
{
    return core && core->hleBios;
}

bool ps1_read(Ps1Core *core, uint32_t addr, int width, uint32_t *out)
{
    if (!core || !core->loaded || !out) return false;
    if (width != 1 && width != 2 && width != 4) return false;

    /* The segment bits only choose cached/uncached/user views of one physical
     * space: strip them, then serve RAM (mirrored through 8 MiB) or the
     * scratchpad. */
    uint32_t phys = addr & 0x1FFFFFFFu;
    const uint8_t *base = nullptr;
    uint32_t off = 0;
    if (phys + (uint32_t) width <= 0x00800000u) {
        base = (const uint8_t *) oga_ps1_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        off = phys & (PS1_RAM_SIZE - 1);
        if (off + (uint32_t) width > PS1_RAM_SIZE) return false;   /* straddles a mirror */
    } else if (phys >= PS1_SCRATCH_BASE && phys + (uint32_t) width <= PS1_SCRATCH_BASE + PS1_SCRATCH_SIZE) {
        base = core->scratch;
        off = phys - PS1_SCRATCH_BASE;
    }
    if (!base) return false;

    /* Little-endian, assembled byte by byte so the answer is the CPU's view
     * whatever the host's byte order. */
    uint32_t v = 0;
    for (int i = width - 1; i >= 0; i--) v = (v << 8) | base[off + (uint32_t) i];
    *out = v;
    return true;
}
