// fb-verify.cpp — is the frame TILED (a bug) or a normal single screen (vision model wrong)?
//
// Quantitatively compare the quadrants. If the top-left quadrant equals the top-right, the image
// really is tiled x2. If it does not, the capture is a normal 256x240 frame and the "2x2 grid"
// description was a vision-model artifact -- a failure this project has recorded before: trust
// transcribed TEXT from a vision model, never its structural/identity claims.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pch.h"
#include "mesen_core.h"

static bool same(const uint8_t* px, int w, int x0, int y0, int x1, int y1, int bw, int bh) {
    long diff = 0, total = 0;
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int a = ((y0+y)*w + (x0+x))*4, b = ((y1+y)*w + (x1+x))*4;
            total++;
            if (px[a]!=px[b] || px[a+1]!=px[b+1] || px[a+2]!=px[b+2]) diff++;
        }
    printf("    differing pixels: %ld / %ld (%.1f%%)\n", diff, total, 100.0*diff/total);
    return diff == 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) return 2;
    int boot = (argc > 2) ? atoi(argv[2]) : 400;

    NesCore* c = nes_create();
    char code[16] = {0};
    if (!nes_load_rom(c, argv[1], ".", code)) { printf("FAIL\n"); return 1; }
    nes_start(c);
    for (int i = 0; i < boot; i++) nes_frame(c);

    int w = 0, h = 0;
    nes_framebuffer(c, &w, &h);
    const uint8_t* px = nes_framebuffer_ptr(c);
    printf("frame: %dx%d\n", w, h);

    printf("\n=== is the LEFT half identical to the RIGHT half? (would mean x2 tiling)\n");
    bool tileX = same(px, w, 0, 0, w/2, 0, w/2, h/2);

    printf("\n=== is the TOP half identical to the BOTTOM half? (would mean y2 tiling)\n");
    bool tileY = same(px, w, 0, 0, 0, h/2, w/2, h/2);

    printf("\n=== CONCLUSION\n");
    if (!tileX && !tileY)
        printf("  NOT tiled. This is a normal single NES frame.\n"
               "  The '2x2 grid' description came from the vision model, not the pixels.\n");
    else
        printf("  TILED (x2=%d y2=%d) -- a real bug in the framebuffer copy.\n", tileX, tileY);

    // How much of the frame is non-black, so "blank screen" can be ruled out too.
    long nonblack = 0;
    for (int i = 0; i < w*h; i++)
        if (px[i*4] || px[i*4+1] || px[i*4+2]) nonblack++;
    printf("  non-black pixels: %ld / %d (%.1f%%)\n", nonblack, w*h, 100.0*nonblack/(w*h));

    nes_stop(c); nes_destroy(c);
    return 0;
}
