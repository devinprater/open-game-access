// cue-field-drive-main.cpp — drive DBZ: Shin Budokai Another Road into its STORY MODE field and
// write a savestate THIS build wrote, so the cue-capture harness can load it.
//
// WHY THIS EXISTS, in two parts.
//
// 1. INPUT. Core/pokecore.cpp creates the PSP core but never gives it a button path: poke_set_button
//    only reaches a backend ops table, and this build populates that table for NDS alone. So every
//    press made through poke_* is written into a field nothing reads -- the game sits in its own
//    attract loop ignoring all twelve buttons. Core/psp_core.h is the ABI that DOES carry input, and
//    scripts/psp-host-proof.sh is the only build on this host that links it cleanly.
//
// 2. A SAVESTATE IS BOUND TO THE BUILD THAT WROTE IT. The field state on this host came from a
//    desktop PPSSPP four months older than the pinned tree, and dies on a marker mismatch deep in
//    the stream. A state written HERE loads HERE.
//
// RAM reads go through psp_debug_read, the same accessor the adapter Host path uses, so the drive
// verifies where it is instead of counting presses. Addresses are the live-verified ones in
// Core/dbzar_adapter.cpp (see its header: the static immediates are relocation addends, not bases).
//
// SCAFFOLDING: like psp-host-proof-main.cpp, this file is never part of the archive.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "psp_core.h"

// ---- the field's own state, as Core/dbzar_adapter.cpp reads it -------------------------------
static const uint32_t LIVE_PTR    = 0x089B02A4u;   // == LIVE_PTR + 4 ONLY in the field
static const uint32_t AR_MODE     = 0x089B51D4u;
static const uint32_t MENU_COUNT  = 0x08BA1D14u;
static const uint32_t MENU_CURSOR = 0x08BA1D18u;
static const uint32_t CITY_BASE   = 0x08A876B0u;
static const uint32_t CITY_STRIDE = 0x70u;
static const int      CITY_COUNT  = 5;
static const uint32_t CITY_MAX    = 0x24u;        // i32 max health; capacity, not id

// A city slot is IN PLAY when it has capacity. An empty slot carries id 0xFFFF and max 0, so
// `id != 0` is exactly inverted for the first city on every map.
static int citiesLive(PspCore *c) {
  int n = 0;
  for (int i = 0; i < CITY_COUNT; i++) {
    if (psp_debug_read(c, CITY_BASE + (uint32_t) i * CITY_STRIDE + CITY_MAX, 4) > 0) n++;
  }
  return n;
}

// ⛔ THE ADAPTER'S GATES, EXACTLY -- NOT A SECOND OPINION ABOUT THE SAME PROPERTY.
// Core/dbzar_adapter.cpp's FieldIsLive() is AR_MODE == 1 AND this pointer nonzero:
const static uint32_t FIELD_LIVE = 0x089B03E4u;   // a pointer only while the field tasks are resident
// MEASURED: the drive's earlier test (the pointer at 0x089B02A4 reading 0x089B02A8, plus a city with
// capacity) was satisfied at a moment when 0x089B03E4 read 0x00000000 -- cities populated, field
// tasks not. The state written there is one the adapter refuses, and the capture harness then
// reported "adapter not ready" about a state the PSP level reads as field data. One oracle for one
// property, and it must be the one the thing being fed actually uses.
static bool fieldLive(PspCore *c) {
  return psp_debug_read(c, AR_MODE, 1) == 1 &&
         psp_debug_read(c, FIELD_LIVE, 4) != 0 &&
         citiesLive(c) > 0;
}

// Emulated frames, not seconds. The core paces itself, so wall clock says nothing about how far
// the game has been driven; every budget and every log line here counts FRAMES.
static long g_frames = 0;

static void idle(PspCore *c, int frames) {
  for (int i = 0; i < frames; i++) { if (!psp_frame(c)) return; g_frames++; }
}

// A press must SPAN emulated frames: the console samples the pad once per frame, so a hold that
// begins and ends between two frames never exists and the game reports "START does nothing".
static void tap(PspCore *c, int btn, int hold) {
  psp_set_button(c, btn, true);
  idle(c, hold < 2 ? 2 : hold);
  psp_set_button(c, btn, false);
  idle(c, 4);
}

static void report(PspCore *c, const char *where) {
  printf("  [%-16s] arMode=%u cities=%d cursor=%u/%u live=%u\n", where,
         psp_debug_read(c, AR_MODE, 1), citiesLive(c),
         psp_debug_read(c, MENU_CURSOR, 4), psp_debug_read(c, MENU_COUNT, 4),
         psp_debug_read(c, LIVE_PTR, 4));
  fflush(stdout);
}

// Fingerprint of the visible frame. With a WORKING input path this is finally a usable instrument:
// the question "did my press do anything" is answered by whether the game LEAVES its own loop, and
// a self-running attract sequence keeps producing new images no matter what you press.
static unsigned long frameHash(PspCore *c) {
  int w = 0, h = 0;
  if (!psp_framebuffer(c, &w, &h) || w <= 0) return 0;
  const unsigned char *px = psp_framebuffer_ptr(c);
  if (!px) return 0;
  unsigned long hsh = 1469598103934665603UL;
  const long n = (long) w * h * 4;
  for (long i = 0; i < n; i += 331) { hsh ^= px[i]; hsh *= 1099511628211UL; }
  return hsh;
}

// Dump the visible frame as a PPM. Cheap, and it is the only instrument that works when the
// reader has nothing to say: outside the field the DBZ reader answers nothing at all, so speech
// cannot tell a menu from a hang. scripts/ppm2png.py turns these into images that can be looked at.
// ⛔ The FILL call must come first: psp_framebuffer stages the pixels and the pointer accessor
// returns them, so calling the pointer first returns null and this writes nothing.
static void dumpFrame(PspCore *c, const char *dir, long idx) {
  int w = 0, h = 0;
  if (!psp_framebuffer(c, &w, &h) || w <= 0 || h <= 0) return;
  const unsigned char *px = psp_framebuffer_ptr(c);
  if (!px) { printf("  dump %ld: fill ok but no pixels\n", idx); return; }
  char path[512];
  snprintf(path, sizeof path, "%s/f%06ld.ppm", dir, idx);
  FILE *f = fopen(path, "wb");
  if (!f) { printf("  dump %ld: cannot write %s\n", idx, path); return; }
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  long lit = 0;
  for (int y = 0; y < h; y++) {
    const unsigned char *s = px + (size_t) y * w * 4;
    for (int x = 0; x < w; x++) {
      unsigned char rgb[3] = {s[x * 4 + 0], s[x * 4 + 1], s[x * 4 + 2]};
      fwrite(rgb, 1, 3, f);
      if (rgb[0] > 24 || rgb[1] > 24 || rgb[2] > 24) lit++;
    }
  }
  fclose(f);
  printf("  dumped %s (%dx%d, %ld lit px)\n", path, w, h, lit);
  fflush(stdout);
}

// Which button does ANYTHING? Count distinct screens over a long window, one button per window.
// A running intro/demo keeps changing; a screen that accepts input settles, or moves the game on.
static int probeButtons(PspCore *c) {
  static const char *names[PSP_BTN_COUNT] = {
      "Select", "Start", "Up", "Right", "Down", "Left",
      "Triangle", "Circle", "Cross", "Square", "L", "R"};
  printf("probe: no-input control first, then one window per button\n");
  unsigned long seen[128];
  int base = 0;
  for (int k = 0; k < 3600; k++) {
    if (!psp_frame(c)) return 1;
    if (k % 200 == 0) {
      unsigned long h = frameHash(c);
      bool dup = false;
      for (int j = 0; j < base; j++) if (seen[j] == h) dup = true;
      if (!dup && base < 128) seen[base++] = h;
    }
  }
  printf("  %-12s %d distinct  (CONTROL: no press at all)\n", "(none)", base);
  fflush(stdout);

  for (int b = 0; b < PSP_BTN_COUNT; b++) {
    int nseen = 0;
    unsigned long sn[128];
    tap(c, b, 8);
    for (int k = 0; k < 3600; k++) {
      if (!psp_frame(c)) return 1;
      if (k % 200 == 0) {
        unsigned long h = frameHash(c);
        bool dup = false;
        for (int j = 0; j < nseen; j++) if (sn[j] == h) dup = true;
        if (!dup && nseen < 128) sn[nseen++] = h;
      }
    }
    printf("  %-12s %d distinct\n", names[b], nseen);
    fflush(stdout);
  }
  report(c, "after probe");
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <game.iso> <save-dir> <out.ppst>\n", argv[0]);
    return 2;
  }
  const char *image = argv[1];
  const char *savedir = argv[2];
  const char *outstate = argv[3];

  PspCore *core = psp_create();
  const char *assets = getenv("PPSSPP_ASSETS");
  if (assets && *assets) psp_set_asset_dir(core, assets);

  char code[16] = {0};
  if (!psp_load_rom(core, image, savedir, code)) {
    fprintf(stderr, "psp_load_rom failed: %s\n", psp_last_error(core));
    return 1;
  }
  setvbuf(stdout, nullptr, _IOLBF, 0);   // ⛔ line buffered: a run that HANGS at exit must still
  printf("game id: %s\n", code);         // leave its log behind, and the one that did not ran 44 h.
  if (!psp_start(core)) {
    fprintf(stderr, "psp_start failed: %s\n", psp_last_error(core));
    return 1;
  }
  report(core, "after boot");

  // ---- DBZ_LOAD=<state>: skip the drive, load a savestate, and read the field's own addresses.
  {
    const char *load = getenv("DBZ_LOAD");
    if (load && *load) {
      printf("loading state %s\n", load);
      fflush(stdout);
      if (!psp_load_state(core, load)) {
        fprintf(stderr, "psp_load_state FAILED: %s\n", psp_last_error(core));
        psp_stop(core);
        psp_destroy(core);
        return 1;
      }
      for (int i = 0; i < 10; i++) idle(core, 1);   // a frame must run: Ready() is read DURING one
      const bool live = fieldLive(core);
      printf("after load: arMode=%u cities=%d livePtr=%u fieldLiveWord=0x%08X\n",
             psp_debug_read(core, AR_MODE, 1), citiesLive(core),
             psp_debug_read(core, LIVE_PTR, 4),
             psp_debug_read(core, FIELD_LIVE, 4));
      for (int i = 0; i < CITY_COUNT; i++) {
        const uint32_t b = CITY_BASE + (uint32_t) i * CITY_STRIDE;
        printf("  city[%d] id=0x%04x cur=%u max=%u\n", i,
               psp_debug_read(core, b, 2), psp_debug_read(core, b + 0x20u, 4),
               psp_debug_read(core, b + CITY_MAX, 4));
      }
      // The ADAPTER's own gates, not this tool's: FIELD_LIVE = 0x089B03E4 (a pointer while the
      // field tasks are resident; 0 everywhere else) and the mode byte 0x089B51D4.
      printf("adapter's own gates: AR_MODE=%u FIELD_LIVE=0x%08X\n",
             psp_debug_read(core, 0x089B51D4u, 1), psp_debug_read(core, 0x089B03E4u, 4));
      printf("fieldLive=%d\n", live ? 1 : 0);
      fflush(stdout);
      psp_stop(core);
      psp_destroy(core);
      return live ? 0 : 1;
    }
  }

  if (getenv("DBZ_PROBE")) {
    idle(core, 9000);                 // let the game reach whatever it does on its own
    report(core, "settled");
    const int rc = probeButtons(core);
    psp_stop(core);
    psp_destroy(core);
    return rc;
  }

  // ---- A. leave the title. The proven drive opens with CROSS and nothing before it; a long START
  // spam here skips the title AND the menu behind it, which is how a blind drive ends up nowhere.
  tap(core, PSP_BTN_CROSS, 8);
  idle(core, 210);
  report(core, "after title");

  // ---- B. the main menu. STORY MODE is the FIRST row and the cursor does not start there, so a
  // blind CROSS lands in ARCADE instead. Walk the cursor to row 0 and CONFIRM the register moved,
  // retrying, rather than counting presses -- a press landing mid-tween is swallowed.
  for (int tries = 0; tries < 20; tries++) {
    const uint32_t count = psp_debug_read(core, MENU_COUNT, 4);
    const uint32_t cur = psp_debug_read(core, MENU_CURSOR, 4);
    if (count == 0) {
      // Not at a menu yet: nudge the game forward and look again.
      tap(core, PSP_BTN_CROSS, 8);
      idle(core, 180);
      continue;
    }
    if (cur == 0) { printf("menu up: %u rows, cursor at row 0 (STORY MODE)\n", count); break; }
    tap(core, PSP_BTN_UP, 6);
    idle(core, 42);
  }
  report(core, "menu settled");

  // ---- C. confirm STORY MODE and carry the story chain (cutscene -> character select -> Team Edit).
  tap(core, PSP_BTN_CROSS, 8);
  idle(core, 240);

  // ⛔ MEASURED TWICE, AND THE SECOND TIME IS WHY THIS IS A SCRIPT. The route is
  // cutscenes -> CHARACTER SELECT -> TEAM EDIT -> the field (docs/research/dbzar-field-mode.md).
  // But a loop of CROSS with a START every fifth round ran about 300 rounds (~29 minutes,
  // cue-ran2/drive2.log) and never left the chain: those screens want a CURSOR MOVE and a confirm,
  // and pressing one button through a screen that wants another goes nowhere -- the same lesson the
  // frame-hash probe above exists for.
  // ⛔ AND NOTHING HERE CAN SAY WHICH SCREEN IT IS ON: the entity array reads `team != -1` on every
  // screen of the mode, and the menu words read fill-pattern garbage outside a menu (0x09090909).
  // So the arrival test is the ADAPTER'S OWN GATE, and the log records a screen hash per press so a
  // failed run says WHERE it stopped rather than only that it did not arrive.
  // ⛔ TWO PHASES, CHOSEN BY THE RAM THE ADAPTER ITSELF READS. MEASURED: with CIRCLE in one
  // rotation, the drive clears the "Save completed" dialog and reaches Another Road inside 60k
  // frames (arMode=1, live pointer correct) -- and then stops, cities=0, because CIRCLE is also
  // BACK, and pressing it every sixteen presses bounces the drive out of the very menus it needs
  // to walk. So: CIRCLE while the mode flag is clear, and never once it is set.
  static const int kEnterMode[] = {
      PSP_BTN_CIRCLE, PSP_BTN_CROSS, PSP_BTN_CROSS, PSP_BTN_START, PSP_BTN_CROSS};
  static const int kInMode[] = {
      PSP_BTN_CROSS, PSP_BTN_CROSS, PSP_BTN_DOWN,     PSP_BTN_CROSS,
      PSP_BTN_RIGHT, PSP_BTN_CROSS, PSP_BTN_UP,       PSP_BTN_CROSS,
      PSP_BTN_LEFT,  PSP_BTN_CROSS, PSP_BTN_START,    PSP_BTN_CROSS,
      PSP_BTN_TRIANGLE, PSP_BTN_CROSS};
  const int kEnterLen = (int)(sizeof(kEnterMode) / sizeof(kEnterMode[0]));
  const int kInLen    = (int)(sizeof(kInMode) / sizeof(kInMode[0]));
  const bool fuzz = getenv("DBZ_FUZZ") != nullptr;
  const long maxFrames = getenv("DBZ_MAXFRAMES") ? atol(getenv("DBZ_MAXFRAMES")) : 60000;
  unsigned long lcg = 20261011UL;          // deterministic on purpose: a rerun repeats the search
  long presses = 0;
  int stuck = 0;
  printf("chain drive: %s, budget %ld emulated frames\n",
         fuzz ? "random (weighted to CROSS)" : "fixed script", maxFrames);

  while (!fieldLive(core) && g_frames < maxFrames) {
    int btn;
    if (fuzz) {
      lcg = lcg * 6364136223846793005UL + 1442695040888963407UL;
      const unsigned r = (unsigned)((lcg >> 33) % 100u);
      if      (r < 40) btn = PSP_BTN_CROSS;      // the cutscene chain is the long part, so CROSS
      else if (r < 55) btn = PSP_BTN_START;      // heavy; the rest explores the cursor screens
      else if (r < 62) btn = PSP_BTN_CIRCLE;     // ⛔ MEASURED: this is the one that mattered
      else if (r < 70) btn = PSP_BTN_DOWN;
      else if (r < 78) btn = PSP_BTN_UP;
      else if (r < 86) btn = PSP_BTN_RIGHT;
      else if (r < 92) btn = PSP_BTN_LEFT;
      else             btn = PSP_BTN_TRIANGLE;
    } else if (psp_debug_read(core, AR_MODE, 1) == 0) {
      btn = kEnterMode[presses % kEnterLen];          // dialogs and the way into the mode
    } else {
      btn = kInMode[presses % kInLen];                // the story chain, without CIRCLE
    }
    const unsigned long h0 = frameHash(core);
    tap(core, btn, 6);
    idle(core, 40);
    presses++;
    if (frameHash(core) == h0) {
      stuck++;
      // ⛔ THE SCREEN IS THE ONE THING THE RAM CANNOT DESCRIBE HERE. Both previous attempts ended
      // on a frozen frame with no picture of it: dump it once, on the first declaration of stuck,
      // so a failed run leaves evidence a person can look at instead of another forty-minute rerun.
      if (stuck == 7) {
        // ⛔ A SCREEN THAT IGNORES EVERY PRESS IS USUALLY A DIALOG WAITING FOR ITS OWN BUTTON.
        // MEASURED: both earlier attempts died on the game's "Save completed" dialog, whose only
        // control is "○ Back" -- a button neither run ever pressed. So: dump the frame for the
        // record, then press CIRCLE (PSP's back/cancel) and START, and give the game room to move.
        const char *dir = getenv("DBZ_FRAMES");
        if (dir && *dir) dumpFrame(core, dir, g_frames);
        printf("  [frame %ld] stuck -- trying CIRCLE then START to dismiss a dialog\n", g_frames);
        fflush(stdout);
        tap(core, PSP_BTN_CIRCLE, 8); idle(core, 90);
        tap(core, PSP_BTN_START, 8);  idle(core, 90);
        tap(core, PSP_BTN_CIRCLE, 8); idle(core, 90);
        stuck = 0;
      }
    } else {
      stuck = 0;
    }
    if (presses % 20 == 0 || fieldLive(core)) {
      printf("  press %4ld  frame %6ld  %sscreen %lu  arMode=%u live=%u cities=%d\n",
             presses, g_frames, stuck > 6 ? "STUCK " : "", frameHash(core),
             psp_debug_read(core, AR_MODE, 1), psp_debug_read(core, LIVE_PTR, 4), citiesLive(core));
      fflush(stdout);
    }
  }
  printf("chain: %ld presses, %ld frames -> %s\n", presses, g_frames,
         fieldLive(core) ? "FIELD LIVE" : "NOT in the field");

  report(core, "end");
  if (!fieldLive(core)) {
    fprintf(stderr, "never reached the field\n");
    psp_stop(core);
    psp_destroy(core);
    return 1;
  }
  printf("FIELD LIVE: %d cities\n", citiesLive(core));
  if (!psp_save_state(core, outstate)) {
    fprintf(stderr, "save_state failed: %s\n", psp_last_error(core));
    psp_stop(core);
    psp_destroy(core);
    return 1;
  }
  printf("wrote state %s\n", outstate);
  psp_stop(core);
  psp_destroy(core);
  return 0;
}
