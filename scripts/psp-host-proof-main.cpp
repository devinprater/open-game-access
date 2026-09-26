// psp-host-proof-main.cpp — host proof harness for the real PPSSPP core.
//
// Boots a PSP game image through the psp_* ABI in Core/psp_core.h, runs a
// fixed number of frames headlessly (software GPU, no window, no audio
// device), and prints PROOF lines the driver script asserts on. This file is
// SCAFFOLDING: it is never part of the iOS archive (nothing lists it) and
// exists only so scripts/psp-host-proof.sh can reproduce the September 2026
// proof (Dissidia 012, ULUS10437, 900/900 frames, live framebuffer) that the
// audited PPSSPP subset in scripts/core-sources.sh was pinned against.
#include <cstdio>
#include <cstdlib>

#include "psp_core.h"

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <game.cso|game.iso> <save-dir> <frames>\n", argv[0]);
    return 2;
  }
  const char *image = argv[1];
  const char *savedir = argv[2];
  const int frames = atoi(argv[3]);
  if (frames <= 0 || frames > 60 * 60 * 5) {
    fprintf(stderr, "refusing frame count %d\n", frames);
    return 2;
  }

  PspCore *core = psp_create();
  char code[16] = {0};
  if (!psp_load_rom(core, image, savedir, code)) {
    fprintf(stderr, "psp_load_rom failed: %s\n", psp_last_error(core));
    psp_destroy(core);
    return 1;
  }
  printf("PROOF: gameid=%s\n", code);
  if (!psp_start(core)) {
    fprintf(stderr, "psp_start failed: %s\n", psp_last_error(core));
    psp_destroy(core);
    return 1;
  }
  int ran = 0;
  while (ran < frames && psp_frame(core)) {
    ++ran;
  }
  printf("PROOF: frames=%d/%d completed=%llu\n", ran, frames,
         psp_frames_completed(core));

  // Nonzero pixels anywhere in the final framebuffer = the GPU actually drew.
  int w = 0, h = 0;
  long nonzero = 0;
  if (psp_framebuffer(core, &w, &h)) {
    const unsigned char *fb = psp_framebuffer_ptr(core);
    printf("PROOF: fb=%dx%d\n", w, h);
    for (int i = 0; i < w * h * 4; i += 4) {
      if (fb[i] | fb[i + 1] | fb[i + 2]) {
        if (++nonzero > 4096) break;
      }
    }
  }
  printf("PROOF: nonzero=%ld\n", nonzero);
  psp_stop(core);
  psp_destroy(core);
  printf("PROOF: shutdown-clean\n");
  return (ran == frames && nonzero > 0) ? 0 : 1;
}
