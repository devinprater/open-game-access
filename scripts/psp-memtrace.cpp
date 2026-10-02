// psp-memtrace.cpp — MemCheck read trace for the Dissidia enemy-name task.
// Resumes a board savestate under the CLASSIC interpreter (whose memory ops
// fire debugger MemChecks), plants read watches on the live-located tooltip
// inputs (pool strings, catalog O record, species/strtab heads, the two
// table globals), then polls every frame across a hover transition. New
// (pc, addr, size) hits name the exact instructions that build the tooltip,
// settling short-vs-word reads and which table is really consulted — the two
// questions static analysis could not close.
//
// Usage:
//   psp-memtrace <cso> <savedir> <assets> <state.ppst> <outdir>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "psp_core.h"

static uint32_t R32(PspCore* c, uint32_t a)
{
    return psp_debug_read(c, a, 4);
}

// Chunked byte scan of [base, base+len) for needle. Returns VA or 0.
static uint32_t Scan(PspCore* c, uint32_t base, uint32_t len,
                     const uint8_t* needle, size_t nlen)
{
    std::vector<uint8_t> buf;
    buf.reserve(0x10000);
    for (uint32_t off = 0; off < len;) {
        uint32_t chunk = len - off > 0x10000 ? 0x10000 : len - off;
        buf.clear();
        for (uint32_t i = 0; i < chunk; i += 4) {
            uint32_t v = R32(c, base + off + i);
            buf.push_back((uint8_t)(v & 0xFF));
            buf.push_back((uint8_t)((v >> 8) & 0xFF));
            buf.push_back((uint8_t)((v >> 16) & 0xFF));
            buf.push_back((uint8_t)((v >> 24) & 0xFF));
        }
        for (size_t i = 0; i + nlen <= buf.size(); i++) {
            if (memcmp(&buf[i], needle, nlen) == 0)
                return base + off + (uint32_t)i;
        }
        off += chunk;
    }
    return 0;
}

static void Run(PspCore* c, int n)
{
    for (int i = 0; i < n; i++)
        if (!psp_frame(c)) break;
}

static bool Shot(PspCore* c, const char* path)
{
    int w = 0, h = 0;
    if (!psp_framebuffer(c, &w, &h)) return false;
    const uint8_t* px = psp_framebuffer_ptr(c);
    if (!px || w <= 0 || h <= 0) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(px[i * 4], f);
        fputc(px[i * 4 + 1], f);
        fputc(px[i * 4 + 2], f);
    }
    fclose(f);
    return true;
}

static void QuietLog(const char* msg, void* ud)
{
    (void)msg;
    (void)ud;
}

struct Watch {
    const char* name;
    uint32_t start, end;
    bool log;  // per-hit (pc, addr, size) lines instead of silent counting
    uint32_t lastHits = 0;
    uint32_t lastPc = 0;
    long events = 0;
};

int main(int argc, char** argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: %s <cso> <savedir> <assets> <state> <out>\n",
                argv[0]);
        return 2;
    }
    PspCore* c = psp_create();
    psp_set_log_callback(c, QuietLog, nullptr);
    psp_set_asset_dir(c, argv[3]);
    psp_set_classic_interpreter(c);
    char code[16] = {0};
    int rc = 0;
    if (!psp_load_rom(c, argv[1], argv[2], code)) {
        fprintf(stderr, "load failed: %s\n", psp_last_error(c));
        rc = 1;
    } else if (!psp_start(c)) {
        fprintf(stderr, "start failed: %s\n", psp_last_error(c));
        rc = 1;
    } else if (!psp_load_state(c, argv[4])) {
        fprintf(stderr, "state failed: %s\n", psp_last_error(c));
        rc = 1;
    } else {
        Run(c, 30);  // let the restored state settle
        // Live-locate the pool strings (boot-proof: scan, don't hardcode).
        const uint8_t fh[] = {'F', 0, 'a', 0, 'l', 0, 's', 0, 'e', 0,
                              ' ', 0, 'H', 0, 'e', 0, 'r', 0, 'o', 0};
        uint32_t heapFH = Scan(c, 0x09C00000u, 0x00030000u, fh, sizeof(fh));
        uint32_t statFH = Scan(c, 0x09B00000u, 0x00080000u, fh, sizeof(fh));
        printf("heapFH=%08x statFH=%08x\n", heapFH, statFH);
        const uint8_t dk[] = {'D', 0, 'e', 0, 'l', 0, 'u', 0, 's', 0,
                              'o', 0, 'r', 0, 'y', 0, ' ', 0, 'K', 0};
        uint32_t heapDK = Scan(c, 0x09C00000u, 0x00030000u, dk, sizeof(dk));
        uint32_t statDK = Scan(c, 0x09B00000u, 0x00080000u, dk, sizeof(dk));
        printf("heapDK=%08x statDK=%08x\n", heapDK, statDK);
        // Catalog walk to the hovered marker's O (manager slot is static).
        uint32_t M = R32(c, 0x08B98940u);
        uint32_t P = M ? R32(c, M + 0x118u) : 0;
        uint32_t B = P ? R32(c, P + 4) : 0;
        uint32_t Ct = B ? R32(c, B + 12) : 0;
        uint32_t C2 = Ct ? R32(c, Ct) : 0;
        uint32_t K = C2 ? R32(c, C2 + 4) : 0;
        uint32_t CB = C2 ? R32(c, C2 + 8) : 0;
        uint32_t D = B ? R32(c, B + 16) : 0;
        int hx = D ? (int)psp_debug_read(c, D + 0x194, 1) : -1;
        int hy = D ? (int)psp_debug_read(c, D + 0x195, 1) : -1;
        printf("M=%08x P=%08x B=%08x Ct=%08x C2=%08x hx=%d hy=%d\n",
               M, P, B, Ct, C2, hx, hy);
        uint32_t T = B ? R32(c, B + 0x0Cu) : 0;
        uint32_t N = T ? R32(c, T + 4) : 0;
        printf("T=%08x N=%08x\n", T, N);
        // O for EVERY active marker: the hovered one lights up when the
        // tooltip builds (cursor-tile matching is unreliable pre-hover).
        uint32_t hoverOs[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        int nHover = 0;
        if (N && K && CB) {
            for (int i = 0; i < 32 && nHover < 8; i++) {
                uint32_t e = N + (uint32_t)i * 0x10u;
                uint32_t key = psp_debug_read(c, e, 1);
                int mx = (int)(int8_t)psp_debug_read(c, e + 2, 1);
                int my = (int)(int8_t)psp_debug_read(c, e + 3, 1);
                uint32_t fl = psp_debug_read(c, e + 0x0C, 1);
                bool active = !(mx == 0 && my == 0 && fl == 0 && key == 0);
                if (active) {
                    uint32_t off = R32(c, K + key * 4);
                    hoverOs[nHover++] = CB + off;
                }
            }
        }
        printf("nHover=%d\n", nHover);
        uint32_t specDesc = R32(c, 0x08B99578u);
        uint32_t specData = specDesc ? R32(c, specDesc) : 0;
        uint32_t strDesc = R32(c, 0x08B98218u);
        uint32_t strData = strDesc ? R32(c, strDesc) : 0;
        printf("specData=%08x strData=%08x\n", specData, strData);

        Watch ws[16];
        int nW = 0;
        if (heapFH) {
            ws[nW].name = "heapFH";
            ws[nW].start = heapFH - 16;
            ws[nW].end = heapFH + 40;
            ws[nW].log = true;
            nW++;
        }
        if (statFH) {
            ws[nW].name = "statFH";
            ws[nW].start = statFH - 16;
            ws[nW].end = statFH + 40;
            ws[nW].log = true;
            nW++;
        }
        if (heapDK) {
            ws[nW].name = "heapDK";
            ws[nW].start = heapDK - 16;
            ws[nW].end = heapDK + 40;
            ws[nW].log = true;
            nW++;
        }
        if (statDK) {
            ws[nW].name = "statDK";
            ws[nW].start = statDK - 16;
            ws[nW].end = statDK + 40;
            ws[nW].log = true;
            nW++;
        }
        for (int i = 0; i < nHover; i++) {
            char* nm = (char*)malloc(8);
            snprintf(nm, 8, "hoverO%d", i);
            ws[nW].name = nm;
            ws[nW].start = hoverOs[i];
            ws[nW].end = hoverOs[i] + 0x40;
            ws[nW].log = true;
            nW++;
        }
        if (specData) {
            ws[nW].name = "specFull";
            ws[nW].start = specData + 8;
            ws[nW].end = specData + 8 + 0x8500;  // 809 recs x 42
            ws[nW].log = true;
            nW++;
        }
        if (strData) {
            ws[nW].name = "strHead";
            ws[nW].start = strData + 8;
            ws[nW].end = strData + 8 + 0x100;
            ws[nW].log = false;
            nW++;
        }
        ws[nW].name = "globals";
        ws[nW].start = 0x08B98210u;
        ws[nW].end = 0x08B99590u;
        ws[nW].log = false;
        nW++;
        for (int i = 0; i < nW; i++) {
            if (!ws[i].start) continue;
            ws[i].start &= ~3u;
            ws[i].end = (ws[i].end + 3) & ~3u;
            if (ws[i].log)
                psp_watch_read_log(c, ws[i].start, ws[i].end);
            else
                psp_watch_read(c, ws[i].start, ws[i].end);
            printf("watch %s [%08x,%08x)\n", ws[i].name, ws[i].start,
                   ws[i].end);
        }
        char sp[512];
        snprintf(sp, sizeof(sp), "%s/pre.ppm", argv[5]);
        printf("pre shot=%d\n", (int)Shot(c, sp));

        long long frame = 0;
        long printed = 0;
        // Phase 0: baseline, tooltip closed (15 frames).
        for (int f = 0; f < 15; f++) {
            Run(c, 1);
            frame++;
            for (int i = 0; i < nW; i++) {
                if (!ws[i].start) continue;
                uint32_t h, pc, ad;
                int sz;
                if (!psp_watch_poll(c, ws[i].start, ws[i].end, &h, &pc,
                                    &ad, &sz))
                    continue;
                if (h != ws[i].lastHits) {
                    ws[i].lastHits = h;
                    ws[i].events++;
                    if (printed < 3000) {
                        printf("f=%lld base %s pc=%08x addr=%08x sz=%d hits=%u\n",
                               frame, ws[i].name, pc, ad, sz, h);
                        printed++;
                    }
                }
            }
        }
        // Phase 1: RRR taps to hover the enemy (tooltip opens), then settle.
        const int taps[] = {3, 3, 3};  // PSP_BTN_RIGHT
        for (int t = 0; t < 3; t++) {
            psp_set_button(c, taps[t], true);
            Run(c, 2);
            psp_set_button(c, taps[t], false);
            Run(c, 150);
            frame += 152;
        }
        for (int f = 0; f < 20; f++) {
            Run(c, 1);
            frame++;
            for (int i = 0; i < nW; i++) {
                if (!ws[i].start) continue;
                uint32_t h, pc, ad;
                int sz;
                if (!psp_watch_poll(c, ws[i].start, ws[i].end, &h, &pc,
                                    &ad, &sz))
                    continue;
                if (h != ws[i].lastHits || pc != ws[i].lastPc) {
                    ws[i].lastHits = h;
                    ws[i].lastPc = pc;
                    ws[i].events++;
                    if (printed < 3000) {
                        printf("f=%lld post %s pc=%08x addr=%08x sz=%d hits=%u\n",
                               frame, ws[i].name, pc, ad, sz, h);
                        printed++;
                    }
                }
            }
        }
        snprintf(sp, sizeof(sp), "%s/post.ppm", argv[5]);
        printf("post shot=%d\n", (int)Shot(c, sp));
        snprintf(sp, sizeof(sp), "%s/final.ppst", argv[5]);
        printf("final state=%d\n", (int)psp_save_state(c, sp));
        for (int i = 0; i < nW; i++)
            if (ws[i].start)
                printf("summary %s events=%ld\n", ws[i].name, ws[i].events);
    }
    psp_stop(c);
    psp_destroy(c);
    return rc;
}
