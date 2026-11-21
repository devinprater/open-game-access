/* genesis_core.cpp — the Sega Genesis / Mega Drive console, Genesis Plus GX underneath.
 *
 * The shape follows Core/snes_core.cpp: an opaque wrapper, an error string on
 * every failure path, the frame captured into OUR buffer (the core's bitmap is
 * its own), and reads that refuse outside the console's RAM rather than
 * inventing values.
 *
 * ⛔ THE CORE IS A SINGLETON, AND THE WRAPPER SAYS SO. Genesis Plus GX is one
 * global machine (cart, work_ram, input, bitmap, snd are globals in the tree),
 * not an object like Mesen's consoles. pokecore owns exactly one live backend
 * at a time and destroys it before creating another, so one wrapper is the
 * truth; genesis_create refuses a second while one lives rather than handing
 * out two handles to the same machine.
 *
 * ⛔ THE FRONTEND SHIM LIVES HERE. The core's TUs include "osd.h" (Core/gpgx/)
 * and reference three things no library provides: the `config` global, the
 * optional-ROM path strings, and load_archive(). All three are defined in this
 * file, next to the only consumer that configures them.
 */
#include "genesis_core.h"

/* \u26d4 INCLUDE ORDER AND LINKAGE, BOTH LOAD-BEARING.
 *
 * The standard headers come FIRST: the core's m68k.h does
 * `#define uint unsigned int`, and any libc++ header included after it is
 * poisoned (the SDK's own `typedef unsigned int uint;` in sys/types.h
 * becomes a syntax error). The host compiler tolerated the order; Apple
 * clang did not, and it was right not to.
 *
 * The core's own headers are included inside extern "C": Genesis Plus GX is
 * C \u2014 its TUs are compiled as C in the build scripts, their native language
 * (as C++ they lean on implicit void* conversions and implicit declarations
 * that GCC's -fpermissive forgives and Apple clang refuses). The headers
 * carry no linkage guards of their own, so the glue declares the linkage,
 * and the shim definitions below are defined inside extern "C" to match. */
#include <array>
#include <cstdio>
#include <new>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
/* "shared.h" is the umbrella every core TU includes; it pulls osd.h (ours,
 * via the gpgx include dir) and the subsystem headers. */
#include "shared.h"
#include "cart_hw/sram.h"
#include "cart_hw/md_cart.h"
}

/* ------------------------------------------------------------------ shim
 *
 * The frontend contract, as documented in Core/gpgx/osd.h. Defined inside
 * extern "C": the C translation units reference these exact symbols.
 */

extern "C" {

t_config config;

/* No optional ROMs ship with the app: empty paths, and load_archive below
 * fails cleanly on an empty path, which is the core's "not present" answer. */
char GG_ROM[256] = "";
char AR_ROM[256] = "";
char SK_ROM[256] = "";
char SK_UPMEM[256] = "";
char MD_BIOS[256] = "";
char CD_BIOS_US[256] = "";
char CD_BIOS_EU[256] = "";
char CD_BIOS_JP[256] = "";
char MS_BIOS_US[256] = "";
char MS_BIOS_EU[256] = "";
char MS_BIOS_JP[256] = "";
char GG_BIOS[256] = "";

int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension)
{
    (void) extension;   /* OGA hands the core real files; there is no archive layer */
    if (!filename || !*filename || !buffer || maxsize <= 0) return 0;
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    size_t n = fread(buffer, 1, (size_t) maxsize, f);
    fclose(f);
    return (int) n;
}

/* crc32 for the two core files that call it undeclared (see Core/gpgx/osd.h).
 * This is zlib's algorithm and chaining semantics exactly, so the values the
 * core computes for its ROM/SRAM fingerprints match every other frontend.
 * Defined here rather than taken from libz so the symbol's provenance is
 * this file, next to the declaration in osd.h that promises it. The
 * Crc32Of table below is the same polynomial. */
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len)
{
    static const uint32_t kPoly = 0xEDB88320u;
    crc = crc ^ 0xFFFFFFFFul;
    for (unsigned int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1u) ? (kPoly ^ (crc >> 1)) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFul;
}

} // extern "C"

namespace {

/* Defaults in the shape of upstream's own frontends (gx/config.c): HQ sound
 * on, no filtering, region and clock AUTO (decided per ROM by its header),
 * no boot ROM, no lock-on, no add-on. The pad type is NOT set here:
 * input_init() chooses PAD3B/PAD6B per game from the ROM's peripheral flags,
 * which is the core's own convention. */
void SetConfigDefaults()
{
    memset(&config, 0, sizeof(config));
    config.hq_fm = 1;
    config.hq_psg = 1;
    config.filter = 0;
    config.ym2612 = 0;          /* the stock YM2612 core */
    config.ym2413 = 0;          /* SMS FM: not our console */
    config.mono = 0;
    config.psg_preamp = 0;
    config.fm_preamp = 0;
    config.cdda_volume = 100;
    config.pcm_volume = 100;
    config.lp_range = 0x9999;   /* upstream's default low-pass range (60%) */
    config.low_freq = 200;
    config.high_freq = 8000;
    config.lg = 100;
    config.mg = 100;
    config.hg = 100;
    config.system = 0;          /* auto: load_rom decides from the file */
    config.region_detect = 0;   /* auto: the ROM header decides NTSC/PAL */
    config.master_clock = 0;    /* auto */
    config.vdp_mode = 0;        /* auto */
    config.force_dtack = 0;
    config.addr_error = 0;
    config.bios = 0;            /* no boot ROM (none ships; cartridges do not need one) */
    config.lock_on = 0;         /* no Game Genie / Sonic & Knuckles lock-on */
    config.add_on = 0;          /* no Sega CD */
    config.overscan = 0;
    config.aspect_ratio = 0;
    config.ntsc = 0;            /* no NTSC filtering: the framebuffer is the game's pixels */
    config.lcd = 0;
    config.gg_extra = 0;
    config.left_border = 0;
    config.render = 0;          /* progressive */
    config.invert_mouse = 0;
    config.gun_cursor = 0;
    config.overclock = 100;
    config.no_sprite_limit = 0; /* hardware sprite limits, as the games expect */
    config.enhanced_vscroll = 0;
    config.enhanced_vscroll_limit = 16;
    config.cd_latency = 0;
}

} // namespace

/* ------------------------------------------------------------------ core */

struct GenesisCore {
    bool loaded = false;
    bool running = false;
    unsigned long long frames = 0;

    uint16_t padMask = 0;               /* INPUT_* bits, port 1 */
    std::string error;
    std::string savePath;               /* the battery .sav; empty = no save file */
    bool sramActive = false;            /* this cartridge has battery SRAM in sram.sram */
    std::vector<uint8_t> sramShadow;    /* last-flushed SRAM bytes, for change detection */

    std::vector<uint8_t> bitmapStorage; /* our allocation behind the core's bitmap */
    std::vector<uint8_t> frameRGBA;     /* converted frame, RGBA8888 */
    int fbW = 0, fbH = 0;

    /* One frame of generated audio, staged by genesis_frame and drained by
     * genesis_read_audio — the GBA ring's model. audio_update() GENERATES a
     * frame's samples when called, so it must be called exactly once per
     * emulated frame, never once per drain. */
    std::vector<int16_t> audioStaging;
    size_t audioStaged = 0;             /* frames (stereo pairs) staged */
    size_t audioDrained = 0;            /* frames already handed out */
};

/* The singleton guard described at the top of this file. */
static GenesisCore *gLiveCore = nullptr;

namespace {

void SetError(GenesisCore *c, const char *msg) { if (c) c->error = msg ? msg : "unknown error"; }

/* CRC32 of the image the core actually runs (cart.rom after any .smd
 * deinterleave), the SNES identity rule: content, not filename. */
uint32_t Crc32Of(const uint8_t *p, size_t n)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* Write the battery SRAM back to its file, but only when it changed: a game
 * that never saves must not rewrite its .sav on every stop. Mirrors the DS
 * backend's flush-on-change discipline. EEPROM/flash cartridges keep their
 * saves inside the chip emulation, NOT in sram.sram — writing sram.sram for
 * one of those would persist the wrong bytes, so they are excluded here and
 * persist through save STATES instead. Stated in the header's contract. */
void FlushSram(GenesisCore *c)
{
    if (!c || !c->sramActive || c->savePath.empty()) return;
    if (c->sramShadow.size() == GEN_RAM_SIZE &&
        memcmp(c->sramShadow.data(), sram.sram, GEN_RAM_SIZE) == 0)
        return;
    FILE *f = fopen(c->savePath.c_str(), "wb");
    if (!f) return;
    fwrite(sram.sram, 1, GEN_RAM_SIZE, f);
    fclose(f);
    c->sramShadow.assign(sram.sram, sram.sram + GEN_RAM_SIZE);
}

} // namespace

GenesisCore *genesis_create(void)
{
    if (gLiveCore) return nullptr;      /* one machine; see the note at the top */
    GenesisCore *c = new (std::nothrow) GenesisCore();
    if (!c) return nullptr;
    SetConfigDefaults();
    gLiveCore = c;
    return c;
}

void genesis_destroy(GenesisCore *core)
{
    if (!core) return;
    genesis_stop(core);
    if (gLiveCore == core) gLiveCore = nullptr;
    delete core;
}

bool genesis_load_rom(GenesisCore *core, const char *rom_path, const char *save_path,
                      char code_out[16])
{
    if (!core || !rom_path) { SetError(core, "No ROM path given."); return false; }
    core->error.clear();
    core->loaded = false;
    core->running = false;
    core->frames = 0;
    core->padMask = 0;
    core->sramActive = false;
    core->sramShadow.clear();
    core->audioStaged = core->audioDrained = 0;

    /* The frame bitmap, at the core's maximum viewport (720x576) in its 32bpp
     * rendering mode, pitch in BYTES. Allocated before load: the core's render
     * init sizes itself against bitmap on system_init. */
    const int kMaxW = 720, kMaxH = 576;
    core->bitmapStorage.assign((size_t) kMaxW * kMaxH * 4, 0);
    memset(&bitmap, 0, sizeof(bitmap));
    bitmap.width = kMaxW;
    bitmap.height = kMaxH;
    bitmap.pitch = kMaxW * 4;
    bitmap.data = core->bitmapStorage.data();

    /* load_rom takes a mutable char* (it keeps the path in rompath). */
    char pathBuf[1024];
    snprintf(pathBuf, sizeof(pathBuf), "%s", rom_path);
    int size = load_rom(pathBuf);
    if (size <= 0) {
        SetError(core, "Genesis Plus GX could not load that file as a Genesis ROM.");
        return false;
    }

    /* ⛔ ROUTING IS BY EXTENSION, IDENTITY IS BY CONTENT. A .bin can be almost
     * anything; the core reports what it actually found in system_hw, and
     * anything that is not the Genesis/Mega Drive hardware is refused in words
     * rather than played on the wrong console. */
    if (system_hw == SYSTEM_SMS || system_hw == SYSTEM_SMS2 ||
        system_hw == SYSTEM_GG || system_hw == SYSTEM_GGMS ||
        system_hw == SYSTEM_MARKIII || system_hw == SYSTEM_SG ||
        system_hw == SYSTEM_SGII || system_hw == SYSTEM_SGII_RAM_EXT) {
        SetError(core, "That file is a Master System or Game Gear ROM, not a Genesis one. This build does not run those yet.");
        return false;
    }
    if (system_hw == SYSTEM_MCD) {
        SetError(core, "That file is a Sega CD image. This build does not run Sega CD.");
        return false;
    }
    if (!(system_hw & SYSTEM_MD)) {
        SetError(core, "That file did not identify as a Genesis or Mega Drive ROM.");
        return false;
    }

    audio_init(32768, 0.0);     /* the app's audio rate; frame rate is the core's own */
    system_init();
    system_reset();

    /* Battery SRAM: restore the .sav the app keeps beside the ROM, when this
     * cartridge actually has SRAM in the sram.sram buffer (see FlushSram for
     * the EEPROM exclusion). */
    core->savePath = save_path ? save_path : "";
    core->sramActive = (sram.detected && sram.type == DEFAULT_SRAM);
    if (core->sramActive && !core->savePath.empty()) {
        if (FILE *f = fopen(core->savePath.c_str(), "rb")) {
            size_t got = fread(sram.sram, 1, GEN_RAM_SIZE, f);
            (void) got;
            fclose(f);
        }
        core->sramShadow.assign(sram.sram, sram.sram + GEN_RAM_SIZE);
    }

    if (code_out) {
        uint32_t crc = Crc32Of(cart.rom, cart.romsize);
        snprintf(code_out, 16, "crc:%08x", crc);
    }

    core->loaded = true;
    return true;
}

bool genesis_start(GenesisCore *core)
{
    if (!core || !core->loaded) { SetError(core, "No Genesis ROM is loaded."); return false; }
    /* The machine was reset at load; the app drives frames synchronously, so
     * starting is a state change here, not a thread spawn (contrast Mesen). */
    core->running = true;
    return true;
}

void genesis_stop(GenesisCore *core)
{
    if (!core) return;
    FlushSram(core);
    core->running = false;
}

bool genesis_running(GenesisCore *core)
{
    return core && core->running;
}

bool genesis_frame(GenesisCore *core)
{
    if (!core || !core->loaded || !core->running) return false;

    input.pad[0] = core->padMask;
    system_frame_gen(0);

    /* Capture the visible viewport, converting the core's 32bpp words
     * (0xAARRGGBB bit layout) into the RGBA8888 bytes every other OGA backend
     * delivers. The conversion is ours because pokecore copies backend pixels
     * verbatim — the SNES rule, learned from its palette-index framebuffer. */
    int w = bitmap.viewport.w, h = bitmap.viewport.h;
    if (w > 0 && h > 0 && bitmap.data) {
        core->frameRGBA.assign((size_t) w * h * 4, 0);
        const int pitchWords = bitmap.pitch / 4;
        const uint32_t *srcBase = (const uint32_t *) bitmap.data;
        for (int y = 0; y < h; y++) {
            const uint32_t *src = srcBase + (bitmap.viewport.y + y) * pitchWords + bitmap.viewport.x;
            uint8_t *dst = core->frameRGBA.data() + (size_t) y * w * 4;
            for (int x = 0; x < w; x++) {
                uint32_t p = src[x];
                dst[x * 4 + 0] = (uint8_t) ((p >> 16) & 0xFF);
                dst[x * 4 + 1] = (uint8_t) ((p >> 8) & 0xFF);
                dst[x * 4 + 2] = (uint8_t) (p & 0xFF);
                dst[x * 4 + 3] = 0xFF;
            }
        }
        core->fbW = w;
        core->fbH = h;
    }

    /* Stage this frame's audio. One audio_update per emulated frame, exactly
     * as upstream's frontends call it; drains happen in genesis_read_audio. */
    core->audioStaging.assign(4096, 0);
    int produced = audio_update(core->audioStaging.data());
    if (produced < 0) produced = 0;
    if (produced > 2048) produced = 2048;
    core->audioStaged = (size_t) produced;
    core->audioDrained = 0;

    core->frames++;
    return true;
}

bool genesis_framebuffer(GenesisCore *core, int *width, int *height)
{
    if (!core || core->frameRGBA.empty()) return false;
    if (width) *width = core->fbW;
    if (height) *height = core->fbH;
    return true;
}

const uint8_t *genesis_framebuffer_ptr(GenesisCore *core)
{
    if (!core || core->frameRGBA.empty()) return nullptr;
    return core->frameRGBA.data();
}

void genesis_set_button(GenesisCore *core, int genesis_button, bool down)
{
    if (!core) return;
    static const uint16_t kBit[GEN_BTN_COUNT] = {
        INPUT_A, INPUT_B, INPUT_C, INPUT_X, INPUT_Y, INPUT_Z,
        INPUT_MODE, INPUT_START, INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT,
    };
    if (genesis_button < 0 || genesis_button >= GEN_BTN_COUNT) return;
    if (down) core->padMask |= kBit[genesis_button];
    else core->padMask &= (uint16_t) ~kBit[genesis_button];
    if (core->loaded) input.pad[0] = core->padMask;
}

int genesis_read_audio(GenesisCore *core, int16_t *out, int max_frames)
{
    if (!core || !out || max_frames <= 0) return 0;
    size_t avail = core->audioStaged - core->audioDrained;
    size_t n = avail < (size_t) max_frames ? avail : (size_t) max_frames;
    if (n == 0) return 0;
    memcpy(out, core->audioStaging.data() + core->audioDrained * 2, n * 2 * sizeof(int16_t));
    core->audioDrained += n;
    return (int) n;
}

bool genesis_save_state(GenesisCore *core, const char *path)
{
    if (!core || !core->loaded || !path) { SetError(core, "No Genesis game is running."); return false; }
    FlushSram(core);    /* a state and its .sav must not disagree about which is newer */
    std::vector<uint8_t> buf(STATE_SIZE);
    int n = state_save(buf.data());
    if (n <= 0) { SetError(core, "Genesis Plus GX could not serialize the state."); return false; }
    FILE *f = fopen(path, "wb");
    if (!f) { SetError(core, "Could not open the state file for writing."); return false; }
    size_t wrote = fwrite(buf.data(), 1, (size_t) n, f);
    fclose(f);
    if (wrote != (size_t) n) { SetError(core, "Could not write the whole state file."); return false; }
    return true;
}

bool genesis_load_state(GenesisCore *core, const char *path)
{
    if (!core || !core->loaded || !path) { SetError(core, "No Genesis game is running."); return false; }
    FILE *f = fopen(path, "rb");
    if (!f) { SetError(core, "Could not open the state file."); return false; }
    std::vector<uint8_t> buf(STATE_SIZE);
    size_t got = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (got == 0) { SetError(core, "The state file is empty."); return false; }
    if (!state_load(buf.data())) { SetError(core, "Genesis Plus GX refused that state file."); return false; }
    /* The state carries its own SRAM; resync the shadow so the next flush
     * compares against what the game now holds, not what it held before. */
    if (core->sramActive)
        core->sramShadow.assign(sram.sram, sram.sram + GEN_RAM_SIZE);
    return true;
}

unsigned long long genesis_frames_completed(GenesisCore *core)
{
    return core ? core->frames : 0;
}

const char *genesis_last_error(GenesisCore *core)
{
    return core ? core->error.c_str() : "no core";
}

bool genesis_read(GenesisCore *core, uint32_t addr, int width, uint32_t *out)
{
    if (!core || !core->loaded || !out) return false;
    if (width != 1 && width != 2) return false;

    /* Work RAM: the 64 KiB the 68k calls its own.
     *
     * ⛔ THE LANES ARE SWAPPED IN MEMORY. This build defines LSB_FIRST (the
     * little-endian host define, as on iOS), and the core keeps RAM in
     * host-word order: the bytes of each 16-bit word sit swapped relative to
     * the CPU's view — the core's own mem68k handlers index with `^ 1` for
     * the same reason. Measured, not assumed: a synthetic ROM's
     * move.w #$1234,$FF0000 lands in raw memory as 34 12. So a read here
     * unswaps and returns THE CPU'S VIEW — an adapter must never have to know
     * the host's endianness to read a game's variables. */
    if (addr >= GEN_RAM_BASE && addr + (uint32_t) width <= GEN_RAM_BASE + GEN_RAM_SIZE) {
        uint32_t off = addr - GEN_RAM_BASE;
        if (width == 1) { *out = work_ram[off ^ 1]; return true; }
        *out = ((uint32_t) work_ram[off + 1] << 8) | work_ram[off];
        return true;
    }

    /* Cartridge SRAM, through the core's own read handlers so the cartridge's
     * mapping (start/end, byte-lane behaviour) is the game's, not a guess. */
    if (sram.detected && addr >= sram.start && addr + (uint32_t) width - 1 <= sram.end) {
        if (width == 1) { *out = sram_read_byte(addr); return true; }
        *out = sram_read_word(addr);
        return true;
    }

    return false;
}
