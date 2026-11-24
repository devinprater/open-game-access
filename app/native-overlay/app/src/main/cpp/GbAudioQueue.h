/*
    GbAudioQueue.h — the Game Boy / GBC / GBA game-audio path on Android.

    Header-only and free of Android and mGBA headers ON PURPOSE: the host test
    (scripts/android-gb-audio-test.sh) compiles this exact file with g++ and
    measures what it does to real sample streams. Nothing here can be tested
    on a phone from CI, so the part that CAN be proven on a host lives where
    a host compiler can reach it.

    ⛔ WHY THIS EXISTS. Android Game Boy games were silent: MGBARunner ran the
    core, mGBA filled its audio ring every frame, and nothing ever read it.
    iOS fixed the same hole in Core/gba_core.cpp (gba_read_audio). Android
    does not link that file -- its GB path is MGBARunner -- so the drain is
    rebuilt here with the SAME contract the other Android sessions use:
    interleaved stereo s16 at 32768 Hz, read from an AudioTrack thread.

    ⛔ TWO THREADS, ONE MUTEX, AND mGBA'S RING IS NEVER TOUCHED BY THE AUDIO
    THREAD. mCircleBuffer is not thread-safe (iOS aborted on exactly this:
    see the audioMutex note in Core/gba_core.cpp). The frame thread drains
    mGBA's ring and push()es here; the AudioTrack thread only read()s here.

    ⛔ THE QUEUE OUTLIVES THE CORE. It is a JNI-file global, not a member of
    MGBARunner, so a stop() that destroys the runner while the AudioTrack
    thread is mid-read cannot leave that thread holding a freed object.
*/
#ifndef GBAUDIOQUEUE_H
#define GBAUDIOQUEUE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace MelonDSAndroid
{

/// The rate every Android game session hands its AudioTrack (DS, PSP,
/// Genesis and now Game Boy), and the rate iOS's engine runs at.
static const unsigned kGbAppAudioRate = 32768;

/// Converts the console's stereo s16 stream to kGbAppAudioRate.
///
/// ⛔ DOWNSAMPLING AVERAGES, IT DOES NOT PICK. A Game Boy produces 131072
/// frames a second; picking every 4th sample (or interpolating between two)
/// folds the square waves' upper harmonics back into the audible band as
/// metallic, out-of-tune whine. Averaging every source frame that falls in
/// an output frame's window is a box low-pass in the same pass, which is
/// what keeps the pulse channels sounding like pulse channels. At a ratio
/// of 1 (the GBA's usual 32768 Hz) each window holds exactly one frame, so
/// the stream passes through unchanged. Below 1 (a source slower than the
/// output) it interpolates linearly.
///
/// ⛔ THE PHASE CARRIES ACROSS CALLS. A frame's worth of samples never maps
/// to a whole number of output frames (2194.4 GB frames per video frame),
/// and restarting the window at every call clicks once a frame -- a 60 Hz
/// buzz. `phase` is how far into the next window the last call stopped, and
/// the partial sums ride along with it.
class GbAudioResampler
{
public:
    void reset()
    {
        phase = 0.0;
        accL = accR = 0.0;
        accN = 0.0;
        haveLast = false;
        lastL = lastR = 0;
    }

    /// Appends the resampled form of `frames` stereo frames from `in`
    /// (interleaved L,R at `srcRate`) to `out`. Returns frames appended.
    size_t process(const int16_t* in, size_t frames, unsigned srcRate, std::vector<int16_t>& out)
    {
        if (!in || frames == 0 || srcRate == 0) return 0;
        const size_t before = out.size();
        const double step = (double) srcRate / (double) kGbAppAudioRate;

        if (step >= 1.0)
        {
            // Box filter: each output frame is the mean of the `step` source
            // frames under its window, weighting the two edge frames by how
            // much of each the window covers.
            for (size_t i = 0; i < frames; i++)
            {
                double l = in[i * 2], r = in[i * 2 + 1];
                double room = step - accN;           // window still to fill
                if (room >= 1.0)
                {
                    accL += l; accR += r; accN += 1.0;
                    if (accN >= step - 1e-9) emit(out, step);
                }
                else
                {
                    // This frame straddles the window edge: its first `room`
                    // finishes the current output frame, the rest opens the next.
                    accL += l * room; accR += r * room; accN += room;
                    emit(out, step);
                    double rest = 1.0 - room;
                    accL = l * rest; accR = r * rest; accN = rest;
                }
            }
        }
        else
        {
            // Upsample: linear interpolation between consecutive source
            // frames, continuing from the previous call's last frame.
            for (size_t i = 0; i < frames; i++)
            {
                int16_t l = in[i * 2], r = in[i * 2 + 1];
                if (!haveLast) { lastL = l; lastR = r; haveLast = true; phase = 0.0; continue; }
                while (phase < 1.0)
                {
                    out.push_back(clamp(lastL + (l - lastL) * phase));
                    out.push_back(clamp(lastR + (r - lastR) * phase));
                    phase += step;
                }
                phase -= 1.0;
                lastL = l; lastR = r;
            }
        }
        return (out.size() - before) / 2;
    }

    static int16_t clamp(double v)
    {
        if (v > 32767.0) return 32767;
        if (v < -32768.0) return -32768;
        return (int16_t) (v >= 0.0 ? v + 0.5 : v - 0.5);
    }

private:
    void emit(std::vector<int16_t>& out, double step)
    {
        out.push_back(clamp(accL / step));
        out.push_back(clamp(accR / step));
        accL = accR = 0.0;
        accN = 0.0;
    }

    double phase = 0.0;
    double accL = 0.0, accR = 0.0, accN = 0.0;
    bool haveLast = false;
    int lastL = 0, lastR = 0;
};

/// The staged game audio between the frame thread and the AudioTrack thread.
class GbAudioQueue
{
public:
    /// ⛔ THE BACKLOG IS BOUNDED. If the frame thread outruns the speaker
    /// (a stall, then a catch-up burst), the oldest audio is dropped so the
    /// game never plays a second behind what is on screen -- and a stopped
    /// AudioTrack can never grow this without bound. A quarter second is
    /// several AudioTrack buffers of slack and still reads as in sync.
    static const size_t kMaxBacklogFrames = kGbAppAudioRate / 4;

    /// Frame thread: resample `frames` console frames at `srcRate` into the queue.
    void push(const int16_t* in, size_t frames, unsigned srcRate)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!enabled) return;
        resampler.process(in, frames, srcRate, staged);
        size_t have = staged.size() / 2 - readPos;
        if (have > kMaxBacklogFrames)
        {
            readPos += have - kMaxBacklogFrames;
            dropped += have - kMaxBacklogFrames;
        }
        compact();
    }

    /// AudioTrack thread: copy up to `maxFrames` stereo frames into `out`.
    /// Returns frames written; 0 when dry.
    int read(int16_t* out, int maxFrames)
    {
        if (!out || maxFrames <= 0) return 0;
        std::lock_guard<std::mutex> lock(mutex);
        size_t have = staged.size() / 2 - readPos;
        if (have == 0) return 0;
        if (have > (size_t) maxFrames) have = (size_t) maxFrames;
        std::memcpy(out, staged.data() + readPos * 2, have * 2 * sizeof(int16_t));
        readPos += have;
        compact();
        return (int) have;
    }

    /// The game-sound switch. Off empties the queue, so turning sound back on
    /// never replays what was produced while it was off.
    void setEnabled(bool on)
    {
        std::lock_guard<std::mutex> lock(mutex);
        enabled = on;
        if (!on) clearLocked();
    }

    bool isEnabled()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return enabled;
    }

    /// A new game, or the end of one: forget everything staged and the phase.
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        clearLocked();
    }

    size_t pendingFrames()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return staged.size() / 2 - readPos;
    }

    size_t droppedFrames()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return dropped;
    }

private:
    void clearLocked()
    {
        staged.clear();
        readPos = 0;
        resampler.reset();
    }

    // Shift unread audio to the front once the read position passes half the
    // buffer: cheap (rare, small) and keeps the vector from growing forever.
    void compact()
    {
        if (readPos == 0) return;
        if (readPos * 2 >= staged.size())
        {
            staged.clear();
            readPos = 0;
            return;
        }
        if (readPos * 2 < staged.size() / 2) return;
        staged.erase(staged.begin(), staged.begin() + (std::ptrdiff_t) (readPos * 2));
        readPos = 0;
    }

    std::mutex mutex;
    GbAudioResampler resampler;
    std::vector<int16_t> staged;
    size_t readPos = 0;
    size_t dropped = 0;
    bool enabled = true;
};

} // namespace MelonDSAndroid

#endif // GBAUDIOQUEUE_H
