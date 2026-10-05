// zelda-shot.cpp — save the framebuffer as a PNG so the result is visible, not just numbers.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mesen_core.h"

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { fprintf(stderr, "usage: %s <rom> <out.ppm> [savedir] [boot_frames]\n", argv[0]); return 2; }
    int boot = (argc > 4) ? atoi(argv[4]) : 420;

    NesCore* c = nes_create();
    char code[16] = {0};
    if (!nes_load_rom(c, argv[1], argc > 3 ? argv[3] : ".", code)) {
        printf("FAIL: %s\n", nes_last_error(c)); return 1;
    }
    nes_start(c);
    for (int i = 0; i < boot; i++) nes_frame(c);

    int w = 0, h = 0;
    if (!nes_framebuffer(c, &w, &h)) { printf("no framebuffer\n"); return 1; }
    const uint8_t* px = nes_framebuffer_ptr(c);
    FILE* f = fopen(argv[2], "wb");
    if (!f) { printf("cannot write %s\n", argv[2]); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) { fputc(px[i*4], f); fputc(px[i*4+1], f); fputc(px[i*4+2], f); }
    fclose(f);
    printf("wrote %s (%dx%d, after %d frames, code=%s)\n", argv[2], w, h, boot, code);
    nes_stop(c); nes_destroy(c);
    return 0;
}
