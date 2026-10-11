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

static bool fieldLive(PspCore *c) {
  return psp_debug_read(c, LIVE_PTR, 4) == LIVE_PTR + 4u && citiesLive(c) > 0;
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
  printf("game id: %s\n", code);
  if (!psp_start(core)) {
    fprintf(stderr, "psp_start failed: %s\n", psp_last_error(core));
    return 1;
  }
  report(core, "after boot");

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
  static const int kScript[] = {
      PSP_BTN_CROSS, PSP_BTN_CROSS, PSP_BTN_DOWN,     PSP_BTN_CROSS,
      PSP_BTN_START, PSP_BTN_CROSS, PSP_BTN_UP,       PSP_BTN_CROSS,
      PSP_BTN_CROSS, PSP_BTN_TRIANGLE, PSP_BTN_RIGHT, PSP_BTN_CROSS,
      PSP_BTN_START, PSP_BTN_CROSS, PSP_BTN_LEFT,     PSP_BTN_CROSS};
  const int kScriptLen = (int)(sizeof(kScript) / sizeof(kScript[0]));
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
      if      (r < 45) btn = PSP_BTN_CROSS;      // the cutscene chain is the long part, so CROSS
      else if (r < 60) btn = PSP_BTN_START;      // heavy; the rest explores the cursor screens
      else if (r < 70) btn = PSP_BTN_DOWN;
      else if (r < 80) btn = PSP_BTN_UP;
      else if (r < 88) btn = PSP_BTN_RIGHT;
      else if (r < 94) btn = PSP_BTN_LEFT;
      else             btn = PSP_BTN_TRIANGLE;
    } else {
      btn = kScript[presses % kScriptLen];
    }
    const unsigned long h0 = frameHash(core);
    tap(core, btn, 6);
    idle(core, 40);
    presses++;
    if (frameHash(core) == h0) stuck++; else stuck = 0;
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
