// Boot a PSP image headless, run N frames, print live words at given addresses.
#include <cstdio>
#include <cstdlib>
#include "psp_core.h"
int main(int argc, char **argv) {
  if (argc < 5) { fprintf(stderr, "usage: %s img savedir frames addr...\n", argv[0]); return 2; }
  PspCore *c = psp_create(); char code[16] = {0};
  if (!psp_load_rom(c, argv[1], argv[2], code)) { fprintf(stderr, "load failed: %s\n", psp_last_error(c)); return 1; }
  printf("gameid=%s\n", code);
  if (!psp_start(c)) { fprintf(stderr, "start failed\n"); return 1; }
  int frames = atoi(argv[3]), ran = 0;
  while (ran < frames && psp_frame(c)) ++ran;
  printf("frames=%d\n", ran);
  for (int i = 4; i < argc; i++) {
    uint32_t a = (uint32_t)strtoul(argv[i], 0, 16);
    printf("%08x: %u %u\n", a, psp_debug_read(c, a, 4), psp_debug_read(c, a + 4, 4));
  }
  psp_stop(c); psp_destroy(c); return 0;
}
