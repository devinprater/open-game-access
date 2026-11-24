// android-gb-audio-test.cpp -- host proof for the Game Boy audio path.
//
// Compiles Core/gb_audio_queue.h, the exact header the shared Core's Game Boy
// host (Core/gba_core.cpp) stages its audio through on Android and iOS alike,
// and measures what it does to real sample streams. No phone can be
// driven from CI, so this is the part of "Game Boy games make sound on Android"
// that a host can prove: the console's 131072 Hz stream comes out at 32768 Hz,
// at the right length, without clicks at frame boundaries, without the square
// waves' harmonics folding back as whine, bounded in latency, and gated by the
// Game sound switch.
//
// Run through scripts/android-gb-audio-test.sh, which also mutates the header and
// requires each mutant to fail here.
#include "gb_audio_queue.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using oga::GbAudioQueue;
using oga::GbAudioResampler;
using oga::kGbAppAudioRate;

static int failures = 0;
static int passes = 0;

static void check(bool cond, const char* what)
{
    if (cond) { passes++; std::printf("  ok   %s\n", what); }
    else { failures++; std::printf("  FAIL %s\n", what); }
}

static const unsigned kGbRate = 131072;          // _GBCoreAudioSampleRate
static const double kPi = 3.14159265358979323846;

// A stereo tone at `rate`: left = sine at `hz`, right = the same, half amplitude.
static std::vector<int16_t> tone(double hz, unsigned rate, size_t frames, double amp)
{
    std::vector<int16_t> v(frames * 2);
    for (size_t i = 0; i < frames; i++)
    {
        double s = std::sin(2.0 * kPi * hz * (double) i / (double) rate) * amp;
        v[i * 2] = (int16_t) std::lround(s);
        v[i * 2 + 1] = (int16_t) std::lround(s * 0.5);
    }
    return v;
}

// Goertzel magnitude of channel `ch` at `hz`, normalised so a full-scale sine of
// amplitude A reads about A.
static double goertzel(const std::vector<int16_t>& v, int ch, double hz, unsigned rate)
{
    size_t n = v.size() / 2;
    double w = 2.0 * kPi * hz / (double) rate, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++)
    {
        double s0 = (double) v[i * 2 + ch] + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / (double) n;
}

// Feed `in` through a fresh resampler in chunks of the given sizes (cycled).
static std::vector<int16_t> chunked(const std::vector<int16_t>& in, unsigned rate,
                                    const std::vector<size_t>& sizes)
{
    GbAudioResampler r;
    std::vector<int16_t> out;
    size_t frames = in.size() / 2, pos = 0, k = 0;
    while (pos < frames)
    {
        size_t n = sizes[k++ % sizes.size()];
        if (n > frames - pos) n = frames - pos;
        r.process(in.data() + pos * 2, n, rate, out);
        pos += n;
    }
    return out;
}

int main()
{
    // One second of Game Boy audio, delivered the way the frame thread delivers
    // it: ~2194 frames a video frame, never a whole number of output frames.
    const size_t oneSecond = kGbRate;
    std::vector<int16_t> sine = tone(1000.0, kGbRate, oneSecond, 12000.0);

    std::printf("== rate and length\n");
    std::vector<int16_t> once;
    GbAudioResampler r1;
    size_t made = r1.process(sine.data(), oneSecond, kGbRate, once);
    check(made == once.size() / 2, "process() reports the frames it appended");
    check(std::labs((long) made - (long) kGbAppAudioRate) <= 1,
          "one second at 131072 Hz becomes one second at 32768 Hz (+-1 frame)");

    std::printf("== frame boundaries do not click\n");
    std::vector<int16_t> parts = chunked(sine, kGbRate, {2194, 2195, 2194, 2193, 7, 1, 4096});
    bool same = parts.size() == once.size();
    int worst = 0;
    for (size_t i = 0; same && i < parts.size(); i++)
        worst = std::max(worst, std::abs((int) parts[i] - (int) once[i]));
    check(same && worst <= 1,
          "fed a frame at a time, the output matches one continuous pass (phase carries)");
    {
        // A constant level, chunked at awkward sizes: any phase restart or
        // mis-weighted window edge shows up as a value that is not the level.
        std::vector<int16_t> dc(oneSecond * 2, 1000);
        std::vector<int16_t> out = chunked(dc, kGbRate, {2194, 3, 2195, 1, 999});
        bool flat = !out.empty();
        for (int16_t s : out) if (s != 1000) { flat = false; break; }
        check(flat, "a constant level stays exactly constant across chunk edges");
    }

    std::printf("== the music survives\n");
    double inAmp = goertzel(sine, 0, 1000.0, kGbRate);
    double outAmp = goertzel(once, 0, 1000.0, kGbAppAudioRate);
    double outAmpR = goertzel(once, 1, 1000.0, kGbAppAudioRate);
    std::printf("       1 kHz in %.0f, out L %.0f, out R %.0f\n", inAmp, outAmp, outAmpR);
    check(outAmp > 0.95 * inAmp && outAmp < 1.02 * inAmp, "a 1 kHz tone keeps its level (within 5%)");
    check(std::fabs(outAmpR - 0.5 * outAmp) < 0.03 * outAmp, "left and right stay separate (stereo kept)");

    std::printf("== square-wave harmonics do not fold back\n");
    {
        // 30 kHz is above the output's 16384 Hz limit. Picking or interpolating
        // folds it to 32768 - 30000 = 2768 Hz at nearly full level; the
        // averaging window must keep that alias small.
        std::vector<int16_t> hi = tone(30000.0, kGbRate, oneSecond, 12000.0);
        std::vector<int16_t> out;
        GbAudioResampler r;
        r.process(hi.data(), oneSecond, kGbRate, out);
        double alias = goertzel(out, 0, 32768.0 - 30000.0, kGbAppAudioRate);
        std::printf("       30 kHz in 12000, alias at 2768 Hz %.0f\n", alias);
        check(alias < 0.15 * 12000.0, "a 30 kHz harmonic leaves under 15% alias in the audible band");
    }

    std::printf("== GBA's 32768 Hz passes through untouched\n");
    {
        std::vector<int16_t> gba = tone(440.0, kGbAppAudioRate, 4000, 9000.0);
        std::vector<int16_t> out = chunked(gba, kGbAppAudioRate, {548, 549, 1});
        check(out == gba, "a 32768 Hz stream is copied sample for sample");
    }

    std::printf("== a slower source is upsampled, not dropped\n");
    {
        std::vector<int16_t> slow = tone(300.0, 16384, 16384, 9000.0);
        std::vector<int16_t> out;
        GbAudioResampler r;
        r.process(slow.data(), 16384, 16384, out);
        check(std::labs((long) out.size() / 2 - 32768) <= 2, "16384 Hz becomes 32768 Hz (+-2 frames)");
        double a = goertzel(out, 0, 300.0, kGbAppAudioRate);
        check(a > 0.95 * 9000.0, "and the tone keeps its level");
    }

    std::printf("== the queue\n");
    {
        GbAudioQueue q;
        q.push(sine.data(), 2194, kGbRate);
        size_t pending = q.pendingFrames();
        check(pending >= 548 && pending <= 549, "one video frame of GB audio stages ~548 output frames");
        std::vector<int16_t> buf(1024 * 2);
        int got = q.read(buf.data(), 1024);
        check(got == (int) pending, "read() hands back exactly what was staged");
        check(q.read(buf.data(), 1024) == 0, "a dry queue reads 0, not stale audio");

        // A long stall, then a burst: the backlog must not grow past a quarter second.
        for (int f = 0; f < 120; f++) q.push(sine.data() + (size_t) f * 2194 * 2 % (oneSecond * 2 - 4400), 2194, kGbRate);
        check(q.pendingFrames() <= GbAudioQueue::kMaxBacklogFrames, "the backlog is capped at a quarter second");
        check(q.droppedFrames() > 0, "and what was cut is counted");

        q.setEnabled(false);
        check(q.pendingFrames() == 0, "Game sound off empties what was staged");
        q.push(sine.data(), 2194, kGbRate);
        check(q.read(buf.data(), 1024) == 0, "and nothing new is staged while it is off");
        q.setEnabled(true);
        q.push(sine.data(), 2194, kGbRate);
        check(q.read(buf.data(), 1024) > 0, "Game sound on again: audio flows");

        q.push(sine.data(), 2194, kGbRate);
        q.clear();
        check(q.read(buf.data(), 1024) == 0, "clear() (a new game) drops the old tail");

        // Many small reads and pushes interleaved: nothing is lost or duplicated.
        GbAudioQueue q2;
        std::vector<int16_t> direct;
        GbAudioResampler ref;
        std::vector<int16_t> drained;
        for (int f = 0; f < 30; f++)
        {
            const int16_t* src = sine.data() + (size_t) f * 2194 * 2;
            q2.push(src, 2194, kGbRate);
            ref.process(src, 2194, kGbRate, direct);
            int n;
            while ((n = q2.read(buf.data(), 37)) > 0)
                drained.insert(drained.end(), buf.begin(), buf.begin() + n * 2);
        }
        check(drained == direct, "interleaved pushes and small reads deliver every frame once, in order");
    }

    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
