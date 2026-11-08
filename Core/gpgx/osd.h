/*
 * osd.h — the frontend shim Genesis Plus GX requires, OGA's version.
 *
 * WHY THIS FILE EXISTS. Genesis Plus GX is not a library with a config API:
 * every core translation unit includes "shared.h", which includes "osd.h" —
 * a header THE FRONTEND supplies. It carries the config struct the core reads
 * (sound flavour, region behaviour, pad types), the paths of the optional
 * BIOS / lock-on ROM files, and the one callback the core makes back into the
 * frontend (osd_input_update, once per frame). Upstream's own frontends each
 * ship one (gx/, sdl/, libretro/); this is OGA's, modelled on the libretro
 * port's, which is the actively maintained embedding of the same core.
 *
 * ⛔ IT LIVES IN ITS OWN DIRECTORY ON PURPOSE. The name is generic — "osd.h"
 * is exactly the kind of header that collides across vendored trees — so it
 * sits in Core/gpgx/ and ONLY the Genesis Plus GX translation units (and
 * Core/genesis_core.cpp, their host) get this directory on their include
 * path. Nothing else in the build can see it, and it can see nothing else.
 *
 * ⛔ THE CONFIG STRUCT IS UPSTREAM'S SHAPE, NOT AN EDITED ONE. The core reads
 * fields by name from t_config; a field dropped here is a compile error in
 * the core, and a field reordered is harmless (the core never serializes it),
 * but the struct is copied from libretro/osd.h at the pinned revision so the
 * core at that revision always finds every field it looks for.
 */
#ifndef OGA_GPGX_OSD_H
#define OGA_GPGX_OSD_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_INPUTS 8
#define MAX_KEYS 8
#define MAXPATHLEN 1024

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323823846
#endif

/* Sprite-limit behaviour, copied from the libretro port's osd.h: the core's
 * renderers size their per-line sprite work from config.no_sprite_limit. */
#define HAVE_NO_SPRITE_LIMIT
#define MAX_SPRITES_PER_LINE 80
#define TMS_MAX_SPRITES_PER_LINE (config.no_sprite_limit ? MAX_SPRITES_PER_LINE : 4)
#define MODE4_MAX_SPRITES_PER_LINE (config.no_sprite_limit ? MAX_SPRITES_PER_LINE : 8)
#define MODE5_MAX_SPRITES_PER_LINE (config.no_sprite_limit ? MAX_SPRITES_PER_LINE : (bitmap.viewport.w >> 4))
#define MODE5_MAX_SPRITE_PIXELS (config.no_sprite_limit ? MAX_SPRITES_PER_LINE * 32 : max_sprite_pixels)

typedef struct
{
  int8_t device;
  uint8_t port;
  uint8_t padtype;
} t_input_config;

typedef struct
{
  char version[16];
  uint8_t hq_fm;
  uint8_t filter;
  uint8_t hq_psg;
  uint8_t ym2612;
  uint8_t ym2413;
  uint8_t mono;
  int16_t psg_preamp;
  int16_t fm_preamp;
  int16_t cdda_volume;
  int16_t pcm_volume;
  uint16_t lp_range;
  int16_t low_freq;
  int16_t high_freq;
  int16_t lg;
  int16_t mg;
  int16_t hg;
  uint8_t system;
  uint8_t region_detect;
  uint8_t master_clock;
  uint8_t vdp_mode;
  uint8_t force_dtack;
  uint8_t addr_error;
  uint8_t bios;
  uint8_t lock_on;
  uint8_t add_on;
  uint8_t overscan;
  uint8_t aspect_ratio;
  uint8_t ntsc;
  uint8_t lcd;
  uint8_t gg_extra;
  uint8_t left_border;
  uint8_t render;
  t_input_config input[MAX_INPUTS];
  uint8_t invert_mouse;
  uint8_t gun_cursor;
  uint32_t overclock;
  uint8_t no_sprite_limit;
  uint8_t enhanced_vscroll;
  uint8_t enhanced_vscroll_limit;
  uint8_t cd_latency;
} t_config;

/* The one config instance. Defined in Core/genesis_core.cpp and filled with
 * the defaults genesis_set_config_defaults() records there. */
extern t_config config;

/* Two core files (cart_hw/sram.c, cart_hw/sms_cart.c) call crc32() without
 * declaring it — upstream's frontends declare it for them (the libretro port
 * via its scrc32.h). Core/genesis_core.cpp DEFINES it with zlib's exact
 * algorithm and values, so no zlib header or symbol is needed for it. */
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);

/* Optional ROM files the core asks for by path: the Genesis boot ROM, the
 * Game Genie / Action Replay lock-on ROMs, and the Sega CD / SMS / GG BIOS
 * files. OGA ships NONE of them — Genesis cartridges boot without a boot ROM,
 * lock-on support is config.lock_on = 0, and Sega CD is out of scope — so
 * these are empty strings, load_archive() fails cleanly on them, and the core
 * proceeds without. They exist because the core references the symbols.
 * Defined in Core/genesis_core.cpp. */
extern char GG_ROM[256];
extern char AR_ROM[256];
extern char SK_ROM[256];
extern char SK_UPMEM[256];
extern char MD_BIOS[256];
extern char CD_BIOS_US[256];
extern char CD_BIOS_EU[256];
extern char CD_BIOS_JP[256];
extern char MS_BIOS_US[256];
extern char MS_BIOS_EU[256];
extern char MS_BIOS_JP[256];
extern char GG_BIOS[256];

/* The frontend's file loader: read a whole file into the buffer, return its
 * size, 0 when it cannot be read. The core uses it for the ROM itself (the
 * path genesis_load_rom hands to load_rom) and for the optional files above.
 * Implemented in Core/genesis_core.cpp as a plain uncompressed read: OGA's
 * picker hands the core real files, never archives. */
extern int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension);

/* Called by the core once per frame from its frame loops. OGA's input is
 * latched through genesis_set_button BEFORE the frame runs (the app's frame
 * model), so there is nothing to poll here. A macro, not a function, so no
 * frontend symbol is owed. */
#define osd_input_update() ((void)0)

#endif /* OGA_GPGX_OSD_H */
